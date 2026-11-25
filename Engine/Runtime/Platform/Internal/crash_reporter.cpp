// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "crash_reporter.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <process.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace UVE::Platform {
namespace {

std::mutex g_crashReporterMutex;
bool g_crashReporterInstalled = false;

#if defined(_WIN32)
struct WindowsCrashState final {
    HANDLE reportFile = INVALID_HANDLE_VALUE;
    HANDLE dumpFile = INVALID_HANDLE_VALUE;
    volatile LONG crashRecorded = 0;
};
WindowsCrashState* g_windowsCrashState = nullptr;
HANDLE g_terminateReportHandle = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__)
volatile std::sig_atomic_t g_reportFileDescriptor = -1;
volatile std::sig_atomic_t g_crashRecorded = 0;
#endif

[[nodiscard]] std::string CurrentTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
#if defined(_WIN32)
    if (localtime_s(&localTime, &now) != 0) {
        return std::to_string(static_cast<long long>(now));
    }
#else
    if (localtime_r(&now, &localTime) == nullptr) {
        return std::to_string(static_cast<long long>(now));
    }
#endif
    std::array<char, 32U> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y%m%d-%H%M%S", &localTime) == 0U) {
        return std::to_string(static_cast<long long>(now));
    }
    return buffer.data();
}

[[nodiscard]] std::uint64_t GetProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_getpid());
#elif !defined(__EMSCRIPTEN__)
    return static_cast<std::uint64_t>(getpid());
#else
    return 0U;
#endif
}

/// Both helpers below build their string by appending instead of with `operator+`. GCC 12 at
/// -O3 inlines `const char* + std::string&&` into `char_traits<char>::copy` through
/// `insert`/`replace` and then reports a `-Wrestrict` overlap whose size it computed by
/// subtraction - a false positive inside the standard library, but fatal under this project's
/// `-Werror` policy, and one that appears only in the optimized profiles (Debug builds inline
/// nothing). Appending with explicit sizes keeps every copy bound a value the compiler can see.
[[nodiscard]] std::string MakeRetrySuffix(const unsigned int attempt) {
    if (attempt == 0U) {
        return {};
    }
    const std::string digits = std::to_string(attempt);
    std::string suffix;
    suffix.reserve(digits.size() + 1U);
    suffix.push_back('-');
    suffix.append(digits);
    return suffix;
}

/// Builds `<stem><suffix><extension>`; see the comment above for why it appends.
[[nodiscard]] std::string MakeReportFileName(const std::string& stem, const std::string& suffix,
                                                const std::string_view extension) {
    std::string fileName;
    fileName.reserve(stem.size() + suffix.size() + extension.size());
    fileName.append(stem);
    fileName.append(suffix);
    fileName.append(extension);
    return fileName;
}

#if defined(_WIN32)
[[nodiscard]] std::filesystem::path MakePathFromAscii(const std::filesystem::path& directory,
                                                         const std::string& filename) {
    std::wstring wideFilename;
    wideFilename.reserve(filename.size());
    for (const char character : filename) {
        wideFilename.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return directory / wideFilename;
}

void WriteWindowsFile(const HANDLE file, const char* const data, const std::size_t size) noexcept {
    if (file == INVALID_HANDLE_VALUE || data == nullptr) {
        return;
    }
    std::size_t writtenTotal = 0U;
    while (writtenTotal < size) {
        const std::size_t remaining = size - writtenTotal;
        const DWORD chunk = remaining > static_cast<std::size_t>(MAXDWORD)
                                ? MAXDWORD
                                : static_cast<DWORD>(remaining);
        DWORD written = 0U;
        if (WriteFile(file, data + writtenTotal, chunk, &written, nullptr) == 0 || written == 0U) {
            break;
        }
        writtenTotal += static_cast<std::size_t>(written);
    }
    static_cast<void>(FlushFileBuffers(file));
}

LONG WINAPI HandleUnhandledWindowsException(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    WindowsCrashState* const state = g_windowsCrashState;
    if (state == nullptr || InterlockedExchange(&state->crashRecorded, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    constexpr char kReport[] = "UniVex native crash report\r\nUnhandled Windows exception.\r\n";
    WriteWindowsFile(state->reportFile, kReport, sizeof(kReport) - 1U);
    if (state->dumpFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        MINIDUMP_EXCEPTION_INFORMATION* exceptionInfoPointer = nullptr;
        if (exceptionPointers != nullptr) {
            exceptionInfo.ThreadId = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = exceptionPointers;
            exceptionInfo.ClientPointers = FALSE;
            exceptionInfoPointer = &exceptionInfo;
        }
        static_cast<void>(MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), state->dumpFile,
                                            MiniDumpNormal, exceptionInfoPointer, nullptr, nullptr));
        static_cast<void>(FlushFileBuffers(state->dumpFile));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void WriteTerminateReport() noexcept {
    constexpr char kMessage[] = "\r\nC++ std::terminate invoked.\r\n";
    WriteWindowsFile(g_terminateReportHandle, kMessage, sizeof(kMessage) - 1U);
    const std::exception_ptr exception = std::current_exception();
    if (exception == nullptr) {
        return;
    }
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& value) {
        constexpr char kPrefix[] = "Exception: ";
        WriteWindowsFile(g_terminateReportHandle, kPrefix, sizeof(kPrefix) - 1U);
        const char* const message = value.what();
        if (message != nullptr) {
            WriteWindowsFile(g_terminateReportHandle, message, std::char_traits<char>::length(message));
            constexpr char kNewline[] = "\r\n";
            WriteWindowsFile(g_terminateReportHandle, kNewline, sizeof(kNewline) - 1U);
        }
    } catch (...) {
        constexpr char kUnknown[] = "Non-standard exception.\r\n";
        WriteWindowsFile(g_terminateReportHandle, kUnknown, sizeof(kUnknown) - 1U);
    }
}

[[noreturn]] void CrashTerminateHandler() noexcept {
    WriteTerminateReport();
    TerminateProcess(GetCurrentProcess(), 134U);
    std::_Exit(134);
}
#elif !defined(__EMSCRIPTEN__)
void WritePosixFile(const int fileDescriptor, const char* const data, const std::size_t size) noexcept {
    if (fileDescriptor < 0 || data == nullptr) {
        return;
    }
    std::size_t writtenTotal = 0U;
    while (writtenTotal < size) {
        const ssize_t written = write(fileDescriptor, data + writtenTotal, size - writtenTotal);
        if (written <= 0) {
            break;
        }
        writtenTotal += static_cast<std::size_t>(written);
    }
}

void WritePosixUnsigned(const int fileDescriptor, std::uint64_t value) noexcept {
    std::array<char, 24U> digits{};
    std::size_t count = 0U;
    do {
        const std::uint64_t digit = value % 10U;
        digits[count] = static_cast<char>('0' + static_cast<char>(digit));
        value /= 10U;
        ++count;
    } while (value != 0U && count < digits.size());
    while (count > 0U) {
        --count;
        WritePosixFile(fileDescriptor, &digits[count], 1U);
    }
}

void WritePosixHex(const int fileDescriptor, std::uintptr_t value) noexcept {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::array<char, sizeof(value) * 2U> digits{};
    std::size_t count = 0U;
    do {
        const std::size_t digit = static_cast<std::size_t>(value & static_cast<std::uintptr_t>(0xFU));
        digits[count] = kHexDigits[digit];
        value >>= 4U;
        ++count;
    } while (value != 0U && count < digits.size());
    while (count > 0U) {
        --count;
        WritePosixFile(fileDescriptor, &digits[count], 1U);
    }
}

void WriteSignalReport(const int signalNumber, const siginfo_t* const signalInfo) noexcept {
    const int fileDescriptor = static_cast<int>(g_reportFileDescriptor);
    if (fileDescriptor < 0) {
        return;
    }
    constexpr char kPrefix[] = "\nFatal POSIX signal: ";
    constexpr char kNewline[] = "\n";
    WritePosixFile(fileDescriptor, kPrefix, sizeof(kPrefix) - 1U);
    WritePosixUnsigned(fileDescriptor, static_cast<std::uint64_t>(signalNumber));
    WritePosixFile(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
    if (signalInfo != nullptr) {
        constexpr char kAddressPrefix[] = "Fault address: 0x";
        WritePosixFile(fileDescriptor, kAddressPrefix, sizeof(kAddressPrefix) - 1U);
        WritePosixHex(fileDescriptor, reinterpret_cast<std::uintptr_t>(signalInfo->si_addr));
        WritePosixFile(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
    }
}

void HandleFatalPosixSignal(const int signalNumber, siginfo_t* const signalInfo, void*) noexcept {
    if (g_crashRecorded != 0) {
        _exit(128 + signalNumber);
    }
    g_crashRecorded = 1;
    WriteSignalReport(signalNumber, signalInfo);
    _exit(128 + signalNumber);
}

void WritePosixTerminateReport() noexcept {
    const int fileDescriptor = static_cast<int>(g_reportFileDescriptor);
    constexpr char kMessage[] = "\nC++ std::terminate invoked.\n";
    WritePosixFile(fileDescriptor, kMessage, sizeof(kMessage) - 1U);
    const std::exception_ptr exception = std::current_exception();
    if (exception == nullptr) {
        return;
    }
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& value) {
        constexpr char kPrefix[] = "Exception: ";
        WritePosixFile(fileDescriptor, kPrefix, sizeof(kPrefix) - 1U);
        const char* const message = value.what();
        if (message != nullptr) {
            WritePosixFile(fileDescriptor, message, std::char_traits<char>::length(message));
            constexpr char kNewline[] = "\n";
            WritePosixFile(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
        }
    } catch (...) {
        constexpr char kUnknown[] = "Non-standard exception.\n";
        WritePosixFile(fileDescriptor, kUnknown, sizeof(kUnknown) - 1U);
    }
}

[[noreturn]] void CrashTerminateHandler() noexcept {
    if (g_crashRecorded == 0) {
        g_crashRecorded = 1;
        WritePosixTerminateReport();
    }
    _exit(134);
}
#endif

} // namespace

struct CrashReporter::Impl final {
    std::filesystem::path reportPath;
    bool installed = false;
    std::terminate_handler previousTerminateHandler = nullptr;
#if defined(_WIN32)
    std::filesystem::path dumpPath;
    WindowsCrashState windowsState{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#elif !defined(__EMSCRIPTEN__)
    static constexpr std::size_t kSignalCount = 5U;
    std::array<int, kSignalCount> signalNumbers{SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
    std::array<struct sigaction, kSignalCount> previousSignalActions{};
    std::size_t installedSignalCount = 0U;
    int reportFileDescriptor = -1;
#endif

    void Uninstall() noexcept {
        if (!installed) {
#if defined(_WIN32)
            const bool reportWasCreated = windowsState.reportFile != INVALID_HANDLE_VALUE;
            const bool dumpWasCreated = windowsState.dumpFile != INVALID_HANDLE_VALUE;
            if (reportWasCreated) {
                static_cast<void>(CloseHandle(windowsState.reportFile));
                windowsState.reportFile = INVALID_HANDLE_VALUE;
            }
            if (dumpWasCreated) {
                static_cast<void>(CloseHandle(windowsState.dumpFile));
                windowsState.dumpFile = INVALID_HANDLE_VALUE;
            }
            std::error_code ignored;
            if (reportWasCreated) {
                std::filesystem::remove(reportPath, ignored);
            }
            if (dumpWasCreated) {
                std::filesystem::remove(dumpPath, ignored);
            }
#elif !defined(__EMSCRIPTEN__)
            const bool reportWasCreated = reportFileDescriptor >= 0;
            if (reportWasCreated) {
                static_cast<void>(close(reportFileDescriptor));
                reportFileDescriptor = -1;
                std::error_code ignored;
                std::filesystem::remove(reportPath, ignored);
            }
#endif
            return;
        }
        const std::lock_guard<std::mutex> lock(g_crashReporterMutex);
#if defined(_WIN32)
        if (g_windowsCrashState == &windowsState) {
            static_cast<void>(SetUnhandledExceptionFilter(previousExceptionFilter));
            g_windowsCrashState = nullptr;
            g_terminateReportHandle = INVALID_HANDLE_VALUE;
        }
        if (previousTerminateHandler != nullptr) {
            static_cast<void>(std::set_terminate(previousTerminateHandler));
            previousTerminateHandler = nullptr;
        }
        if (windowsState.reportFile != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(windowsState.reportFile));
            windowsState.reportFile = INVALID_HANDLE_VALUE;
        }
        if (windowsState.dumpFile != INVALID_HANDLE_VALUE) {
            static_cast<void>(CloseHandle(windowsState.dumpFile));
            windowsState.dumpFile = INVALID_HANDLE_VALUE;
        }
        if (InterlockedCompareExchange(&windowsState.crashRecorded, 0, 0) == 0) {
            std::error_code ignored;
            std::filesystem::remove(reportPath, ignored);
            std::filesystem::remove(dumpPath, ignored);
        }
#elif !defined(__EMSCRIPTEN__)
        for (std::size_t index = 0U; index < installedSignalCount; ++index) {
            static_cast<void>(sigaction(signalNumbers[index], &previousSignalActions[index], nullptr));
        }
        installedSignalCount = 0U;
        if (g_reportFileDescriptor == reportFileDescriptor) {
            g_reportFileDescriptor = -1;
            g_crashRecorded = 0;
        }
        if (previousTerminateHandler != nullptr) {
            static_cast<void>(std::set_terminate(previousTerminateHandler));
            previousTerminateHandler = nullptr;
        }
        if (reportFileDescriptor >= 0) {
            static_cast<void>(close(reportFileDescriptor));
            reportFileDescriptor = -1;
        }
        std::error_code ignored;
        std::filesystem::remove(reportPath, ignored);
#endif
        if (installed) {
            g_crashReporterInstalled = false;
            installed = false;
        }
    }

    ~Impl() { Uninstall(); }
};

CrashReporter::CrashReporter() = default;
CrashReporter::~CrashReporter() = default;

CrashReporterResult CrashReporter::Install(const std::filesystem::path& dumpDirectory,
                                                     const std::string& applicationIdentifier) {
    if (m_impl != nullptr && m_impl->installed) {
        return {CrashReporterCode::AlreadyInstalled, m_impl->reportPath,
                "This crash reporter already owns the process handlers."};
    }
    if (dumpDirectory.empty() || applicationIdentifier.empty()) {
        return {CrashReporterCode::Failed, {}, "A dump directory and application identifier are required."};
    }
#if defined(__EMSCRIPTEN__)
    static_cast<void>(dumpDirectory);
    static_cast<void>(applicationIdentifier);
    return {CrashReporterCode::Unsupported, {}, "The web target does not provide a native crash-report backend."};
#else
    std::error_code error;
    std::filesystem::create_directories(dumpDirectory, error);
    if (error || !std::filesystem::is_directory(dumpDirectory, error) || error) {
        return {CrashReporterCode::Failed, {}, "Unable to create or inspect the crash dump directory."};
    }

    const std::lock_guard<std::mutex> lock(g_crashReporterMutex);
    if (g_crashReporterInstalled) {
        return {CrashReporterCode::AlreadyInstalled, {}, "Another crash reporter already owns the process handlers."};
    }
    std::unique_ptr<Impl> candidate = std::make_unique<Impl>();
    const std::string fileStem = "uve-crash-" + CurrentTimestamp() + "-" + std::to_string(GetProcessId());
#if defined(_WIN32)
    std::filesystem::path reportPath;
    std::filesystem::path dumpPath;
    for (unsigned int suffix = 0U; suffix < 100U; ++suffix) {
        const std::string suffixText = MakeRetrySuffix(suffix);
        reportPath = MakePathFromAscii(dumpDirectory, MakeReportFileName(fileStem, suffixText, ".log"));
        dumpPath = MakePathFromAscii(dumpDirectory, MakeReportFileName(fileStem, suffixText, ".dmp"));
        candidate->windowsState.reportFile = CreateFileW(reportPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (candidate->windowsState.reportFile == INVALID_HANDLE_VALUE) {
            const DWORD reportOpenError = GetLastError();
            if (reportOpenError == ERROR_FILE_EXISTS || reportOpenError == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return {CrashReporterCode::Failed, {}, "Unable to create the crash report file."};
        }
        candidate->windowsState.dumpFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (candidate->windowsState.dumpFile != INVALID_HANDLE_VALUE) {
            break;
        }
        const DWORD dumpOpenError = GetLastError();
        static_cast<void>(CloseHandle(candidate->windowsState.reportFile));
        candidate->windowsState.reportFile = INVALID_HANDLE_VALUE;
        std::error_code ignored;
        std::filesystem::remove(reportPath, ignored);
        if (dumpOpenError != ERROR_FILE_EXISTS && dumpOpenError != ERROR_ALREADY_EXISTS) {
            return {CrashReporterCode::Failed, {}, "Unable to reserve the native minidump file."};
        }
    }
    if (candidate->windowsState.reportFile == INVALID_HANDLE_VALUE ||
        candidate->windowsState.dumpFile == INVALID_HANDLE_VALUE) {
        return {CrashReporterCode::Failed, {}, "Unable to reserve unique crash-report filenames."};
    }
    candidate->reportPath = reportPath;
    candidate->dumpPath = dumpPath;
    constexpr char kHeader[] = "UniVex native crash report reserved for this process.\r\nApplication: ";
    WriteWindowsFile(candidate->windowsState.reportFile, kHeader, sizeof(kHeader) - 1U);
    WriteWindowsFile(candidate->windowsState.reportFile, applicationIdentifier.data(), applicationIdentifier.size());
    constexpr char kLineEnding[] = "\r\n";
    WriteWindowsFile(candidate->windowsState.reportFile, kLineEnding, sizeof(kLineEnding) - 1U);
    candidate->previousExceptionFilter = SetUnhandledExceptionFilter(&HandleUnhandledWindowsException);
    g_windowsCrashState = &candidate->windowsState;
    g_terminateReportHandle = candidate->windowsState.reportFile;
    candidate->previousTerminateHandler = std::set_terminate(&CrashTerminateHandler);
#else
    int reportFileDescriptor = -1;
    std::filesystem::path reportPath;
    for (unsigned int suffix = 0U; suffix < 100U; ++suffix) {
        const std::string suffixText = MakeRetrySuffix(suffix);
        reportPath = dumpDirectory / MakeReportFileName(fileStem, suffixText, ".log");
        int flags = O_CREAT | O_EXCL | O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        reportFileDescriptor = open(reportPath.c_str(), flags, 0600);
        if (reportFileDescriptor >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return {CrashReporterCode::Failed, {}, "Unable to create the crash report file."};
        }
    }
    if (reportFileDescriptor < 0) {
        return {CrashReporterCode::Failed, {}, "Unable to reserve a unique crash-report filename."};
    }
    candidate->reportFileDescriptor = reportFileDescriptor;
    candidate->reportPath = reportPath;
    constexpr char kHeader[] = "UniVex crash report reserved for this process.\nApplication: ";
    WritePosixFile(reportFileDescriptor, kHeader, sizeof(kHeader) - 1U);
    WritePosixFile(reportFileDescriptor, applicationIdentifier.data(), applicationIdentifier.size());
    constexpr char kLineEnding[] = "\n";
    WritePosixFile(reportFileDescriptor, kLineEnding, sizeof(kLineEnding) - 1U);

    struct sigaction action{};
    action.sa_sigaction = &HandleFatalPosixSignal;
    action.sa_flags = SA_SIGINFO;
    static_cast<void>(sigemptyset(&action.sa_mask));
    g_reportFileDescriptor = static_cast<std::sig_atomic_t>(reportFileDescriptor);
    g_crashRecorded = 0;
    for (std::size_t index = 0U; index < candidate->signalNumbers.size(); ++index) {
        if (sigaction(candidate->signalNumbers[index], &action, &candidate->previousSignalActions[index]) != 0) {
            for (std::size_t restoreIndex = 0U; restoreIndex < candidate->installedSignalCount; ++restoreIndex) {
                static_cast<void>(sigaction(candidate->signalNumbers[restoreIndex],
                                            &candidate->previousSignalActions[restoreIndex], nullptr));
            }
            candidate->installedSignalCount = 0U;
            g_reportFileDescriptor = -1;
            static_cast<void>(close(reportFileDescriptor));
            candidate->reportFileDescriptor = -1;
            std::error_code ignored;
            std::filesystem::remove(reportPath, ignored);
            return {CrashReporterCode::Failed, {}, "Unable to install a fatal POSIX signal handler."};
        }
        ++candidate->installedSignalCount;
    }
    candidate->previousTerminateHandler = std::set_terminate(&CrashTerminateHandler);
#endif
    candidate->installed = true;
    g_crashReporterInstalled = true;
    const std::filesystem::path reportPathResult = candidate->reportPath;
    m_impl = std::move(candidate);
#if defined(_WIN32)
    return {CrashReporterCode::Installed, reportPathResult,
            "Native crash reporting installed; a Windows minidump will be written beside the report."};
#else
    return {CrashReporterCode::Installed, reportPathResult,
            "Fatal-signal and terminate reporting installed; POSIX reports include signal/address metadata."};
#endif
#endif
}

bool CrashReporter::IsInstalled() const noexcept { return m_impl != nullptr && m_impl->installed; }

} // namespace UVE::Platform
