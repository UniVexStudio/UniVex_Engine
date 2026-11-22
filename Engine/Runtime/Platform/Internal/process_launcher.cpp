// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "process_launcher.h"

#include <cerrno>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace UVE::Platform {
namespace {

/// The program each platform opens a directory with.
#if defined(_WIN32)
inline constexpr std::string_view kFileManagerProgramUVE = "explorer.exe";
#elif defined(__APPLE__)
inline constexpr std::string_view kFileManagerProgramUVE = "open";
#else
inline constexpr std::string_view kFileManagerProgramUVE = "xdg-open";
#endif

#if defined(_WIN32)
/// Quotes one argument for CommandLineToArgvW-style parsing.
[[nodiscard]] std::wstring QuoteArgumentUVE(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

} // namespace

ProcessLaunchResultUVE LaunchDetachedProcessUVE(const ProcessLaunchDescUVE& desc) {
    if (desc.executable.empty()) {
        return {false, "No program to launch."};
    }
#if defined(_WIN32)
    std::wstring commandLine = QuoteArgumentUVE(desc.executable.wstring());
    for (const std::string& argument : desc.arguments) {
        commandLine += L' ';
        commandLine += QuoteArgumentUVE(std::filesystem::path(argument).wstring());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = desc.workingDirectory.wstring();
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process)) {
        return {false, "Unable to start " + desc.executable.string() + ": " +
                           std::system_category().message(static_cast<int>(GetLastError()))};
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {true, "Started " + desc.executable.string() + "."};
#else
    // An intermediate child starts the program and exits at once, so the program is re-parented
    // away from the caller and never lingers as its zombie.
    const std::string executable = desc.executable.string();
    std::vector<std::string> storage;
    storage.reserve(desc.arguments.size() + 1U);
    storage.push_back(executable);
    storage.insert(storage.end(), desc.arguments.begin(), desc.arguments.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (std::string& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    int errorPipe[2] = {-1, -1};
    // Close-on-exec, so the started program does not hold the pipe open and the read below ends
    // as soon as the intermediate child exits.
    if (pipe(errorPipe) != 0 || fcntl(errorPipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        const int error = errno;
        for (const int descriptor : errorPipe) {
            if (descriptor >= 0) {
                close(descriptor);
            }
        }
        return {false, std::string("Unable to start ") + executable + ": " + std::strerror(error)};
    }
    const pid_t intermediate = fork();
    if (intermediate < 0) {
        close(errorPipe[0]);
        close(errorPipe[1]);
        return {false, std::string("Unable to start ") + executable + ": " + std::strerror(errno)};
    }
    if (intermediate == 0) {
        close(errorPipe[0]);
        setsid();
        if (!desc.workingDirectory.empty() && chdir(desc.workingDirectory.c_str()) != 0) {
            const int error = errno;
            static_cast<void>(write(errorPipe[1], &error, sizeof(error)));
            _exit(1);
        }
        pid_t program = 0;
        const int spawned = desc.executable.has_parent_path()
                                ? posix_spawn(&program, executable.c_str(), nullptr, nullptr, argv.data(), environ)
                                : posix_spawnp(&program, executable.c_str(), nullptr, nullptr, argv.data(), environ);
        if (spawned != 0) {
            static_cast<void>(write(errorPipe[1], &spawned, sizeof(spawned)));
        }
        _exit(spawned == 0 ? 0 : 1);
    }
    close(errorPipe[1]);
    int childError = 0;
    const ssize_t received = read(errorPipe[0], &childError, sizeof(childError));
    close(errorPipe[0]);
    int status = 0;
    static_cast<void>(waitpid(intermediate, &status, 0));
    if (received == static_cast<ssize_t>(sizeof(childError))) {
        return {false, std::string("Unable to start ") + executable + ": " + std::strerror(childError)};
    }
    return {true, "Started " + executable + "."};
#endif
}

std::filesystem::path CurrentExecutablePathUVE() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2U);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0U;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::canonical(buffer, error);
    return error ? std::filesystem::path(buffer) : resolved;
#else
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : self;
#endif
}

ProcessLaunchResultUVE RevealInFileManagerUVE(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return {false, "The folder does not exist: " + directory.string()};
    }
    return LaunchDetachedProcessUVE({std::filesystem::path(kFileManagerProgramUVE), {directory.string()}, {}});
}

} // namespace UVE::Platform
