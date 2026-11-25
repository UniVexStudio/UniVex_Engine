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

inline constexpr std::size_t kCopyChunkBytes = 1U << 16U;
inline constexpr std::string_view kStagingSuffix = ".installing";
inline constexpr std::string_view kPreviousSuffix = ".previous";
#if !defined(_WIN32)
// freedesktop.org locations under the per-user data folder.
inline constexpr std::string_view kApplicationsDirectoryName = "applications";
inline constexpr std::string_view kDesktopEntryFileName = "univex-hub.desktop";
inline constexpr std::string_view kDesktopEntryCategories = "Development;IDE;";
/// Desktop Entry Specification version the generated entry follows.
inline constexpr std::string_view kDesktopEntrySpecVersion = "1.5";
inline constexpr std::string_view kMimeDirectoryName = "mime";
inline constexpr std::string_view kMimePackagesDirectoryName = "packages";
inline constexpr std::string_view kMimeDefinitionFileName = "univex-project.xml";
/// Icon theme folders for the application icon and the project file icon.
inline constexpr std::string_view kIconThemeDirectory = "icons/hicolor/256x256";
inline constexpr std::string_view kApplicationIconContext = "apps";
inline constexpr std::string_view kFileIconContext = "mimetypes";
/// Icon theme name of the Hub's application icon.
inline constexpr std::string_view kHubIconThemeName = "univex-hub";
inline constexpr std::string_view kIconFileExtension = ".png";
/// Refresh the desktop's caches after the files change; absent tools are fine.
inline constexpr std::string_view kUpdateMimeDatabaseProgram = "update-mime-database";
inline constexpr std::string_view kUpdateDesktopDatabaseProgram = "update-desktop-database";
#endif
inline constexpr std::string_view kUninstallOption = "--uninstall";
#if defined(_WIN32)
inline constexpr std::wstring_view kUninstallRegistryKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\UniVexHub";
inline constexpr std::wstring_view kShortcutFileName = L"UniVex Hub.lnk";
/// Per-user file association: the extension's key names the program identifier's key.
inline constexpr std::wstring_view kClassesRegistryKey = L"Software\\Classes\\";
#endif

[[nodiscard]] std::filesystem::path Executable(const std::filesystem::path& directory, const std::string_view stem) {
    return directory / (std::string(stem) + std::string(Platform::kExecutableSuffix));
}

[[nodiscard]] std::string UniqueSuffix() { return std::to_string(Asset::GenerateAssetGuid().value); }

#if !defined(_WIN32)
/// The per-user data folder desktop entries, media types and icons live under.
[[nodiscard]] std::filesystem::path DesktopDataRoot(const std::filesystem::path& entriesRoot) {
    if (!entriesRoot.empty()) {
        return entriesRoot;
    }
    const Platform::ApplicationUserDataDirectoryResult root = Platform::ResolvePerUserDataRoot();
    return root.success ? root.directory : std::filesystem::path{};
}

/// The icon theme name of a media type ("application/x-thing" -> "application-x-thing").
[[nodiscard]] std::string MediaTypeIconName() {
    std::string name(kProjectMediaType);
    std::replace(name.begin(), name.end(), '/', '-');
    return name;
}

struct DesktopFiles final {
    std::filesystem::path desktopEntry;
    std::filesystem::path mimeDefinition;
    std::filesystem::path applicationIcon;
    std::filesystem::path fileIcon;
};

[[nodiscard]] DesktopFiles DesktopFilesFor(const std::filesystem::path& dataRoot) {
    const std::filesystem::path icons = dataRoot / kIconThemeDirectory;
    return {dataRoot / kApplicationsDirectoryName / kDesktopEntryFileName,
            dataRoot / kMimeDirectoryName / kMimePackagesDirectoryName / kMimeDefinitionFileName,
            icons / kApplicationIconContext / (std::string(kHubIconThemeName) + std::string(kIconFileExtension)),
            icons / kFileIconContext / (MediaTypeIconName() + std::string(kIconFileExtension))};
}

/// Asks the desktop to re-read media types and applications. Only for the real data folder.
void RefreshDesktopCaches(const std::filesystem::path& dataRoot) {
    static_cast<void>(Platform::LaunchDetachedProcess(
        {std::filesystem::path(kUpdateMimeDatabaseProgram), {(dataRoot / kMimeDirectoryName).string()}, {}}));
    static_cast<void>(Platform::LaunchDetachedProcess({std::filesystem::path(kUpdateDesktopDatabaseProgram),
                                                          {(dataRoot / kApplicationsDirectoryName).string()},
                                                          {}}));
}

/// Quotes a path for a desktop entry's Exec key.
[[nodiscard]] std::string QuoteExecArgument(const std::string& value) {
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
[[nodiscard]] std::wstring Widen(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] std::filesystem::path StartMenuDirectory(const std::filesystem::path& entriesRoot) {
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

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1U) * sizeof(wchar_t)));
}
#endif

} // namespace

std::string FormatDisplayVersion(const Core::Version& version) {
    if (version.build != 0U) {
        return version.ToString();
    }
    return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
}

std::optional<SetupPayloadLocation> LocateSetupPayload(const std::filesystem::path& setupExecutable) {
    std::ifstream input(setupExecutable, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(kSetupFooterBytes)) {
        return std::nullopt;
    }
    std::array<char, kSetupFooterBytes> footer{};
    input.seekg(fileSize - static_cast<std::streamoff>(footer.size()));
    input.read(footer.data(), static_cast<std::streamsize>(footer.size()));
    if (!input || std::string_view(footer.data() + kSha256DigestBytes + kSetupPayloadSizeBytes,
                                   kSetupPayloadMagic.size()) != kSetupPayloadMagic) {
        return std::nullopt;
    }
    SetupPayloadLocation location;
    std::memcpy(location.digest.data(), footer.data(), kSha256DigestBytes);
    for (std::size_t index = 0U; index < kSetupPayloadSizeBytes; ++index) {
        location.size |= static_cast<std::uint64_t>(static_cast<unsigned char>(footer[kSha256DigestBytes + index]))
                         << (static_cast<std::size_t>(CHAR_BIT) * index);
    }
    const std::uint64_t available = static_cast<std::uint64_t>(fileSize) - kSetupFooterBytes;
    if (location.size == 0U || location.size > available) {
        return std::nullopt;
    }
    location.offset = available - location.size;
    return location;
}

bool ExtractSetupPayload(const std::filesystem::path& setupExecutable, const SetupPayloadLocation& location,
                            const std::filesystem::path& archive) {
    std::ifstream input(setupExecutable, std::ios::binary);
    std::ofstream output(archive, std::ios::binary | std::ios::trunc);
    if (!input.is_open() || !output.is_open()) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(location.offset));
    Sha256 hash;
    std::vector<char> chunk(kCopyChunkBytes);
    std::uint64_t remaining = location.size;
    while (remaining > 0U) {
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
        input.read(chunk.data(), static_cast<std::streamsize>(take));
        if (static_cast<std::size_t>(input.gcount()) != take) {
            return false;
        }
        hash.Update(std::as_bytes(std::span<const char>(chunk.data(), take)));
        output.write(chunk.data(), static_cast<std::streamsize>(take));
        remaining -= take;
    }
    output.close();
    return static_cast<bool>(output) && hash.Finish() == location.digest;
}

std::optional<std::filesystem::path> DefaultHubInstallDirectory() {
#if defined(_WIN32)
    PWSTR programs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_UserProgramFiles, KF_FLAG_CREATE, nullptr, &programs))) {
        return std::nullopt;
    }
    std::filesystem::path directory(programs);
    CoTaskMemFree(programs);
    return directory / kHubInstallFolderName;
#else
    const Platform::ApplicationUserDataDirectoryResult root = Platform::ResolvePerUserDataRoot();
    if (!root.success) {
        return std::nullopt;
    }
    return root.directory / kHubInstallFolderName;
#endif
}

std::filesystem::path HubExecutableIn(const std::filesystem::path& installDirectory) {
    return Executable(installDirectory, kHubExecutableStem);
}

HubSetupResult InstallHub(const std::filesystem::path& payloadArchive, const std::filesystem::path& installDirectory,
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
        target.parent_path() / ("." + target.filename().string() + std::string(kStagingSuffix) + UniqueSuffix());
    std::filesystem::create_directories(staging, error);
    if (error) {
        return {false, "Unable to write to " + target.parent_path().string() + ": " + error.message()};
    }
    const auto discard = [&staging] {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    };
    const ArchiveExtractResult extracted = ExtractTarGz(payloadArchive, staging);
    if (!extracted.extracted) {
        discard();
        return {false, "The installer's files could not be unpacked: " + extracted.message};
    }
    if (!std::filesystem::is_regular_file(HubExecutableIn(staging), error)) {
        discard();
        return {false, "The installer does not contain the Hub."};
    }
    if (!setupExecutable.empty()) {
        std::filesystem::copy_file(setupExecutable, Executable(staging, kSetupExecutableStem),
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            discard();
            return {false, "Unable to keep the uninstaller: " + error.message()};
        }
    }

    // Swap: the old installation moves aside, the new one takes its place, then the old one goes.
    const std::filesystem::path previous =
        target.parent_path() / ("." + target.filename().string() + std::string(kPreviousSuffix) + UniqueSuffix());
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

HubSetupResult RegisterHubWithDesktop(const std::filesystem::path& installDirectory, const std::string& version,
                                            const std::filesystem::path& entriesRoot) {
    const std::filesystem::path hub = HubExecutableIn(installDirectory);
#if defined(_WIN32)
    const std::filesystem::path startMenu = StartMenuDirectory(entriesRoot);
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
        link->SetDescription(Widen(std::string(kHubDescription)).c_str());
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            shortcutMade = SUCCEEDED(file->Save((startMenu / kShortcutFileName).c_str(), TRUE));
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
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallRegistryKey.data(), 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                            nullptr) == ERROR_SUCCESS) {
            const std::filesystem::path uninstaller = Executable(installDirectory, kSetupExecutableStem);
            SetRegistryString(key, L"DisplayName", Widen(std::string(kHubDisplayName)));
            SetRegistryString(key, L"DisplayVersion", Widen(version));
            SetRegistryString(key, L"Publisher", Widen(std::string(kPublisher)));
            SetRegistryString(key, L"InstallLocation", installDirectory.wstring());
            SetRegistryString(key, L"DisplayIcon", hub.wstring());
            SetRegistryString(key, L"UninstallString",
                                 L"\"" + uninstaller.wstring() + L"\" " + Widen(std::string(kUninstallOption)));
            RegCloseKey(key);
        }
        const std::wstring classes(kClassesRegistryKey);
        const std::wstring programId = Widen(std::string(kProjectProgramId));
        const std::filesystem::path icon = installDirectory / kHubIconsDirectoryName / kHubWindowsIconFileName;
        const std::array<std::pair<std::wstring, std::wstring>, 4U> values{{
            {classes + Widen(std::string(Platform::kEditorProjectFileExtension)), programId},
            {classes + programId, Widen(std::string(kProjectTypeDisplayName))},
            {classes + programId + L"\\DefaultIcon", L"\"" + icon.wstring() + L"\",0"},
            {classes + programId + L"\\shell\\open\\command", L"\"" + hub.wstring() + L"\" \"%1\""},
        }};
        for (const auto& [path, value] : values) {
            HKEY entry = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &entry, nullptr) ==
                ERROR_SUCCESS) {
                SetRegistryString(entry, nullptr, value);
                RegCloseKey(entry);
            }
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    return {true, "Added UniVex Hub to the Start menu."};
#else
    const std::filesystem::path dataRoot = DesktopDataRoot(entriesRoot);
    if (dataRoot.empty()) {
        return {false, "The applications folder is unavailable."};
    }
    const DesktopFiles files = DesktopFilesFor(dataRoot);
    std::error_code error;
    for (const std::filesystem::path* const file :
         {&files.desktopEntry, &files.mimeDefinition, &files.applicationIcon, &files.fileIcon}) {
        std::filesystem::create_directories(file->parent_path(), error);
    }
    const std::filesystem::path icon = installDirectory / kHubIconsDirectoryName / kHubIconFileName;
    if (std::filesystem::is_regular_file(icon, error)) {
        for (const std::filesystem::path* const target : {&files.applicationIcon, &files.fileIcon}) {
            std::filesystem::copy_file(icon, *target, std::filesystem::copy_options::overwrite_existing, error);
            error.clear();
        }
    }
    const std::filesystem::path uninstaller = Executable(installDirectory, kSetupExecutableStem);
    const bool hasUninstaller = std::filesystem::is_regular_file(uninstaller, error);
    {
        std::ofstream entry(files.desktopEntry, std::ios::trunc);
        entry << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=" << kHubDisplayName << "\n"
              << "Comment=" << kHubDescription << "\n"
              << "Version=" << kDesktopEntrySpecVersion << "\n"
              << "X-UniVex-Version=" << version << "\n"
              << "Exec=" << QuoteExecArgument(hub.string()) << " %f\n"
              << "Path=" << installDirectory.string() << "\n"
              << "Icon=" << kHubIconThemeName << "\n"
              << "StartupWMClass=" << kHubDisplayName << "\n"
              << "MimeType=" << kProjectMediaType << ";\n"
              << "Terminal=false\n"
              << "Categories=" << kDesktopEntryCategories << "\n";
        if (hasUninstaller) {
            entry << "Actions=Uninstall;\n\n"
                  << "[Desktop Action Uninstall]\n"
                  << "Name=Uninstall " << kHubDisplayName << "\n"
                  << "Exec=" << QuoteExecArgument(uninstaller.string()) << " " << kUninstallOption << "\n";
        }
        if (!entry) {
            return {false, "Unable to add UniVex Hub to the applications menu."};
        }
    }
    {
        std::ofstream mime(files.mimeDefinition, std::ios::trunc);
        mime << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
             << "  <mime-type type=\"" << kProjectMediaType << "\">\n"
             << "    <comment>" << kProjectTypeDisplayName << "</comment>\n"
             << "    <icon name=\"" << MediaTypeIconName() << "\"/>\n"
             << "    <glob pattern=\"*" << Platform::kEditorProjectFileExtension << "\"/>\n"
             << "  </mime-type>\n"
             << "</mime-info>\n";
        if (!mime) {
            return {false, "Unable to register UniVex project files."};
        }
    }
    if (entriesRoot.empty()) {
        RefreshDesktopCaches(dataRoot);
    }
    return {true, "Added UniVex Hub to the applications menu; project files open with it."};
#endif
}

HubSetupResult UninstallHub(const std::filesystem::path& installDirectory, const std::filesystem::path& entriesRoot) {
    std::error_code error;
#if defined(_WIN32)
    const std::filesystem::path startMenu = StartMenuDirectory(entriesRoot);
    if (!startMenu.empty()) {
        std::filesystem::remove(startMenu / kShortcutFileName, error);
    }
    if (entriesRoot.empty()) {
        RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallRegistryKey.data());
        const std::wstring classes(kClassesRegistryKey);
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + Widen(std::string(kProjectProgramId))).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (classes + Widen(std::string(Platform::kEditorProjectFileExtension))).c_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
#else
    const std::filesystem::path dataRoot = DesktopDataRoot(entriesRoot);
    if (!dataRoot.empty()) {
        const DesktopFiles files = DesktopFilesFor(dataRoot);
        for (const std::filesystem::path* const file :
             {&files.desktopEntry, &files.mimeDefinition, &files.applicationIcon, &files.fileIcon}) {
            std::filesystem::remove(*file, error);
        }
        if (entriesRoot.empty()) {
            RefreshDesktopCaches(dataRoot);
        }
    }
#endif
    if (!std::filesystem::is_regular_file(HubExecutableIn(installDirectory), error)) {
        return {false, installDirectory.string() + " does not hold a Hub installation."};
    }
    std::filesystem::remove_all(installDirectory, error);
    if (error) {
        return {false, "Unable to remove " + installDirectory.string() + ": " + error.message()};
    }
    return {true, "UniVex Hub was removed. Your projects and downloaded engines were kept."};
}

} // namespace UVE::Online
