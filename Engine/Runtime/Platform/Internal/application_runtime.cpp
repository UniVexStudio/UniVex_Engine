// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "application_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace UVE::Platform {
namespace {

[[nodiscard]] std::uint64_t HashIdentifier(const std::string_view identifier) noexcept {
    // FNV-1a is stable across processes and standard-library implementations, unlike std::hash.
    // Kept local deliberately: uve_platform sits below uve_utilities, so including
    // <uve/utilities/hash_uve.h> here would be a layer inversion (and a CMake target cycle).
    // Same algorithm and values as Utilities::HashString — keep in sync.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char rawByte : identifier) {
        const unsigned char byte = static_cast<unsigned char>(rawByte);
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string HashIdentifierHex(const std::string_view identifier) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << HashIdentifier(identifier);
    return output.str();
}

[[nodiscard]] std::string SanitizeDirectoryName(const std::string& text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (const char rawCharacter : text) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_') {
            sanitized.push_back(static_cast<char>(character));
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

[[nodiscard]] bool IsSafeUserDataDirectoryName(const std::string_view directoryName) noexcept {
    if (directoryName.empty() || directoryName == "." || directoryName == ".." || directoryName.size() > 64U) {
        return false;
    }
    for (const char rawCharacter : directoryName) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_') {
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] ApplicationUserDataDirectoryResult MakeDirectoryFailure(std::string message) {
    return {false, {}, std::move(message)};
}

} // namespace

std::string_view GetApplicationTarget() noexcept {
#if defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "web";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    return "ios";
#else
    return "macos";
#endif
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

std::string_view GetEnginePackageTarget() noexcept {
#if defined(__ANDROID__)
#if defined(__aarch64__)
    return "android-arm64-v8a";
#elif defined(__x86_64__)
    return "android-x86_64";
#elif defined(__arm__)
    return "android-armeabi-v7a";
#elif defined(__i386__)
    return "android-x86";
#else
    return "android";
#endif
#else
    return GetApplicationTarget();
#endif
}

bool IsCompatibleEnginePackageTarget(const std::string_view manifestTarget) noexcept {
    if (manifestTarget == GetEnginePackageTarget()) {
        return true;
    }
#if defined(__ANDROID__) && defined(__aarch64__)
    // Earlier Android packages were always arm64-v8a and wrote bare "android" in engine.json.
    if (manifestTarget == "android") {
        return true;
    }
#endif
    return false;
}

void AttachParentConsoleForCli() noexcept {
#if defined(_WIN32)
    const auto isRedirected = [](const DWORD stdHandleId) noexcept {
        const HANDLE handle = GetStdHandle(stdHandleId);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        const DWORD type = GetFileType(handle);
        return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
    };
    const bool outRedirected = isRedirected(STD_OUTPUT_HANDLE);
    const bool errRedirected = isRedirected(STD_ERROR_HANDLE);
    const bool inRedirected = isRedirected(STD_INPUT_HANDLE);
    if (outRedirected && errRedirected) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return;
    }
    if (!outRedirected) {
        static_cast<void>(std::freopen("CONOUT$", "w", stdout));
    }
    if (!errRedirected) {
        static_cast<void>(std::freopen("CONOUT$", "w", stderr));
    }
    if (!inRedirected) {
        static_cast<void>(std::freopen("CONIN$", "r", stdin));
    }
#endif
}

ApplicationUserDataDirectoryResult ResolvePerUserDataRoot() {
#if defined(_WIN32)
    const char* const localAppData = std::getenv("LOCALAPPDATA");
    const char* const roamingAppData = std::getenv("APPDATA");
    const char* const home = std::getenv("USERPROFILE");
    const char* const selected = localAppData != nullptr && localAppData[0] != '\0'
                                     ? localAppData
                                     : (roamingAppData != nullptr && roamingAppData[0] != '\0'
                                            ? roamingAppData
                                            : home);
    if (selected == nullptr || selected[0] == '\0') {
        return MakeDirectoryFailure("No per-user Windows application-data directory is available.");
    }
    return {true, std::filesystem::path(selected), "Per-user data root resolved."};
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0' || !std::filesystem::path(home).is_absolute()) {
        return MakeDirectoryFailure("HOME is unavailable for the Apple application-data directory.");
    }
    return {true, std::filesystem::path(home) / "Library" / "Application Support", "Per-user data root resolved."};
#elif defined(__ANDROID__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0' || !std::filesystem::path(home).is_absolute()) {
        return MakeDirectoryFailure("An app-sandbox HOME is required for Android application data.");
    }
    return {true, std::filesystem::path(home), "Per-user data root resolved."};
#else
    const char* const xdgDataHome = std::getenv("XDG_DATA_HOME");
    const char* const home = std::getenv("HOME");
    if (xdgDataHome != nullptr && xdgDataHome[0] != '\0' && std::filesystem::path(xdgDataHome).is_absolute()) {
        return {true, std::filesystem::path(xdgDataHome), "Per-user data root resolved."};
    } else if (home != nullptr && home[0] != '\0') {
        return {true, std::filesystem::path(home) / ".local" / "share", "Per-user data root resolved."};
    } else {
        return MakeDirectoryFailure("Neither XDG_DATA_HOME nor HOME is available for application data.");
    }
#endif
}

ApplicationUserDataDirectoryResult ResolveUserHomeDirectory() {
#if defined(_WIN32)
    const char* const home = std::getenv("USERPROFILE");
#else
    const char* const home = std::getenv("HOME");
#endif
    if (home == nullptr || home[0] == '\0' || !std::filesystem::path(home).is_absolute()) {
        return MakeDirectoryFailure("The user's home directory is unavailable.");
    }
    return {true, std::filesystem::path(home), "Home directory resolved."};
}

namespace {

#if defined(_WIN32)
[[nodiscard]] std::filesystem::path ResolveWindowsKnownFolder(const GUID& folderId) {
    using ShGetKnownFolderPathFn = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
    using CoTaskMemFreeFn = void(WINAPI*)(LPVOID);
    HMODULE const shell32 = LoadLibraryW(L"shell32.dll");
    HMODULE const ole32 = LoadLibraryW(L"ole32.dll");
    std::filesystem::path result;
    if (shell32 != nullptr && ole32 != nullptr) {
        const auto getKnown =
            reinterpret_cast<ShGetKnownFolderPathFn>(GetProcAddress(shell32, "SHGetKnownFolderPath"));
        const auto freeMem = reinterpret_cast<CoTaskMemFreeFn>(GetProcAddress(ole32, "CoTaskMemFree"));
        if (getKnown != nullptr && freeMem != nullptr) {
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(getKnown(folderId, 0, nullptr, &rawPath)) && rawPath != nullptr) {
                result = std::filesystem::path(rawPath);
                freeMem(rawPath);
            }
        }
    }
    if (ole32 != nullptr) {
        FreeLibrary(ole32);
    }
    if (shell32 != nullptr) {
        FreeLibrary(shell32);
    }
    return result;
}
#elif !defined(__APPLE__) && !defined(__ANDROID__)
[[nodiscard]] std::filesystem::path ParseXdgUserDir(const std::filesystem::path& home,
                                                       const std::string_view key) {
    const std::filesystem::path configFile = home / ".config" / "user-dirs.dirs";
    std::ifstream input(configFile);
    if (!input.is_open()) {
        return {};
    }
    std::string line;
    const std::string prefix = std::string(key) + "=";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0U) != 0U) {
            continue;
        }
        std::string value = line.substr(prefix.size());
        if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
            value = value.substr(1U, value.size() - 2U);
        }
        if (value.rfind("$HOME/", 0U) == 0U) {
            return home / value.substr(6U);
        }
        if (value == "$HOME") {
            return home;
        }
        const std::filesystem::path parsed(value);
        if (parsed.is_absolute()) {
            return parsed;
        }
    }
    return {};
}
#endif

} // namespace

ApplicationUserDataDirectoryResult ResolveUserDocumentsDirectory() {
#if defined(_WIN32)
    // FOLDERID_Documents: {FDD39AD0-238F-46AF-ADB4-6C85480369C7}
    static constexpr GUID kFolderIdDocuments = {
        0xFDD39AD0UL, 0x238FU, 0x46AFU, {0xADU, 0xB4U, 0x6CU, 0x85U, 0x48U, 0x03U, 0x69U, 0xC7U}};
    const std::filesystem::path known = ResolveWindowsKnownFolder(kFolderIdDocuments);
    if (!known.empty() && known.is_absolute()) {
        return {true, known, "Windows Documents directory resolved."};
    }
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (home.success) {
        return {true, home.directory / "Documents", "Windows Documents directory resolved."};
    }
    return MakeDirectoryFailure("The Windows Documents directory is unavailable.");
#elif defined(__ANDROID__)
    const char* const publicDocs = std::getenv("UNIVEX_ANDROID_PUBLIC_DOCS");
    const char* const allFiles = std::getenv("UNIVEX_ANDROID_ALL_FILES");
    const char* const appExtDocs = std::getenv("UNIVEX_ANDROID_APP_EXT_DOCS");
    const bool hasAllFiles = allFiles != nullptr && allFiles[0] == '1';
    if (publicDocs != nullptr && publicDocs[0] != '\0' && hasAllFiles) {
        return {true, std::filesystem::path(publicDocs), "Android public Documents directory resolved."};
    }
    if (publicDocs != nullptr && publicDocs[0] != '\0') {
        std::error_code probeError;
        const std::filesystem::path candidate(publicDocs);
        if (std::filesystem::is_directory(candidate, probeError)) {
            const std::filesystem::path testDir = candidate / "UniVex Projects";
            std::filesystem::create_directories(testDir, probeError);
            if (!probeError && std::filesystem::is_directory(testDir, probeError)) {
                return {true, candidate, "Android public Documents directory resolved."};
            }
        }
    }
    if (appExtDocs != nullptr && appExtDocs[0] != '\0') {
        return {true, std::filesystem::path(appExtDocs), "Android external app Documents directory resolved."};
    }
    return ResolveUserHomeDirectory();
#elif defined(__APPLE__)
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (home.success) {
        return {true, home.directory / "Documents", "Apple Documents directory resolved."};
    }
    return MakeDirectoryFailure("The user's Documents directory is unavailable.");
#else
    const char* const xdgDocs = std::getenv("XDG_DOCUMENTS_DIR");
    if (xdgDocs != nullptr && xdgDocs[0] != '\0' && std::filesystem::path(xdgDocs).is_absolute()) {
        return {true, std::filesystem::path(xdgDocs), "XDG Documents directory resolved."};
    }
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (!home.success) {
        return MakeDirectoryFailure("The user's Documents directory is unavailable.");
    }
    const std::filesystem::path configured = ParseXdgUserDir(home.directory, "XDG_DOCUMENTS_DIR");
    if (!configured.empty()) {
        return {true, configured, "XDG Documents directory resolved."};
    }
    return {true, home.directory / "Documents", "Documents directory resolved."};
#endif
}

ApplicationUserDataDirectoryResult ResolveUserDownloadsDirectory() {
#if defined(_WIN32)
    // FOLDERID_Downloads: {374DE290-123F-4565-9164-39C4925E467B}
    static constexpr GUID kFolderIdDownloads = {
        0x374DE290UL, 0x123FU, 0x4565U, {0x91U, 0x64U, 0x39U, 0xC4U, 0x92U, 0x5EU, 0x46U, 0x7BU}};
    const std::filesystem::path known = ResolveWindowsKnownFolder(kFolderIdDownloads);
    if (!known.empty() && known.is_absolute()) {
        return {true, known, "Windows Downloads directory resolved."};
    }
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (home.success) {
        return {true, home.directory / "Downloads", "Windows Downloads directory resolved."};
    }
    return MakeDirectoryFailure("The Downloads directory is unavailable.");
#elif defined(__ANDROID__)
    const char* const downloads = std::getenv("UNIVEX_ANDROID_DOWNLOADS");
    if (downloads != nullptr && downloads[0] != '\0') {
        return {true, std::filesystem::path(downloads), "Android Downloads directory resolved."};
    }
    return {true, std::filesystem::path("/storage/emulated/0/Download"), "Android Downloads directory resolved."};
#elif defined(__APPLE__)
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (home.success) {
        return {true, home.directory / "Downloads", "Apple Downloads directory resolved."};
    }
    return MakeDirectoryFailure("The Downloads directory is unavailable.");
#else
    const char* const xdgDownloads = std::getenv("XDG_DOWNLOAD_DIR");
    if (xdgDownloads != nullptr && xdgDownloads[0] != '\0' && std::filesystem::path(xdgDownloads).is_absolute()) {
        return {true, std::filesystem::path(xdgDownloads), "XDG Downloads directory resolved."};
    }
    const ApplicationUserDataDirectoryResult home = ResolveUserHomeDirectory();
    if (!home.success) {
        return MakeDirectoryFailure("The Downloads directory is unavailable.");
    }
    const std::filesystem::path configured = ParseXdgUserDir(home.directory, "XDG_DOWNLOAD_DIR");
    if (!configured.empty()) {
        return {true, configured, "XDG Downloads directory resolved."};
    }
    return {true, home.directory / "Downloads", "Downloads directory resolved."};
#endif
}

ApplicationUserDataDirectoryResult ResolvePublicStorageRoot() {
#if defined(__ANDROID__)
    const char* const root = std::getenv("UNIVEX_ANDROID_STORAGE_ROOT");
    if (root != nullptr && root[0] != '\0') {
        return {true, std::filesystem::path(root), "Android shared storage root resolved."};
    }
    return {true, std::filesystem::path("/storage/emulated/0"), "Android shared storage root resolved."};
#else
    return ResolveUserHomeDirectory();
#endif
}

ApplicationUserDataDirectoryResult ResolveToolUserDataDirectory(const std::string_view directoryName) {
    const std::filesystem::path name{directoryName};
    if (name.empty() || name.is_absolute() || name.has_parent_path() || name == "." || name == "..") {
        return MakeDirectoryFailure("A tool data directory name must be a single path component.");
    }
    ApplicationUserDataDirectoryResult root = ResolvePerUserDataRoot();
    if (!root.success) {
        return root;
    }
    const std::filesystem::path directory = root.directory / name;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory, error)) {
        return MakeDirectoryFailure("Unable to create the tool data directory '" + directory.string() + "'.");
    }
    return {true, directory, "Tool data directory resolved."};
}

ApplicationUserDataDirectoryResult ResolveApplicationUserDataDirectory(
    const EditorProjectPackage& package, const std::filesystem::path& projectRoot) {
    std::string directoryName = package.applicationSettings.userDataDirectoryName;
    if (directoryName.empty()) {
        const std::string& fallback = package.productMetadata.shortName.empty()
                                          ? package.projectId
                                          : package.productMetadata.shortName;
        directoryName = SanitizeDirectoryName(fallback);
    } else if (!IsSafeUserDataDirectoryName(directoryName)) {
        return MakeDirectoryFailure("The configured user-data directory name is not a safe single path segment.");
    }
    if (directoryName.empty()) {
        directoryName = "univex-project-" + HashIdentifierHex(package.projectId).substr(0U, 8U);
    }

    std::error_code error;
    const std::filesystem::path absoluteProjectRoot = std::filesystem::absolute(
        projectRoot.empty() ? std::filesystem::current_path(error) : projectRoot, error);
    if (error) {
        return MakeDirectoryFailure("Unable to resolve the project root for application data: " + error.message());
    }

    std::filesystem::path baseDirectory;
    if (package.applicationSettings.portableUserData) {
        baseDirectory = absoluteProjectRoot;
    } else {
        ApplicationUserDataDirectoryResult root = ResolvePerUserDataRoot();
        if (!root.success) {
            return root;
        }
        baseDirectory = std::move(root.directory);
    }

    const std::filesystem::path directory = baseDirectory / directoryName;
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error) {
        return MakeDirectoryFailure("Unable to create the application user-data directory '" +
                                       directory.string() + "': " + error.message());
    }
    error.clear();
    if (!std::filesystem::is_directory(directory, error) || error) {
        return MakeDirectoryFailure("The application user-data path is not a directory: " + directory.string());
    }
    return {true, directory, "Application user-data directory resolved."};
}

SingleInstanceLock::~SingleInstanceLock() {
#if defined(_WIN32)
    if (m_nativeHandle != nullptr) {
        static_cast<void>(ReleaseMutex(static_cast<HANDLE>(m_nativeHandle)));
        static_cast<void>(CloseHandle(static_cast<HANDLE>(m_nativeHandle)));
        m_nativeHandle = nullptr;
    }
#elif !defined(__EMSCRIPTEN__)
    if (m_fileDescriptor >= 0) {
        static_cast<void>(flock(m_fileDescriptor, LOCK_UN));
        static_cast<void>(close(m_fileDescriptor));
        m_fileDescriptor = -1;
    }
#endif
}

SingleInstanceLockResult SingleInstanceLock::Acquire(
    const std::string_view applicationIdentifier, const std::filesystem::path& userDataDirectory) {
    if (applicationIdentifier.empty()) {
        return {SingleInstanceLockCode::Failed, "The application identifier for the instance lock is empty."};
    }
    if (IsAcquired()) {
        return {SingleInstanceLockCode::Acquired, "This process already owns the instance lock."};
    }
#if defined(_WIN32)
    static_cast<void>(userDataDirectory);
    std::wstring mutexName = L"Local\\UniVex_";
    for (const char character : HashIdentifierHex(applicationIdentifier)) {
        mutexName.push_back(static_cast<wchar_t>(character));
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (mutex == nullptr) {
        return {SingleInstanceLockCode::Failed, "Unable to create the single-instance mutex."};
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        static_cast<void>(CloseHandle(mutex));
        return {SingleInstanceLockCode::AlreadyRunning, "Another instance of this application is already running."};
    }
    m_nativeHandle = mutex;
    return {SingleInstanceLockCode::Acquired, "Single-instance mutex acquired."};
#elif defined(__EMSCRIPTEN__)
    static_cast<void>(userDataDirectory);
    return {SingleInstanceLockCode::Unsupported,
            "This web target does not provide the native single-instance lock backend."};
#else
    const std::filesystem::path lockPath =
        userDataDirectory / (".uve-instance-" + HashIdentifierHex(applicationIdentifier) + ".lock");
    int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fileDescriptor = open(lockPath.c_str(), flags, 0600);
    if (fileDescriptor < 0) {
        return {SingleInstanceLockCode::Failed,
                "Unable to open the single-instance lock file '" + lockPath.string() + "'."};
    }
    if (flock(fileDescriptor, LOCK_EX | LOCK_NB) != 0) {
        const int lockError = errno;
        static_cast<void>(close(fileDescriptor));
        if (lockError == EWOULDBLOCK || lockError == EAGAIN) {
            return {SingleInstanceLockCode::AlreadyRunning,
                    "Another instance of this application is already running."};
        }
        return {SingleInstanceLockCode::Failed, "Unable to acquire the single-instance file lock."};
    }
    m_fileDescriptor = fileDescriptor;
    return {SingleInstanceLockCode::Acquired, "Single-instance file lock acquired."};
#endif
}

bool SingleInstanceLock::IsAcquired() const noexcept {
#if defined(_WIN32)
    return m_nativeHandle != nullptr;
#elif defined(__EMSCRIPTEN__)
    return false;
#else
    return m_fileDescriptor >= 0;
#endif
}

} // namespace UVE::Platform
