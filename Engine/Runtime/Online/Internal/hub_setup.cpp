// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_setup.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "application_runtime.h"
#include "asset_guid.h"
#include "editor_project_package.h"
#include "process_launcher.h"
#include "tar_gz_archive.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace UVE::Online {
namespace {

inline constexpr std::size_t kCopyChunkBytesUVE = 1U << 16U;
inline constexpr std::string_view kStagingSuffixUVE = ".installing";
inline constexpr std::string_view kPreviousSuffixUVE = ".previous";
#if !defined(_WIN32)
// freedesktop.org locations under the per-user data folder.
inline constexpr std::string_view kApplicationsDirectoryNameUVE = "applications";
inline constexpr std::string_view kDesktopEntryFileNameUVE = "univex-hub.desktop";
inline constexpr std::string_view kDesktopEntryCategoriesUVE = "Development;IDE;";
/// Desktop Entry Specification version the generated entry follows.
inline constexpr std::string_view kDesktopEntrySpecVersionUVE = "1.5";
inline constexpr std::string_view kMimeDirectoryNameUVE = "mime";
inline constexpr std::string_view kMimePackagesDirectoryNameUVE = "packages";
inline constexpr std::string_view kMimeDefinitionFileNameUVE = "univex-project.xml";
/// Icon theme folders for the application icon and the project file icon.
inline constexpr std::string_view kIconThemeDirectoryUVE = "icons/hicolor/256x256";
inline constexpr std::string_view kApplicationIconContextUVE = "apps";
inline constexpr std::string_view kFileIconContextUVE = "mimetypes";
/// Icon theme name of the Hub's application icon.
inline constexpr std::string_view kHubIconThemeNameUVE = "univex-hub";
inline constexpr std::string_view kIconFileExtensionUVE = ".png";
/// Refresh the desktop's caches after the files change; absent tools are fine.
inline constexpr std::string_view kUpdateMimeDatabaseProgramUVE = "update-mime-database";
inline constexpr std::string_view kUpdateDesktopDatabaseProgramUVE = "update-desktop-database";
#endif
inline constexpr std::string_view kUninstallOptionUVE = "--uninstall";
#if defined(_WIN32)
inline constexpr std::wstring_view kUninstallRegistryKeyUVE =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\UniVexHub";
inline constexpr std::wstring_view kShortcutFileNameUVE = L"UniVex Hub.lnk";
/// Per-user file association: the extension's key names the program identifier's key.
inline constexpr std::wstring_view kClassesRegistryKeyUVE = L"Software\\Classes\\";
#endif

[[nodiscard]] std::filesystem::path ExecutableUVE(const std::filesystem::path& directory, const std::string_view stem) {
    return directory / (std::string(stem) + std::string(Platform::kExecutableSuffixUVE));
}

[[nodiscard]] std::string UniqueSuffixUVE() { return std::to_string(Asset::GenerateAssetGuidUVE().value); }

#if !defined(_WIN32)
/// The per-user data folder desktop entries, media types and icons live under.
[[nodiscard]] std::filesystem::path DesktopDataRootUVE(const std::filesystem::path& entriesRoot) {
    if (!entriesRoot.empty()) {
        return entriesRoot;
    }
    const Platform::ApplicationUserDataDirectoryResultUVE root = Platform::ResolvePerUserDataRootUVE();
    return root.success ? root.directory : std::filesystem::path{};
}

/// The icon theme name of a media type ("application/x-thing" -> "application-x-thing").
[[nodiscard]] std::string MediaTypeIconNameUVE() {
    std::string name(kProjectMediaTypeUVE);
    std::replace(name.begin(), name.end(), '/', '-');
    return name;
}

struct DesktopFilesUVE final {
    std::filesystem::path desktopEntry;
    std::filesystem::path mimeDefinition;
    std::filesystem::path applicationIcon;
    std::filesystem::path fileIcon;
};

[[nodiscard]] DesktopFilesUVE DesktopFilesForUVE(const std::filesystem::path& dataRoot) {
    const std::filesystem::path icons = dataRoot / kIconThemeDirectoryUVE;
    return {dataRoot / kApplicationsDirectoryNameUVE / kDesktopEntryFileNameUVE,
            dataRoot / kMimeDirectoryNameUVE / kMimePackagesDirectoryNameUVE / kMimeDefinitionFileNameUVE,
            icons / kApplicationIconContextUVE / (std::string(kHubIconThemeNameUVE) + std::string(kIconFileExtensionUVE)),
            icons / kFileIconContextUVE / (MediaTypeIconNameUVE() + std::string(kIconFileExtensionUVE))};
}

/// Asks the desktop to re-read media types and applications. Only for the real data folder.
void RefreshDesktopCachesUVE(const std::filesystem::path& dataRoot) {
    static_cast<void>(Platform::LaunchDetachedProcessUVE(
        {std::filesystem::path(kUpdateMimeDatabaseProgramUVE), {(dataRoot / kMimeDirectoryNameUVE).string()}, {}}));
    static_cast<void>(Platform::LaunchDetachedProcessUVE({std::filesystem::path(kUpdateDesktopDatabaseProgramUVE),
                                                          {(dataRoot / kApplicationsDirectoryNameUVE).string()},
                                                          {}}));
}

/// Quotes a path for a desktop entry's Exec key.
[[nodiscard]] std::string QuoteExecArgumentUVE(const std::string& value) {
    std::string quoted = "\"";
    for (const char character : value) {
        if (character == '"' || character == '`' || character == '$' || character == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}
#else
[[nodiscard]] std::wstring WidenUVE(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] std::filesystem::path StartMenuDirectoryUVE(const std::filesystem::path& entriesRoot) {
    if (!entriesRoot.empty()) {
        return entriesRoot;
    }
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) {
        return {};
    }
    std::filesystem::path directory(programs);
    CoTaskMemFree(programs);
    return directory;
}

void SetRegistryStringUVE(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t)));
}
#endif

} // namespace

std::string FormatDisplayVersionUVE(const Core::VersionUVE& version) {
    if (version.build != 0U) {
        return version.ToStringUVE();
    }
    return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
}

std::optional<SetupPayloadLocationUVE> LocateSetupPayloadUVE(const std::filesystem::path& setupExecutable) {
    std::ifstream input(setupExecutable, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(kSetupFooterBytesUVE)) {
        return std::nullopt;
    }
    std::array<char, kSetupFooterBytesUVE> footer{};
    input.seekg(fileSize - static_cast<std::streamoff>(footer.size()));
    input.read(footer.data(), static_cast<std::streamsize>(footer.size()));
    if (!input || std::string_view(footer.data() + kSha256DigestBytesUVE + kSetupPayloadSizeBytesUVE,
                                   kSetupPayloadMagicUVE.size()) != kSetupPayloadMagicUVE) {
        return std::nullopt;
    }
    SetupPayloadLocationUVE location;
    std::memcpy(location.digest.data(), footer.data(), kSha256DigestBytesUVE);
    for (std::size_t index = 0U; index < kSetupPayloadSizeBytesUVE; ++index) {
        location.size |= static_cast<std::uint64_t>(static_cast<unsigned char>(footer[kSha256DigestBytesUVE + index]))
                         << (static_cast<std::size_t>(CHAR_BIT) * index);
    }
    const std::uint64_t available = static_cast<std::uint64_t>(fileSize) - kSetupFooterBytesUVE;
    if (location.size == 0U || location.size > available) {
        return std::nullopt;
    }
    location.offset = available - location.size;
    return location;
}

bool ExtractSetupPayloadUVE(const std::filesystem::path& setupExecutable, const SetupPayloadLocationUVE& location,
                            const std::filesystem::path& archive) {
    std::ifstream input(setupExecutable, std::ios::binary);
    std::ofstream output(archive, std::ios::binary | std::ios::trunc);
    if (!input.is_open() || !output.is_open()) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(location.offset));
    Sha256UVE hash;
    std::vector<char> chunk(kCopyChunkBytesUVE);
    std::uint64_t remaining = location.size;
    while (remaining > 0U) {
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
        input.read(chunk.data(), static_cast<std::streamsize>(take));
        if (static_cast<std::size_t>(input.gcount()) != take) {
            return false;
        }
        hash.UpdateUVE(std::as_bytes(std::span<const char>(chunk.data(), take)));
        output.write(chunk.data(), static_cast<std::streamsize>(take));
        remaining -= take;
    }
    output.close();
    return static_cast<bool>(output) && hash.FinishUVE() == location.digest;
}

std::optional<std::filesystem::path> DefaultHubInstallDirectoryUVE() {
#if defined(_WIN32)
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_UserProgramFiles, KF_FLAG_CREATE, nullptr, &programs))) {
        return std::nullopt;
    }
    std::filesystem::path directory(programs);
    CoTaskMemFree(programs);
    return directory / kHubInstallFolderNameUVE;
#else
    const Platform::ApplicationUserDataDirectoryResultUVE root = Platform::ResolvePerUserDataRootUVE();
    if (!root.success) {
        return std::nullopt;
    }
    return root.directory / kHubInstallFolderNameUVE;
#endif
}

std::filesystem::path HubExecutableInUVE(const std::filesystem::path& installDirectory) {
    return ExecutableUVE(installDirectory, kHubExecutableStemUVE);
}

HubSetupResultUVE InstallHubUVE(const std::filesystem::path& payloadArchive, const std::filesystem::path& installDirectory,
                                const std::filesystem::path& setupExecutable) {
    std::error_code error;
    const std::filesystem::path target = std::filesystem::absolute(installDirectory, error).lexically_normal();
    if (error || target.filename().empty()) {
        return {false, "Choose a folder to install into."};
    }
    if (std::filesystem::exists(target, error) && !std::filesystem::is_directory(target, error)) {
        return {false, target.string() + " is a file, not a folder."};
    }
    std::filesystem::create_directories(target.parent_path(), error);
    const std::filesystem::path staging =
        target.parent_path() / ("." + target.filename().string() + std::string(kStagingSuffixUVE) + UniqueSuffixUVE());
    std::filesystem::create_directories(staging, error);
    if (error) {
        return {false, "Unable to write to " + target.parent_path().string() + ": " + error.message()};
    }
    const auto discard = [&staging] {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const ArchiveExtractResultUVE extracted = ExtractTarGzUVE(payloadArchive, staging);
    if (!extracted.extracted) {
        discard();
        return {false, "The installer's files could not be unpacked: " + extracted.message};
    }
    if (!std::filesystem::is_regular_file(HubExecutableInUVE(staging), error)) {
        discard();
        return {false, "The installer does not contain the Hub."};
    }
    if (!setupExecutable.empty()) {
        std::filesystem::copy_file(setupExecutable, ExecutableUVE(staging, kSetupExecutableStemUVE),
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            discard();
            return {false, "Unable to keep the uninstaller: " + error.message()};
        }
    }

    // Swap: the old installation moves aside, the new one takes its place, then the old one goes.
    const std::filesystem::path previous =
        target.parent_path() / ("." + target.filename().string() + std::string(kPreviousSuffixUVE) + UniqueSuffixUVE());
    const bool upgrading = std::filesystem::exists(target, error);
    if (upgrading) {
        std::filesystem::rename(target, previous, error);
        if (error) {
            discard();
            return {false, "The installed Hub is in use; close it and try again (" + error.message() + ")."};
        }
    }
    std::filesystem::rename(staging, target, error);
    if (error) {
        if (upgrading) {
            std::error_code ignored;
            std::filesystem::rename(previous, target, ignored);
        }
        discard();
        return {false, "Unable to finish installing: " + error.message()};
    }
    if (upgrading) {
        std::filesystem::remove_all(previous, error);
    }
    return {true, upgrading ? "Updated the Hub in " + target.string() + "." : "Installed the Hub in " + target.string() + "."};
}

HubSetupResultUVE RegisterHubWithDesktopUVE(const std::filesystem::path& installDirectory, const std::string& version,
                                            const std::filesystem::path& entriesRoot) {
    const std::filesystem::path hub = HubExecutableInUVE(installDirectory);
#if defined(_WIN32)
    const std::filesystem::path startMenu = StartMenuDirectoryUVE(entriesRoot);
    if (startMenu.empty()) {
        return {false, "The Start menu folder is unavailable."};
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* link = nullptr;
    bool shortcutMade = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&link)))) {
        link->SetPath(hub.c_str());
        link->SetWorkingDirectory(installDirectory.c_str());
        link->SetDescription(WidenUVE(std::string(kHubDescriptionUVE)).c_str());
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            shortcutMade = SUCCEEDED(file->Save((startMenu / kShortcutFileNameUVE).c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    if (!shortcutMade) {
        return {false, "Unable to add the Start menu shortcut."};
    }
    if (entriesRoot.empty()) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallRegistryKeyUVE.data(), 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                            nullptr) == ERROR_SUCCESS) {
            const std::filesystem::path uninstaller = ExecutableUVE(installDirectory, kSetupExecutableStemUVE);
            SetRegistryStringUVE(key, L"DisplayName", WidenUVE(std::string(kHubDisplayNameUVE)));
            SetRegistryStringUVE(key, L"DisplayVersion", WidenUVE(version));
            SetRegistryStringUVE(key, L"Publisher", WidenUVE(std::string(kPublisherUVE)));
            SetRegistryStringUVE(key, L"InstallLocation", installDirectory.wstring());
            SetRegistryStringUVE(key, L"DisplayIcon", hub.wstring());
            SetRegistryStringUVE(key, L"UninstallString",
                                 L"\"" + uninstaller.wstring() + L"\" " + WidenUVE(std::string(kUninstallOptionUVE)));
            RegCloseKey(key);
        }
        const std::wstring classes(kClassesRegistryKeyUVE);
        const std::wstring programId = WidenUVE(std::string(kProjectProgramIdUVE));
        const std::filesystem::path icon = installDirectory / kHubIconsDirectoryNameUVE / kHubWindowsIconFileNameUVE;
        const std::array<std::pair<std::wstring, std::wstring>, 4U> values{{
            {classes + WidenUVE(std::string(Platform::kEditorProjectFileExtensionUVE)), programId},
            {classes + programId, WidenUVE(std::string(kProjectTypeDisplayNameUVE))},
            {classes + programId + L"\\DefaultIcon", L"\"" + icon.wstring() + L"\",0"},
            {classes + programId + L"\\shell\\open\\command", L"\"" + hub.wstring() + L"\" \"%1\""},
        }};
        for (const auto& [path, value] : values) {
            HKEY entry = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &entry, nullptr) ==
                ERROR_SUCCESS) {
                SetRegistryStringUVE(entry, nullptr, value);
                RegCloseKey(entry);
            }
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    return {true, "Added UniVex Hub to the Start menu."};
#else
    const std::filesystem::path dataRoot = DesktopDataRootUVE(entriesRoot);
    if (dataRoot.empty()) {
        return {false, "The applications folder is unavailable."};
    }
    const DesktopFilesUVE files = DesktopFilesForUVE(dataRoot);
    std::error_code error;
    for (const std::filesystem::path* const file :
         {&files.desktopEntry, &files.mimeDefinition, &files.applicationIcon, &files.fileIcon}) {
        std::filesystem::create_directories(file->parent_path(), error);
    }
    const std::filesystem::path icon = installDirectory / kHubIconsDirectoryNameUVE / kHubIconFileNameUVE;
    if (std::filesystem::is_regular_file(icon, error)) {
        for (const std::filesystem::path* const target : {&files.applicationIcon, &files.fileIcon}) {
            std::filesystem::copy_file(icon, *target, std::filesystem::copy_options::overwrite_existing, error);
            error.clear();
        }
    }
    const std::filesystem::path uninstaller = ExecutableUVE(installDirectory, kSetupExecutableStemUVE);
    const bool hasUninstaller = std::filesystem::is_regular_file(uninstaller, error);
    {
        std::ofstream entry(files.desktopEntry, std::ios::trunc);
        entry << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=" << kHubDisplayNameUVE << "\n"
              << "Comment=" << kHubDescriptionUVE << "\n"
              << "Version=" << kDesktopEntrySpecVersionUVE << "\n"
              << "X-UniVex-Version=" << version << "\n"
              << "Exec=" << QuoteExecArgumentUVE(hub.string()) << " %f\n"
              << "Path=" << installDirectory.string() << "\n"
              << "Icon=" << kHubIconThemeNameUVE << "\n"
              << "StartupWMClass=" << kHubDisplayNameUVE << "\n"
              << "MimeType=" << kProjectMediaTypeUVE << ";\n"
              << "Terminal=false\n"
              << "Categories=" << kDesktopEntryCategoriesUVE << "\n";
        if (hasUninstaller) {
            entry << "Actions=Uninstall;\n\n"
                  << "[Desktop Action Uninstall]\n"
                  << "Name=Uninstall " << kHubDisplayNameUVE << "\n"
                  << "Exec=" << QuoteExecArgumentUVE(uninstaller.string()) << " " << kUninstallOptionUVE << "\n";
        }
        if (!entry) {
            return {false, "Unable to add UniVex Hub to the applications menu."};
        }
    }
    {
        std::ofstream mime(files.mimeDefinition, std::ios::trunc);
        mime << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
             << "  <mime-type type=\"" << kProjectMediaTypeUVE << "\">\n"
             << "    <comment>" << kProjectTypeDisplayNameUVE << "</comment>\n"
             << "    <icon name=\"" << MediaTypeIconNameUVE() << "\"/>\n"
             << "    <glob pattern=\"*" << Platform::kEditorProjectFileExtensionUVE << "\"/>\n"
             << "  </mime-type>\n"
             << "</mime-info>\n";
        if (!mime) {
            return {false, "Unable to register UniVex project files."};
        }
    }
    if (entriesRoot.empty()) {
        RefreshDesktopCachesUVE(dataRoot);
    }
    return {true, "Added UniVex Hub to the applications menu; project files open with it."};
#endif
}

HubSetupResultUVE UninstallHubUVE(const std::filesystem::path& installDirectory, const std::filesystem::path& entriesRoot) {
    std::error_code error;
#if defined(_WIN32)
    const std::filesystem::path startMenu = StartMenuDirectoryUVE(entriesRoot);
    if (!startMenu.empty()) {
        std::filesystem::remove(startMenu / kShortcutFileNameUVE, error);
    }
    if (entriesRoot.empty()) {
        RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallRegistryKeyUVE.data());
        const std::wstring classes(kClassesRegistryKeyUVE);
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + WidenUVE(std::string(kProjectProgramIdUVE))).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + WidenUVE(std::string(Platform::kEditorProjectFileExtensionUVE))).c_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
#else
    const std::filesystem::path dataRoot = DesktopDataRootUVE(entriesRoot);
    if (!dataRoot.empty()) {
        const DesktopFilesUVE files = DesktopFilesForUVE(dataRoot);
        for (const std::filesystem::path* const file :
             {&files.desktopEntry, &files.mimeDefinition, &files.applicationIcon, &files.fileIcon}) {
            std::filesystem::remove(*file, error);
        }
        if (entriesRoot.empty()) {
            RefreshDesktopCachesUVE(dataRoot);
        }
    }
#endif
    if (!std::filesystem::is_regular_file(HubExecutableInUVE(installDirectory), error)) {
        return {false, installDirectory.string() + " does not hold a Hub installation."};
    }
    std::filesystem::remove_all(installDirectory, error);
    if (error) {
        return {false, "Unable to remove " + installDirectory.string() + ": " + error.message()};
    }
    return {true, "UniVex Hub was removed. Your projects and downloaded engines were kept."};
}

} // namespace UVE::Online
