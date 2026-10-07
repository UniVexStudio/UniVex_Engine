// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/platform/crash_reporter_uve.h"

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

std::mutex g_crashReporterMutexUVE;
bool g_crashReporterInstalledUVE = false;

#if defined(_WIN32)
struct WindowsCrashStateUVE final {
    HANDLE reportFile = INVALID_HANDLE_VALUE;
    HANDLE dumpFile = INVALID_HANDLE_VALUE;
    volatile LONG crashRecorded = 0;
};
WindowsCrashStateUVE* g_windowsCrashStateUVE = nullptr;
HANDLE g_terminateReportHandleUVE = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__)
volatile std::sig_atomic_t g_reportFileDescriptorUVE = -1;
volatile std::sig_atomic_t g_crashRecordedUVE = 0;
#endif

[[nodiscard]] std::string CurrentTimestampUVE() {
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

[[nodiscard]] std::uint64_t GetProcessIdUVE() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_getpid());
#elif !defined(__EMSCRIPTEN__)
    return static_cast<std::uint64_t>(getpid());
#else
    return 0U;
#endif
}

#if defined(_WIN32)
[[nodiscard]] std::filesystem::path MakePathFromAsciiUVE(const std::filesystem::path& directory,
                                                         const std::string& filename) {
    std::wstring wideFilename;
    wideFilename.reserve(filename.size());
    for (const char character : filename) {
        wideFilename.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }
    return directory / wideFilename;
}

void WriteWindowsFileUVE(const HANDLE file, const char* const data, const std::size_t size) noexcept {
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

LONG WINAPI HandleUnhandledWindowsExceptionUVE(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    WindowsCrashStateUVE* const state = g_windowsCrashStateUVE;
    if (state == nullptr || InterlockedExchange(&state->crashRecorded, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    constexpr char kReport[] = "UniVex native crash report\r\nUnhandled Windows exception.\r\n";
    WriteWindowsFileUVE(state->reportFile, kReport, sizeof(kReport) - 1U);
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

void WriteTerminateReportUVE() noexcept {
    constexpr char kMessage[] = "\r\nC++ std::terminate invoked.\r\n";
    WriteWindowsFileUVE(g_terminateReportHandleUVE, kMessage, sizeof(kMessage) - 1U);
    const std::exception_ptr exception = std::current_exception();
    if (exception == nullptr) {
        return;
    }
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& value) {
        constexpr char kPrefix[] = "Exception: ";
        WriteWindowsFileUVE(g_terminateReportHandleUVE, kPrefix, sizeof(kPrefix) - 1U);
        const char* const message = value.what();
        if (message != nullptr) {
            WriteWindowsFileUVE(g_terminateReportHandleUVE, message, std::char_traits<char>::length(message));
            constexpr char kNewline[] = "\r\n";
            WriteWindowsFileUVE(g_terminateReportHandleUVE, kNewline, sizeof(kNewline) - 1U);
        }
    } catch (...) {
        constexpr char kUnknown[] = "Non-standard exception.\r\n";
        WriteWindowsFileUVE(g_terminateReportHandleUVE, kUnknown, sizeof(kUnknown) - 1U);
    }
}

[[noreturn]] void CrashTerminateHandlerUVE() noexcept {
    WriteTerminateReportUVE();
    TerminateProcess(GetCurrentProcess(), 134U);
    std::_Exit(134);
}
#elif !defined(__EMSCRIPTEN__)
void WritePosixFileUVE(const int fileDescriptor, const char* const data, const std::size_t size) noexcept {
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

void WritePosixUnsignedUVE(const int fileDescriptor, std::uint64_t value) noexcept {
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
        WritePosixFileUVE(fileDescriptor, &digits[count], 1U);
    }
}

void WritePosixHexUVE(const int fileDescriptor, std::uintptr_t value) noexcept {
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
        WritePosixFileUVE(fileDescriptor, &digits[count], 1U);
    }
}

void WriteSignalReportUVE(const int signalNumber, const siginfo_t* const signalInfo) noexcept {
    const int fileDescriptor = static_cast<int>(g_reportFileDescriptorUVE);
    if (fileDescriptor < 0) {
        return;
    }
    constexpr char kPrefix[] = "\nFatal POSIX signal: ";
    constexpr char kNewline[] = "\n";
    WritePosixFileUVE(fileDescriptor, kPrefix, sizeof(kPrefix) - 1U);
    WritePosixUnsignedUVE(fileDescriptor, static_cast<std::uint64_t>(signalNumber));
    WritePosixFileUVE(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
    if (signalInfo != nullptr) {
        constexpr char kAddressPrefix[] = "Fault address: 0x";
        WritePosixFileUVE(fileDescriptor, kAddressPrefix, sizeof(kAddressPrefix) - 1U);
        WritePosixHexUVE(fileDescriptor, reinterpret_cast<std::uintptr_t>(signalInfo->si_addr));
        WritePosixFileUVE(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
    }
}

void HandleFatalPosixSignalUVE(const int signalNumber, siginfo_t* const signalInfo, void*) noexcept {
    if (g_crashRecordedUVE != 0) {
        _exit(128 + signalNumber);
    }
    g_crashRecordedUVE = 1;
    WriteSignalReportUVE(signalNumber, signalInfo);
    _exit(128 + signalNumber);
}

void WritePosixTerminateReportUVE() noexcept {
    const int fileDescriptor = static_cast<int>(g_reportFileDescriptorUVE);
    constexpr char kMessage[] = "\nC++ std::terminate invoked.\n";
    WritePosixFileUVE(fileDescriptor, kMessage, sizeof(kMessage) - 1U);
    const std::exception_ptr exception = std::current_exception();
    if (exception == nullptr) {
        return;
    }
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& value) {
        constexpr char kPrefix[] = "Exception: ";
        WritePosixFileUVE(fileDescriptor, kPrefix, sizeof(kPrefix) - 1U);
        const char* const message = value.what();
        if (message != nullptr) {
            WritePosixFileUVE(fileDescriptor, message, std::char_traits<char>::length(message));
            constexpr char kNewline[] = "\n";
            WritePosixFileUVE(fileDescriptor, kNewline, sizeof(kNewline) - 1U);
        }
    } catch (...) {
        constexpr char kUnknown[] = "Non-standard exception.\n";
        WritePosixFileUVE(fileDescriptor, kUnknown, sizeof(kUnknown) - 1U);
    }
}

[[noreturn]] void CrashTerminateHandlerUVE() noexcept {
    if (g_crashRecordedUVE == 0) {
        g_crashRecordedUVE = 1;
        WritePosixTerminateReportUVE();
    }
    _exit(134);
}
#endif

} // namespace

struct CrashReporterUVE::ImplUVE final {
    std::filesystem::path reportPath;
    bool installed = false;
    std::terminate_handler previousTerminateHandler = nullptr;
#if defined(_WIN32)
    std::filesystem::path dumpPath;
    WindowsCrashStateUVE windowsState{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
#elif !defined(__EMSCRIPTEN__)
    static constexpr std::size_t kSignalCount = 5U;
    std::array<int, kSignalCount> signalNumbers{SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
    std::array<struct sigaction, kSignalCount> previousSignalActions{};
    std::size_t installedSignalCount = 0U;
    int reportFileDescriptor = -1;
#endif

    void UninstallUVE() noexcept {
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
        const std::lock_guard<std::mutex> lock(g_crashReporterMutexUVE);
#if defined(_WIN32)
        if (g_windowsCrashStateUVE == &windowsState) {
            static_cast<void>(SetUnhandledExceptionFilter(previousExceptionFilter));
            g_windowsCrashStateUVE = nullptr;
            g_terminateReportHandleUVE = INVALID_HANDLE_VALUE;
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
        if (g_reportFileDescriptorUVE == reportFileDescriptor) {
            g_reportFileDescriptorUVE = -1;
            g_crashRecordedUVE = 0;
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
            g_crashReporterInstalledUVE = false;
            installed = false;
        }
    }

    ~ImplUVE() { UninstallUVE(); }
};

CrashReporterUVE::CrashReporterUVE() = default;
CrashReporterUVE::~CrashReporterUVE() = default;

CrashReporterResultUVE CrashReporterUVE::InstallUVE(const std::filesystem::path& dumpDirectory,
                                                     const std::string& applicationIdentifier) {
    if (m_impl != nullptr && m_impl->installed) {
        return {CrashReporterCodeUVE::AlreadyInstalled, m_impl->reportPath,
                "This crash reporter already owns the process handlers."};
    }
    if (dumpDirectory.empty() || applicationIdentifier.empty()) {
        return {CrashReporterCodeUVE::Failed, {}, "A dump directory and application identifier are required."};
    }
#if defined(__EMSCRIPTEN__)
    static_cast<void>(dumpDirectory);
    static_cast<void>(applicationIdentifier);
    return {CrashReporterCodeUVE::Unsupported, {}, "The web target does not provide a native crash-report backend."};
#else
    std::error_code error;
    std::filesystem::create_directories(dumpDirectory, error);
    if (error || !std::filesystem::is_directory(dumpDirectory, error) || error) {
        return {CrashReporterCodeUVE::Failed, {}, "Unable to create or inspect the crash dump directory."};
    }

    const std::lock_guard<std::mutex> lock(g_crashReporterMutexUVE);
    if (g_crashReporterInstalledUVE) {
        return {CrashReporterCodeUVE::AlreadyInstalled, {}, "Another crash reporter already owns the process handlers."};
    }
    std::unique_ptr<ImplUVE> candidate = std::make_unique<ImplUVE>();
    const std::string fileStem = "uve-crash-" + CurrentTimestampUVE() + "-" + std::to_string(GetProcessIdUVE());
#if defined(_WIN32)
    std::filesystem::path reportPath;
    std::filesystem::path dumpPath;
    for (unsigned int suffix = 0U; suffix < 100U; ++suffix) {
        const std::string suffixText = suffix == 0U ? std::string{} : "-" + std::to_string(suffix);
        reportPath = MakePathFromAsciiUVE(dumpDirectory, fileStem + suffixText + ".log");
        dumpPath = MakePathFromAsciiUVE(dumpDirectory, fileStem + suffixText + ".dmp");
        candidate->windowsState.reportFile = CreateFileW(reportPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (candidate->windowsState.reportFile == INVALID_HANDLE_VALUE) {
            const DWORD reportOpenError = GetLastError();
            if (reportOpenError == ERROR_FILE_EXISTS || reportOpenError == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return {CrashReporterCodeUVE::Failed, {}, "Unable to create the crash report file."};
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
            return {CrashReporterCodeUVE::Failed, {}, "Unable to reserve the native minidump file."};
        }
    }
    if (candidate->windowsState.reportFile == INVALID_HANDLE_VALUE ||
        candidate->windowsState.dumpFile == INVALID_HANDLE_VALUE) {
        return {CrashReporterCodeUVE::Failed, {}, "Unable to reserve unique crash-report filenames."};
    }
    candidate->reportPath = reportPath;
    candidate->dumpPath = dumpPath;
    constexpr char kHeader[] = "UniVex native crash report reserved for this process.\r\nApplication: ";
    WriteWindowsFileUVE(candidate->windowsState.reportFile, kHeader, sizeof(kHeader) - 1U);
    WriteWindowsFileUVE(candidate->windowsState.reportFile, applicationIdentifier.data(), applicationIdentifier.size());
    constexpr char kLineEnding[] = "\r\n";
    WriteWindowsFileUVE(candidate->windowsState.reportFile, kLineEnding, sizeof(kLineEnding) - 1U);
    candidate->previousExceptionFilter = SetUnhandledExceptionFilter(&HandleUnhandledWindowsExceptionUVE);
    g_windowsCrashStateUVE = &candidate->windowsState;
    g_terminateReportHandleUVE = candidate->windowsState.reportFile;
    candidate->previousTerminateHandler = std::set_terminate(&CrashTerminateHandlerUVE);
#else
    int reportFileDescriptor = -1;
    std::filesystem::path reportPath;
    for (unsigned int suffix = 0U; suffix < 100U; ++suffix) {
        const std::string suffixText = suffix == 0U ? std::string{} : "-" + std::to_string(suffix);
        reportPath = dumpDirectory / (fileStem + suffixText + ".log");
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
            return {CrashReporterCodeUVE::Failed, {}, "Unable to create the crash report file."};
        }
    }
    if (reportFileDescriptor < 0) {
        return {CrashReporterCodeUVE::Failed, {}, "Unable to reserve a unique crash-report filename."};
    }
    candidate->reportFileDescriptor = reportFileDescriptor;
    candidate->reportPath = reportPath;
    constexpr char kHeader[] = "UniVex crash report reserved for this process.\nApplication: ";
    WritePosixFileUVE(reportFileDescriptor, kHeader, sizeof(kHeader) - 1U);
    WritePosixFileUVE(reportFileDescriptor, applicationIdentifier.data(), applicationIdentifier.size());
    constexpr char kLineEnding[] = "\n";
    WritePosixFileUVE(reportFileDescriptor, kLineEnding, sizeof(kLineEnding) - 1U);

    struct sigaction action{};
    action.sa_sigaction = &HandleFatalPosixSignalUVE;
    action.sa_flags = SA_SIGINFO;
    static_cast<void>(sigemptyset(&action.sa_mask));
    g_reportFileDescriptorUVE = static_cast<std::sig_atomic_t>(reportFileDescriptor);
    g_crashRecordedUVE = 0;
    for (std::size_t index = 0U; index < candidate->signalNumbers.size(); ++index) {
        if (sigaction(candidate->signalNumbers[index], &action, &candidate->previousSignalActions[index]) != 0) {
            for (std::size_t restoreIndex = 0U; restoreIndex < candidate->installedSignalCount; ++restoreIndex) {
                static_cast<void>(sigaction(candidate->signalNumbers[restoreIndex],
                                            &candidate->previousSignalActions[restoreIndex], nullptr));
            }
            candidate->installedSignalCount = 0U;
            g_reportFileDescriptorUVE = -1;
            static_cast<void>(close(reportFileDescriptor));
            candidate->reportFileDescriptor = -1;
            std::error_code ignored;
            std::filesystem::remove(reportPath, ignored);
            return {CrashReporterCodeUVE::Failed, {}, "Unable to install a fatal POSIX signal handler."};
        }
        ++candidate->installedSignalCount;
    }
    candidate->previousTerminateHandler = std::set_terminate(&CrashTerminateHandlerUVE);
#endif
    candidate->installed = true;
    g_crashReporterInstalledUVE = true;
    const std::filesystem::path reportPathResult = candidate->reportPath;
    m_impl = std::move(candidate);
#if defined(_WIN32)
    return {CrashReporterCodeUVE::Installed, reportPathResult,
            "Native crash reporting installed; a Windows minidump will be written beside the report."};
#else
    return {CrashReporterCodeUVE::Installed, reportPathResult,
            "Fatal-signal and terminate reporting installed; POSIX reports include signal/address metadata."};
#endif
#endif
}

bool CrashReporterUVE::IsInstalledUVE() const noexcept { return m_impl != nullptr && m_impl->installed; }

} // namespace UVE::Platform
