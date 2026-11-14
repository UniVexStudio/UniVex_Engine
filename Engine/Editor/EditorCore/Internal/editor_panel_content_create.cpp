#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
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
inline GetOpenFileNameWFn LoadGetOpenFileNameW(HMODULE comdlg) noexcept {
    if (comdlg == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<GetOpenFileNameWFn>(GetProcAddress(comdlg, "GetOpenFileNameW"));
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

constexpr std::size_t kMaximumContentRecentUVE = 5U;
constexpr std::size_t kMaximumContentNameBytesUVE = 96U;

[[nodiscard]] std::string LowerExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

/// A file name a person typed: not empty, no separators, no "." or "..", nothing a filesystem
/// rejects on any platform we ship to.
[[nodiscard]] bool IsContentNameValidUVE(const std::string_view name) {
    if (name.empty() || name.size() > kMaximumContentNameBytesUVE || name == "." || name == ".." ||
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

std::optional<std::filesystem::path> EditorUVE::RenameContentFileUVE(const std::filesystem::path& file,
                                                                     const std::string_view newStem) {
    std::error_code error;
    if (!IsContentNameValidUVE(newStem) || !std::filesystem::exists(file, error)) {
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

std::optional<std::filesystem::path> EditorUVE::RenameContentAssetUVE(const std::filesystem::path& file,
                                                                      const std::string_view newStem) {
    const std::optional<std::filesystem::path> target = RenameContentFileUVE(file, newStem);
    if (!target.has_value() || *target == file) {
        return target;
    }
    // The file keeps its GUID under the new name, so scenes and players that use it still do.
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    for (const Asset::AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
        if (record.path == file.lexically_normal()) {
            static_cast<void>(assetDatabase.RelocateUVE(record.guid, *target));
        }
    }
    const std::string ext = LowerExtensionUVE(*target);
    const bool isSceneOrEntityOrPrefab = (ext == ".uventity" || ext == ".uvprefab" || ext == ".uvscene");

    if (m_entityEditSession.has_value() && m_entityEditSession->assetPath == file) {
        m_entityEditSession->assetPath = *target;
        const Scene::EntityUVE entityRoot = GetEntityEditorRootUVE();
        if (entityRoot != Scene::kInvalidEntityUVE) {
            Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entityRoot)) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(entityRoot).name = newName;
            }
            static_cast<void>(SaveEntityEditorUVE());
        }
    } else if (m_activeScenePath == file) {
        m_activeScenePath = *target;
        const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
        if (sceneRoot != Scene::kInvalidEntityUVE) {
            Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(sceneRoot)) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(sceneRoot).name = newName;
            }
        }
    }

    if (isSceneOrEntityOrPrefab) {
        if (const auto fileData = Asset::ReadUveFileUVE(*target); fileData.has_value()) {
            const auto& [header, payload] = *fileData;
            const std::string payloadText(reinterpret_cast<const char*>(payload.data()), payload.size());
            nlohmann::json payloadJson = nlohmann::json::parse(payloadText, nullptr, false);
            if (!payloadJson.is_discarded() && payloadJson.contains("entities") &&
                payloadJson["entities"].is_array() && !payloadJson["entities"].empty()) {
                const std::string newName = target->stem().string();
                for (auto& entityObj : payloadJson["entities"]) {
                    bool isRoot = true;
                    if (entityObj.contains("components") && entityObj["components"].contains("HierarchyComponentUVE")) {
                        const auto& h = entityObj["components"]["HierarchyComponentUVE"];
                        if (h.contains("parentLocalId") && h["parentLocalId"].get<std::int64_t>() >= 0) {
                            isRoot = false;
                        }
                    }
                    if (isRoot) {
                        if (!entityObj.contains("components") || !entityObj["components"].is_object()) {
                            entityObj["components"] = nlohmann::json::object();
                        }
                        if (!entityObj["components"].contains("NameComponentUVE") || !entityObj["components"]["NameComponentUVE"].is_object()) {
                            entityObj["components"]["NameComponentUVE"] = nlohmann::json::object();
                        }
                        entityObj["components"]["NameComponentUVE"]["name"] = newName;
                        break;
                    }
                }
                const std::string dumped = payloadJson.dump(4);
                std::vector<std::byte> payloadBytes(dumped.size());
                std::memcpy(payloadBytes.data(), dumped.data(), dumped.size());
                static_cast<void>(Asset::WriteUveFileUVE(*target, header.assetType, payloadBytes));
            }
        }
    }
    return target;
}

std::optional<std::filesystem::path> EditorUVE::DuplicateContentFileUVE(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    const bool directory = std::filesystem::is_directory(file, error);
    const std::filesystem::path target =
        MakeUniqueContentPathUVE(file.parent_path(), (directory ? file.filename() : file.stem()).string(),
                                 directory ? std::string_view{} : std::string_view{file.extension().string()});
    std::filesystem::copy(file, target, std::filesystem::copy_options::recursive, error);
    if (error) {
        return std::nullopt;
    }
    return target;
}

void EditorUVE::PushContentCreateRecentUVE(std::vector<std::string>& recent, const std::string_view id) {
    std::erase(recent, id);
    recent.insert(recent.begin(), std::string{id});
    if (recent.size() > kMaximumContentRecentUVE) {
        recent.resize(kMaximumContentRecentUVE);
    }
}

void EditorUVE::BeginContentRenameUVE(const std::filesystem::path& relativePath) {
    m_contentRenamePath = relativePath;
    std::error_code error;
    const std::filesystem::path absolute = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / relativePath;
    m_contentRenameText = (std::filesystem::is_directory(absolute, error) ? relativePath.filename() : relativePath.stem())
                              .string();
    m_contentRenameFocus = true;
}

bool EditorUVE::PromptImportFilesUVE(const std::filesystem::path& targetDirectory) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_contentBrowserDirectory) : targetDirectory;

#if defined(_WIN32)
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
                L"All Supported Assets\0*.fbx;*.gltf;*.glb;*.obj;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.wav;*.ogg;*.mp3;*.uvs;*.uvshader;*.glsl\0"
                L"3D Models (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0"
                L"Images & Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0"
                L"Audio (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0"
                L"Scripts & Shaders (*.uvs;*.uvshader;*.glsl)\0*.uvs;*.uvshader;*.glsl\0"
                L"All Files (*.*)\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

            if (pGetOpenFileNameW(&ofn)) {
                FreeLibrary(comdlg);
                std::vector<std::filesystem::path> chosenFiles;
                const wchar_t* ptr = buffer.data();
                std::wstring firstString(ptr);
                ptr += firstString.size() + 1U;
                if (*ptr == L'\0') {
                    // Single file selected
                    chosenFiles.push_back(std::filesystem::path(firstString));
                } else {
                    // Multiple files: first string is directory, followed by null-separated filenames
                    std::filesystem::path baseDir(firstString);
                    while (*ptr != L'\0') {
                        std::wstring filename(ptr);
                        chosenFiles.push_back(baseDir / filename);
                        ptr += filename.size() + 1U;
                    }
                }
                return ImportExternalFilesUVE(chosenFiles, destDir);
            }
        }
        FreeLibrary(comdlg);
    }
#endif

    // Any file on this computer, through the editor's own file dialog; it is imported into destDir.
    FilePickerRequestUVE request;
    request.mode = FilePickerModeUVE::Open;
    request.title = "Import Assets";
    request.extensions = {".fbx", ".gltf", ".glb", ".obj", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".wav", ".ogg", ".mp3", ".uvshader", ".glsl"};
    request.onPick = [this, destDir](const std::filesystem::path& chosen) { static_cast<void>(ImportExternalFilesUVE({chosen}, destDir)); };
    OpenFilePickerUVE(std::move(request));
    return false;
}

bool EditorUVE::ImportExternalFilesUVE(const std::vector<std::filesystem::path>& sourceFiles,
                                      const std::filesystem::path& targetDirectory) {
    if (m_services == nullptr || sourceFiles.empty()) {
        return false;
    }
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_contentBrowserDirectory) : targetDirectory;
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    std::size_t importedCount = 0;
    for (const std::filesystem::path& src : sourceFiles) {
        if (!std::filesystem::exists(src, ec)) {
            continue;
        }
        const std::filesystem::path destFile = destDir / src.filename();
        std::filesystem::copy_file(src, destFile, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            ++importedCount;
            m_services->GetAssetDatabaseUVE().RegisterUVE(destFile);
        }
    }

    if (importedCount > 0U) {
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndexUVE();
        const Asset::ProjectFileSnapshotUVE freshSnapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        QueueModelAutoImportsUVE(freshSnapshot);
        m_contentStatusMessage = "Imported " + std::to_string(importedCount) + " asset(s) into " +
                                 (destDir.filename().empty() ? "Content" : destDir.filename().generic_string()) + ".";
        return true;
    }
    return false;
}

void EditorUVE::OpenCreateShaderModalUVE(const std::filesystem::path& targetDirectory,
                                        std::function<void(const std::filesystem::path&)> onCreated) {
    m_createShaderModal = CreateShaderModalStateUVE{};
    m_createShaderModal.isOpen = true;
    m_createShaderModal.openRequested = true;
    m_createShaderModal.targetDirectory = targetDirectory.empty() ? m_contentBrowserDirectory : targetDirectory;
    m_createShaderModal.shaderTypeIndex = 0;
    m_createShaderModal.onShaderCreated = std::move(onCreated);

    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
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
