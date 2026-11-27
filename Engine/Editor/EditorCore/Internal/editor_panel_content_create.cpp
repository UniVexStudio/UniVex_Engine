#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shobjidl.h>
#endif
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content panel's making and managing of things: the "+ Add" catalogue (also opened by a
// right-click on empty space), the right-click menu of a file, inline rename, and dragging an
// entity asset into the scene.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "uve_file_envelope.h"
#include "shader_asset.h"

#include "name_component.h"
#include "editor_content_catalogue.h"

namespace UVE::Editor {
namespace {

#if defined(_WIN32)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
using GetOpenFileNameWFn = BOOL (WINAPI *)(LPOPENFILENAMEW);
using CoInitializeExFn = HRESULT (WINAPI *)(LPVOID, DWORD);
using CoUninitializeFn = void (WINAPI *)();
using CoCreateInstanceFn = HRESULT (WINAPI *)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
using CoTaskMemFreeFn = void (WINAPI *)(LPVOID);

inline GetOpenFileNameWFn LoadGetOpenFileNameW(HMODULE comdlg) noexcept {
    if (comdlg == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<GetOpenFileNameWFn>(GetProcAddress(comdlg, "GetOpenFileNameW"));
}

enum class WinNativePickOutcome : std::uint8_t {
    Picked,
    Cancelled,
    Unavailable,
};

// Standard COM CLSID_FileOpenDialog / IID_IFileOpenDialog defined inline so MinGW/MSVC need no extra -luuid/-lole32 link libs.
inline constexpr GUID kClsidFileOpenDialog{
    0xDC1C5A9CU, 0xE88AU, 0x4DDEU, {0xA5U, 0xA1U, 0x60U, 0xF8U, 0x2AU, 0x20U, 0xAEU, 0xF7U}};
inline constexpr GUID kIidIFileOpenDialog{
    0xD57C7288U, 0xD4ADU, 0x4768U, {0xBEU, 0x02U, 0x9DU, 0x96U, 0x95U, 0x32U, 0xD9U, 0x60U}};

[[nodiscard]] WinNativePickOutcome PickWithWindowsFileOpenDialog(
    const bool pickFolders, std::vector<std::filesystem::path>& outPaths) {
    outPaths.clear();
    HMODULE ole32 = LoadLibraryA("ole32.dll");
    if (ole32 == nullptr) {
        return WinNativePickOutcome::Unavailable;
    }
    const auto pCoInitializeEx = reinterpret_cast<CoInitializeExFn>(GetProcAddress(ole32, "CoInitializeEx"));
    const auto pCoUninitialize = reinterpret_cast<CoUninitializeFn>(GetProcAddress(ole32, "CoUninitialize"));
    const auto pCoCreateInstance = reinterpret_cast<CoCreateInstanceFn>(GetProcAddress(ole32, "CoCreateInstance"));
    const auto pCoTaskMemFree = reinterpret_cast<CoTaskMemFreeFn>(GetProcAddress(ole32, "CoTaskMemFree"));
    if (pCoInitializeEx == nullptr || pCoUninitialize == nullptr || pCoCreateInstance == nullptr ||
        pCoTaskMemFree == nullptr) {
        FreeLibrary(ole32);
        return WinNativePickOutcome::Unavailable;
    }

    const HRESULT initHr = pCoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninit = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        FreeLibrary(ole32);
        return WinNativePickOutcome::Unavailable;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = pCoCreateInstance(kClsidFileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, kIidIFileOpenDialog,
                                   reinterpret_cast<void**>(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (shouldUninit) {
            pCoUninitialize();
        }
        FreeLibrary(ole32);
        return WinNativePickOutcome::Unavailable;
    }

    DWORD options = 0U;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_ALLOWMULTISELECT | FOS_PATHMUSTEXIST;
        if (pickFolders) {
            options |= FOS_PICKFOLDERS;
        } else {
            options |= FOS_FILEMUSTEXIST;
        }
        dialog->SetOptions(options);
    }
    dialog->SetTitle(pickFolders ? L"Select Folder(s) to Import" : L"Select Assets to Import");
    if (!pickFolders) {
        static constexpr COMDLG_FILTERSPEC kFilters[] = {
            {L"All Supported Assets",
             L"*.fbx;*.gltf;*.glb;*.obj;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.wav;*.ogg;*.mp3;*.uvs;*.uvshader;*.glsl;*.uvanim"},
            {L"3D Models & Animations (*.fbx;*.gltf;*.glb;*.obj;*.uvanim)", L"*.fbx;*.gltf;*.glb;*.obj;*.uvanim"},
            {L"Images & Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr)", L"*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr"},
            {L"Audio (*.wav;*.ogg;*.mp3)", L"*.wav;*.ogg;*.mp3"},
            {L"Scripts & Shaders (*.uvs;*.uvshader;*.glsl)", L"*.uvs;*.uvshader;*.glsl"},
            {L"All Files (*.*)", L"*.*"},
        };
        dialog->SetFileTypes(static_cast<UINT>(sizeof(kFilters) / sizeof(kFilters[0])), kFilters);
        dialog->SetFileTypeIndex(1U);
    }

    WinNativePickOutcome outcome = WinNativePickOutcome::Cancelled;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr) {
            DWORD count = 0U;
            if (SUCCEEDED(items->GetCount(&count))) {
                outPaths.reserve(static_cast<std::size_t>(count));
                for (DWORD i = 0U; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item != nullptr) {
                        PWSTR pszPath = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath != nullptr) {
                            outPaths.emplace_back(pszPath);
                            pCoTaskMemFree(pszPath);
                        }
                        item->Release();
                    }
                }
            }
            items->Release();
        }
        outcome = outPaths.empty() ? WinNativePickOutcome::Cancelled : WinNativePickOutcome::Picked;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        outcome = WinNativePickOutcome::Cancelled;
    } else {
        outcome = WinNativePickOutcome::Unavailable;
    }

    dialog->Release();
    if (shouldUninit) {
        pCoUninitialize();
    }
    FreeLibrary(ole32);
    return outcome;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

constexpr std::size_t kMaximumContentRecent = 5U;
constexpr std::size_t kMaximumContentNameBytes = 96U;

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

/// A file name a person typed: not empty, no separators, no "." or "..", nothing a filesystem
/// rejects on any platform we ship to.
[[nodiscard]] bool IsContentNameValid(const std::string_view name) {
    if (name.empty() || name.size() > kMaximumContentNameBytes || name == "." || name == ".." ||
        name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](const char character) {
        return std::iscntrl(static_cast<unsigned char>(character)) != 0 ||
               std::string_view{"/\\:*?\"<>|"}.find(character) != std::string_view::npos;
    });
}

} // namespace

namespace {

}

std::optional<std::filesystem::path> Editor::RenameContentFile(const std::filesystem::path& file,
                                                                     const std::string_view newStem) {
    std::error_code error;
    if (!IsContentNameValid(newStem) || !std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    const bool directory = std::filesystem::is_directory(file, error);
    const std::filesystem::path target =
        file.parent_path() / (std::string{newStem} + (directory ? std::string{} : file.extension().string()));
    if (target == file) {
        return target;
    }
    if (std::filesystem::exists(target, error)) {
        return std::nullopt;
    }
    std::filesystem::rename(file, target, error);
    if (error) {
        return std::nullopt;
    }
    return target;
}

std::optional<std::filesystem::path> Editor::RenameContentAsset(const std::filesystem::path& file,
                                                                      const std::string_view newStem) {
    const std::optional<std::filesystem::path> target = RenameContentFile(file, newStem);
    if (!target.has_value() || *target == file) {
        return target;
    }
    // The file keeps its GUID under the new name, so scenes and players that use it still do.
    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    for (const Asset::AssetRecord& record : assetDatabase.GetRegisteredAssets()) {
        if (record.path == file.lexically_normal()) {
            static_cast<void>(assetDatabase.Relocate(record.guid, *target));
        }
    }
    const std::string ext = LowerExtension(*target);
    const bool isSceneOrEntityOrPrefab = (ext == ".uventity" || ext == ".uvprefab" || ext == ".uvscene");

    if (m_entityEditSession.has_value() && m_entityEditSession->assetPath == file) {
        m_entityEditSession->assetPath = *target;
        const Scene::Entity entityRoot = GetEntityEditorRoot();
        if (entityRoot != Scene::kInvalidEntity) {
            Scene::IEntityManager& entityManager = m_services->GetEntityManager();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponent<Scene::NameComponent>(entityRoot)) {
                entityManager.GetComponent<Scene::NameComponent>(entityRoot).name = newName;
            }
            static_cast<void>(SaveEntityEditor());
        }
    } else if (m_activeScenePath == file) {
        m_activeScenePath = *target;
        const Scene::Entity sceneRoot = GetDocumentSceneRoot();
        if (sceneRoot != Scene::kInvalidEntity) {
            Scene::IEntityManager& entityManager = m_services->GetEntityManager();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponent<Scene::NameComponent>(sceneRoot)) {
                entityManager.GetComponent<Scene::NameComponent>(sceneRoot).name = newName;
            }
        }
    }

    if (isSceneOrEntityOrPrefab) {
        if (const auto fileData = Asset::ReadUveFile(*target); fileData.has_value()) {
            const auto& [header, payload] = *fileData;
            const std::string payloadText(reinterpret_cast<const char*>(payload.data()), payload.size());
            nlohmann::json payloadJson = nlohmann::json::parse(payloadText, nullptr, false);
            if (!payloadJson.is_discarded() && payloadJson.contains("entities") &&
                payloadJson["entities"].is_array() && !payloadJson["entities"].empty()) {
                const std::string newName = target->stem().string();
                for (auto& entityObj : payloadJson["entities"]) {
                    bool isRoot = true;
                    if (entityObj.contains("components") && entityObj["components"].is_object()) {
                        const auto& comps = entityObj["components"];
                        const auto hIt = comps.contains("HierarchyComponent") ? comps.find("HierarchyComponent")
                                                                              : comps.find("HierarchyComponentUVE");
                        if (hIt != comps.end() && hIt->is_object() && hIt->contains("parentLocalId") &&
                            (*hIt)["parentLocalId"].get<std::int64_t>() >= 0) {
                            isRoot = false;
                        }
                    }
                    if (isRoot) {
                        if (!entityObj.contains("components") || !entityObj["components"].is_object()) {
                            entityObj["components"] = nlohmann::json::object();
                        }
                        const char* nameKey = entityObj["components"].contains("NameComponentUVE") &&
                                                      !entityObj["components"].contains("NameComponent")
                                                  ? "NameComponentUVE"
                                                  : "NameComponent";
                        if (!entityObj["components"].contains(nameKey) || !entityObj["components"][nameKey].is_object()) {
                            entityObj["components"][nameKey] = nlohmann::json::object();
                        }
                        entityObj["components"][nameKey]["name"] = newName;
                        break;
                    }
                }
                const std::string dumped = payloadJson.dump(4);
                std::vector<std::byte> payloadBytes(dumped.size());
                std::memcpy(payloadBytes.data(), dumped.data(), dumped.size());
                static_cast<void>(Asset::WriteUveFile(*target, header.assetType, payloadBytes));
            }
        }
    }
    return target;
}

std::optional<std::filesystem::path> Editor::DuplicateContentFile(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    const bool directory = std::filesystem::is_directory(file, error);
    const std::filesystem::path target =
        MakeUniqueContentPath(file.parent_path(), (directory ? file.filename() : file.stem()).string(),
                                 directory ? std::string_view{} : std::string_view{file.extension().string()});
    std::filesystem::copy(file, target, std::filesystem::copy_options::recursive, error);
    if (error) {
        return std::nullopt;
    }
    return target;
}

void Editor::PushContentCreateRecent(std::vector<std::string>& recent, const std::string_view id) {
    std::erase(recent, id);
    recent.insert(recent.begin(), std::string{id});
    if (recent.size() > kMaximumContentRecent) {
        recent.resize(kMaximumContentRecent);
    }
}

void Editor::BeginContentRename(const std::filesystem::path& relativePath) {
    m_contentRenamePath = relativePath;
    std::error_code error;
    const std::filesystem::path absolute = m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativePath;
    m_contentRenameText = (std::filesystem::is_directory(absolute, error) ? relativePath.filename() : relativePath.stem())
                              .string();
    m_contentRenameFocus = true;
}

namespace {
SystemImportPicker g_systemImportPicker;
SystemImportFilesPicker g_systemImportFilesPicker;
SystemImportFolderPicker g_systemImportFolderPicker;

[[nodiscard]] std::filesystem::path DefaultImportStartDirectory() {
#if defined(__ANDROID__)
    const char* const storage = std::getenv("UNIVEX_ANDROID_PUBLIC_STORAGE");
    const std::filesystem::path home = (storage != nullptr && *storage != '\0') ? std::filesystem::path(storage)
                                                                                : std::filesystem::path("/storage/emulated/0");
    const std::filesystem::path downloads = home / "Download";
#else
#if defined(_WIN32)
    const char* const homeEnv = std::getenv("USERPROFILE");
#else
    const char* const homeEnv = std::getenv("HOME");
#endif
    const std::filesystem::path home = homeEnv != nullptr ? std::filesystem::path(homeEnv) : std::filesystem::path();
    const std::filesystem::path downloads = home / "Downloads";
#endif
    std::error_code error;
    return std::filesystem::is_directory(downloads, error) ? downloads : home;
}

[[nodiscard]] bool IsSameOrSubpath(const std::filesystem::path& parent, const std::filesystem::path& candidate) {
    std::error_code ec;
    const std::filesystem::path pNorm = std::filesystem::weakly_canonical(parent, ec).lexically_normal();
    if (ec) {
        return false;
    }
    const std::filesystem::path cNorm = std::filesystem::weakly_canonical(candidate, ec).lexically_normal();
    if (ec) {
        return false;
    }
    auto pIt = pNorm.begin();
    auto cIt = cNorm.begin();
    for (; pIt != pNorm.end(); ++pIt, ++cIt) {
        if (cIt == cNorm.end() || *pIt != *cIt) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasHiddenSegment(const std::filesystem::path& relativePath) {
    for (const std::filesystem::path& part : relativePath) {
        const std::string segment = part.string();
        if (!segment.empty() && segment != "." && segment != ".." && segment.front() == '.') {
            return true;
        }
    }
    return false;
}
} // namespace

void SetSystemImportPicker(SystemImportPicker picker) {
    g_systemImportPicker = std::move(picker);
}

void SetSystemImportFilesPicker(SystemImportFilesPicker picker) {
    g_systemImportFilesPicker = std::move(picker);
}

void SetSystemImportFolderPicker(SystemImportFolderPicker picker) {
    g_systemImportFolderPicker = std::move(picker);
}

bool Editor::PromptImportFiles(const std::filesystem::path& targetDirectory) {
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_assetTableDirectory) : targetDirectory;

#if defined(_WIN32)
    {
        std::vector<std::filesystem::path> chosenFiles;
        const WinNativePickOutcome outcome = PickWithWindowsFileOpenDialog(false, chosenFiles);
        if (outcome == WinNativePickOutcome::Picked) {
            return ImportExternalFiles(chosenFiles, destDir);
        }
        if (outcome == WinNativePickOutcome::Cancelled) {
            return false;
        }
    }
    // Fallback if COM IFileOpenDialog is unavailable.
    HMODULE comdlg = LoadLibraryA("comdlg32.dll");
    if (comdlg != nullptr) {
        const GetOpenFileNameWFn pGetOpenFileNameW = LoadGetOpenFileNameW(comdlg);
        if (pGetOpenFileNameW != nullptr) {
            std::vector<wchar_t> buffer(65536, 0);
            OPENFILENAMEW ofn;
            std::memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = buffer.data();
            ofn.nMaxFile = static_cast<DWORD>(buffer.size());
            ofn.lpstrTitle = L"Select Assets to Import";
            ofn.lpstrFilter =
                L"All Supported Assets\0*.fbx;*.gltf;*.glb;*.obj;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.wav;*.ogg;*.mp3;*.uvs;*.uvshader;*.glsl;*.uvanim\0"
                L"3D Models & Animations (*.fbx;*.gltf;*.glb;*.obj;*.uvanim)\0*.fbx;*.gltf;*.glb;*.obj;*.uvanim\0"
                L"Images & Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0"
                L"Audio (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0"
                L"Scripts & Shaders (*.uvs;*.uvshader;*.glsl)\0*.uvs;*.uvshader;*.glsl\0"
                L"All Files (*.*)\0*.*\0\0";
            ofn.nFilterIndex = 1;
            // NOCHANGEDIR: the project's relative paths stay valid after browsing elsewhere.
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_NOCHANGEDIR;

            if (pGetOpenFileNameW(&ofn)) {
                FreeLibrary(comdlg);
                std::vector<std::filesystem::path> chosenFiles;
                const wchar_t* ptr = buffer.data();
                std::wstring firstString(ptr);
                ptr += firstString.size() + 1U;
                if (*ptr == L'\0') {
                    chosenFiles.push_back(std::filesystem::path(firstString));
                } else {
                    std::filesystem::path baseDir(firstString);
                    while (*ptr != L'\0') {
                        std::wstring filename(ptr);
                        chosenFiles.push_back(baseDir / filename);
                        ptr += filename.size() + 1U;
                    }
                }
                return ImportExternalFiles(chosenFiles, destDir);
            }
            FreeLibrary(comdlg);
            return false;   // cancelled: no second dialog
        }
        FreeLibrary(comdlg);
    }
#endif

    // Android: straight into the system file manager with multi-select enabled (reaches Downloads,
    // Drive, USB); chosen files arrive staged in cache and are imported one-by-one into destDir.
    if (g_systemImportFilesPicker &&
        g_systemImportFilesPicker([this, destDir](const std::vector<std::filesystem::path>& chosen) {
            static_cast<void>(ImportExternalFiles(chosen, destDir, true));
        })) {
        return false;
    }
    if (g_systemImportPicker &&
        g_systemImportPicker([this, destDir](const std::filesystem::path& chosen) {
            static_cast<void>(ImportExternalFiles({chosen}, destDir, true));
        })) {
        return false;
    }

    // Fallback: PRISM file dialog with multi-select enabled.
    FilePickerRequest request;
    request.mode = FilePickerMode::Open;
    request.allowMultiSelect = true;
    request.title = "Import Assets";
    request.extensions = {".fbx", ".gltf", ".glb", ".obj", ".uvanim", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".wav", ".ogg", ".mp3", ".uvs", ".uvshader", ".glsl"};
    request.onPick = [this, destDir](const std::filesystem::path& chosen) {
        static_cast<void>(ImportExternalFiles({chosen}, destDir));
    };
    request.onPickMany = [this, destDir](const std::vector<std::filesystem::path>& chosen) {
        static_cast<void>(ImportExternalFiles(chosen, destDir));
    };
    request.startDirectory = DefaultImportStartDirectory();
    OpenFilePicker(std::move(request));
    return false;
}

bool Editor::PromptImportFolder(const std::filesystem::path& targetDirectory) {
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_assetTableDirectory) : targetDirectory;

#if defined(_WIN32)
    {
        std::vector<std::filesystem::path> chosenFolders;
        const WinNativePickOutcome outcome = PickWithWindowsFileOpenDialog(true, chosenFolders);
        if (outcome == WinNativePickOutcome::Picked) {
            return ImportExternalFiles(chosenFolders, destDir);
        }
        if (outcome == WinNativePickOutcome::Cancelled) {
            return false;
        }
    }
#endif

    // Android: native SAF folder picker (ACTION_OPEN_DOCUMENT_TREE) preserving subfolder layout.
    if (g_systemImportFolderPicker &&
        g_systemImportFolderPicker([this, destDir](const std::vector<std::filesystem::path>& chosen) {
            static_cast<void>(ImportExternalFiles(chosen, destDir, true));
        })) {
        return false;
    }

    // Linux / fallback: PRISM folder picker.
    FilePickerRequest request;
    request.mode = FilePickerMode::OpenFolder;
    request.title = "Import Folder";
    request.onPick = [this, destDir](const std::filesystem::path& chosen) {
        static_cast<void>(ImportExternalFiles({chosen}, destDir));
    };
    request.onPickMany = [this, destDir](const std::vector<std::filesystem::path>& chosen) {
        static_cast<void>(ImportExternalFiles(chosen, destDir));
    };
    request.startDirectory = DefaultImportStartDirectory();
    OpenFilePicker(std::move(request));
    return false;
}

bool Editor::ImportExternalFiles(const std::vector<std::filesystem::path>& sourceFiles,
                                      const std::filesystem::path& targetDirectory,
                                      const bool deleteStagedSources) {
    if (m_services == nullptr || sourceFiles.empty()) {
        return false;
    }
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_assetTableDirectory) : targetDirectory;
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copyItems;
    std::vector<std::filesystem::path> cleanupDirs;

    for (const std::filesystem::path& rawSrc : sourceFiles) {
        std::error_code stErr;
        const std::filesystem::file_status st = std::filesystem::symlink_status(rawSrc, stErr);
        if (stErr || !std::filesystem::exists(st) || std::filesystem::is_symlink(st)) {
            continue;
        }
        if (std::filesystem::is_directory(st)) {
            // Reject self-recursive folder copy (destination inside or equal to source).
            if (IsSameOrSubpath(rawSrc, destDir)) {
                m_contentStatusMessage = "Skipped self-recursive folder import: " + rawSrc.filename().generic_string();
                continue;
            }
            const std::filesystem::path folderName = rawSrc.filename().empty() ? rawSrc.parent_path().filename() : rawSrc.filename();
            const std::filesystem::path targetRoot = folderName.empty() ? destDir : (destDir / folderName);
            std::error_code walkErr;
            for (std::filesystem::recursive_directory_iterator it(
                     rawSrc, std::filesystem::directory_options::skip_permission_denied, walkErr),
                 end;
                 !walkErr && it != end; it.increment(walkErr)) {
                std::error_code entryErr;
                const std::filesystem::file_status entrySt = it->symlink_status(entryErr);
                if (entryErr || std::filesystem::is_symlink(entrySt)) {
                    it.disable_recursion_pending();
                    continue;
                }
                const std::string fname = it->path().filename().string();
                if (fname.empty() || fname.front() == '.') {
                    if (std::filesystem::is_directory(entrySt)) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!std::filesystem::is_regular_file(entrySt) || it->path().extension() == ".uvcopy-tmp") {
                    continue;
                }
                const std::filesystem::path rel = std::filesystem::relative(it->path(), rawSrc, entryErr);
                if (entryErr || rel.empty() || HasHiddenSegment(rel)) {
                    continue;
                }
                const std::filesystem::path destFile = targetRoot / rel;
                std::error_code eqErr;
                if (std::filesystem::weakly_canonical(it->path(), eqErr) == std::filesystem::weakly_canonical(destFile, eqErr) && !eqErr) {
                    continue;
                }
                copyItems.emplace_back(it->path(), destFile);
            }
            if (deleteStagedSources) {
                cleanupDirs.push_back(rawSrc.parent_path());
            }
        } else if (std::filesystem::is_regular_file(st)) {
            const std::string fname = rawSrc.filename().string();
            if (fname.empty() || fname.front() == '.' || rawSrc.extension() == ".uvcopy-tmp") {
                continue;
            }
            const std::filesystem::path destFile = destDir / rawSrc.filename();
            std::error_code eqErr;
            if (std::filesystem::weakly_canonical(rawSrc, eqErr) == std::filesystem::weakly_canonical(destFile, eqErr) && !eqErr) {
                continue;
            }
            copyItems.emplace_back(rawSrc, destFile);
            if (deleteStagedSources && rawSrc.has_parent_path()) {
                cleanupDirs.push_back(rawSrc.parent_path());
            }
        }
    }

    // Deduplicate staged cleanup roots.
    if (!cleanupDirs.empty()) {
        std::sort(cleanupDirs.begin(), cleanupDirs.end());
        cleanupDirs.erase(std::unique(cleanupDirs.begin(), cleanupDirs.end()), cleanupDirs.end());
    }

    if (copyItems.empty()) {
        for (const std::filesystem::path& dir : cleanupDirs) {
            std::error_code rmErr;
            std::filesystem::remove_all(dir, rmErr);
        }
        return false;
    }

    const std::string targetLabel = destDir.filename().empty() ? "Content" : destDir.filename().generic_string();

    // Interactive PRISM mode: copy strictly one file at a time on the single background worker
    // so importing 500-1,000+ assets never freezes or crashes low-end desktop/mobile devices.
    if (m_prismUiRequested) {
        const std::size_t count = copyItems.size();
        EnqueueBackgroundExternalCopies(std::move(copyItems), std::move(cleanupDirs));
        m_contentStatusMessage = "Queued " + std::to_string(count) + " asset(s) for import into " + targetLabel + ".";
        return true;
    }

    // Headless/test mode: copy synchronously and deterministically.
    std::size_t importedCount = 0U;
    for (const auto& [src, destFile] : copyItems) {
        std::error_code copyErr;
        if (destFile.has_parent_path()) {
            std::filesystem::create_directories(destFile.parent_path(), copyErr);
        }
        if (!copyErr) {
            std::filesystem::copy_file(src, destFile, std::filesystem::copy_options::overwrite_existing, copyErr);
        }
        if (!copyErr) {
            ++importedCount;
            m_services->GetAssetDatabase().Register(destFile);
        }
    }
    for (const std::filesystem::path& dir : cleanupDirs) {
        std::error_code rmErr;
        std::filesystem::remove_all(dir, rmErr);
    }

    if (importedCount > 0U) {
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndex(); // queues the new files' model and animation imports
        m_contentStatusMessage = "Imported " + std::to_string(importedCount) + " asset(s) into " + targetLabel + ".";
        return true;
    }
    return false;
}

void Editor::OpenCreateShaderModal(const std::filesystem::path& targetDirectory,
                                        std::function<void(const std::filesystem::path&)> onCreated) {
    m_createShaderModal = CreateShaderModalState{};
    m_createShaderModal.isOpen = true;
    m_createShaderModal.openRequested = true;
    m_createShaderModal.targetDirectory = targetDirectory.empty() ? m_assetTableDirectory : targetDirectory;
    m_createShaderModal.shaderTypeIndex = 0;
    m_createShaderModal.onShaderCreated = std::move(onCreated);

    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path contentRoot = snapshot.contentRoot;
    std::filesystem::path fullDir = m_createShaderModal.targetDirectory;
    if (fullDir.is_relative()) {
        fullDir = contentRoot / fullDir;
    }
    const std::filesystem::path fullDefault = fullDir / "NewShader.uvshader";
    const std::filesystem::path relDefault = fullDefault.lexically_relative(contentRoot);
    m_createShaderModal.shaderPathText = (relDefault.empty() || *relDefault.begin() == "..") ? fullDefault.generic_string() : relDefault.generic_string();
}

} // namespace UVE::Editor
