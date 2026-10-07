// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/platform/application_runtime_uve.h"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
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

[[nodiscard]] std::uint64_t HashIdentifierUVE(const std::string_view identifier) noexcept {
    // FNV-1a is stable across processes and standard-library implementations, unlike std::hash.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char rawByte : identifier) {
        const unsigned char byte = static_cast<unsigned char>(rawByte);
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string HashIdentifierHexUVE(const std::string_view identifier) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << HashIdentifierUVE(identifier);
    return output.str();
}

[[nodiscard]] std::string SanitizeDirectoryNameUVE(const std::string& text) {
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

[[nodiscard]] bool IsSafeUserDataDirectoryNameUVE(const std::string_view directoryName) noexcept {
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

[[nodiscard]] ApplicationUserDataDirectoryResultUVE MakeDirectoryFailureUVE(std::string message) {
    return {false, {}, std::move(message)};
}

} // namespace

std::string_view GetApplicationTargetUVE() noexcept {
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

ApplicationUserDataDirectoryResultUVE ResolveApplicationUserDataDirectoryUVE(
    const EditorProjectPackageUVE& package, const std::filesystem::path& projectRoot) {
    std::string directoryName = package.applicationSettings.userDataDirectoryName;
    if (directoryName.empty()) {
        const std::string& fallback = package.productMetadata.shortName.empty()
                                          ? package.projectId
                                          : package.productMetadata.shortName;
        directoryName = SanitizeDirectoryNameUVE(fallback);
    } else if (!IsSafeUserDataDirectoryNameUVE(directoryName)) {
        return MakeDirectoryFailureUVE("The configured user-data directory name is not a safe single path segment.");
    }
    if (directoryName.empty()) {
        directoryName = "univex-project-" + HashIdentifierHexUVE(package.projectId).substr(0U, 8U);
    }

    std::error_code error;
    const std::filesystem::path absoluteProjectRoot = std::filesystem::absolute(
        projectRoot.empty() ? std::filesystem::current_path(error) : projectRoot, error);
    if (error) {
        return MakeDirectoryFailureUVE("Unable to resolve the project root for application data: " + error.message());
    }

    std::filesystem::path baseDirectory;
    if (package.applicationSettings.portableUserData) {
        baseDirectory = absoluteProjectRoot;
    } else {
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
            return MakeDirectoryFailureUVE("No per-user Windows application-data directory is available.");
        }
        baseDirectory = selected;
#elif defined(__APPLE__)
        const char* const home = std::getenv("HOME");
        if (home == nullptr || home[0] == '\0' || !std::filesystem::path(home).is_absolute()) {
            return MakeDirectoryFailureUVE("HOME is unavailable for the Apple application-data directory.");
        }
        baseDirectory = std::filesystem::path(home) / "Library" / "Application Support";
#elif defined(__ANDROID__)
        const char* const home = std::getenv("HOME");
        if (home == nullptr || home[0] == '\0' || !std::filesystem::path(home).is_absolute()) {
            return MakeDirectoryFailureUVE("An app-sandbox HOME is required for Android application data.");
        }
        baseDirectory = home;
#else
        const char* const xdgDataHome = std::getenv("XDG_DATA_HOME");
        const char* const home = std::getenv("HOME");
        if (xdgDataHome != nullptr && xdgDataHome[0] != '\0' && std::filesystem::path(xdgDataHome).is_absolute()) {
            baseDirectory = xdgDataHome;
        } else if (home != nullptr && home[0] != '\0') {
            baseDirectory = std::filesystem::path(home) / ".local" / "share";
        } else {
            return MakeDirectoryFailureUVE("Neither XDG_DATA_HOME nor HOME is available for application data.");
        }
#endif
    }

    const std::filesystem::path directory = baseDirectory / directoryName;
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error) {
        return MakeDirectoryFailureUVE("Unable to create the application user-data directory '" +
                                       directory.string() + "': " + error.message());
    }
    error.clear();
    if (!std::filesystem::is_directory(directory, error) || error) {
        return MakeDirectoryFailureUVE("The application user-data path is not a directory: " + directory.string());
    }
    return {true, directory, "Application user-data directory resolved."};
}

SingleInstanceLockUVE::~SingleInstanceLockUVE() {
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

SingleInstanceLockResultUVE SingleInstanceLockUVE::AcquireUVE(
    const std::string_view applicationIdentifier, const std::filesystem::path& userDataDirectory) {
    if (applicationIdentifier.empty()) {
        return {SingleInstanceLockCodeUVE::Failed, "The application identifier for the instance lock is empty."};
    }
    if (IsAcquiredUVE()) {
        return {SingleInstanceLockCodeUVE::Acquired, "This process already owns the instance lock."};
    }
#if defined(_WIN32)
    std::wstring mutexName = L"Local\\UniVex_";
    for (const char character : HashIdentifierHexUVE(applicationIdentifier)) {
        mutexName.push_back(static_cast<wchar_t>(character));
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (mutex == nullptr) {
        return {SingleInstanceLockCodeUVE::Failed, "Unable to create the single-instance mutex."};
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        static_cast<void>(CloseHandle(mutex));
        return {SingleInstanceLockCodeUVE::AlreadyRunning, "Another instance of this application is already running."};
    }
    m_nativeHandle = mutex;
    return {SingleInstanceLockCodeUVE::Acquired, "Single-instance mutex acquired."};
#elif defined(__EMSCRIPTEN__)
    static_cast<void>(userDataDirectory);
    return {SingleInstanceLockCodeUVE::Unsupported,
            "This web target does not provide the native single-instance lock backend."};
#else
    const std::filesystem::path lockPath =
        userDataDirectory / (".uve-instance-" + HashIdentifierHexUVE(applicationIdentifier) + ".lock");
    int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fileDescriptor = open(lockPath.c_str(), flags, 0600);
    if (fileDescriptor < 0) {
        return {SingleInstanceLockCodeUVE::Failed,
                "Unable to open the single-instance lock file '" + lockPath.string() + "'."};
    }
    if (flock(fileDescriptor, LOCK_EX | LOCK_NB) != 0) {
        const int lockError = errno;
        static_cast<void>(close(fileDescriptor));
        if (lockError == EWOULDBLOCK || lockError == EAGAIN) {
            return {SingleInstanceLockCodeUVE::AlreadyRunning,
                    "Another instance of this application is already running."};
        }
        return {SingleInstanceLockCodeUVE::Failed, "Unable to acquire the single-instance file lock."};
    }
    m_fileDescriptor = fileDescriptor;
    return {SingleInstanceLockCodeUVE::Acquired, "Single-instance file lock acquired."};
#endif
}

bool SingleInstanceLockUVE::IsAcquiredUVE() const noexcept {
#if defined(_WIN32)
    return m_nativeHandle != nullptr;
#elif defined(__EMSCRIPTEN__)
    return false;
#else
    return m_fileDescriptor >= 0;
#endif
}

} // namespace UVE::Platform
