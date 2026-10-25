// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/logging/log_sink_uve.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace UVE::Debug {

namespace {

/// Formats a time_point as an ISO-8601 UTC timestamp ("YYYY-MM-DDTHH:MM:SSZ").
/// Uses gmtime_r (POSIX, thread-safe) rather than std::gmtime, since this
/// increment targets Linux only.
std::string FormatTimestampUVE(std::chrono::system_clock::time_point timePoint) {
    const std::time_t timeT = std::chrono::system_clock::to_time_t(timePoint);
    std::tm tmValue{};
#if defined(_WIN32)
    gmtime_s(&tmValue, &timeT);
#else
    gmtime_r(&timeT, &tmValue);
#endif
    std::ostringstream stream;
    stream << std::put_time(&tmValue, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

/// Writes the shared human-readable line format used by both ConsoleSinkUVE
/// and FileSinkUVE: "[LEVEL] [category] message (file:line)". `category` and
/// the source-location suffix are omitted when not present on the message.
void WriteLineUVE(std::ostream& out, const LogMessageUVE& message, bool includeTimestamp) {
    if (includeTimestamp) {
        out << '[' << FormatTimestampUVE(message.timestamp) << "] ";
    }
    out << '[' << ToStringUVE(message.level) << "] ";
    if (!message.category.empty()) {
        out << '[' << message.category << "] ";
    }
    out << message.message;
    if (message.sourceFile != nullptr) {
        out << " (" << message.sourceFile << ':' << message.sourceLine << ')';
    }
    out << '\n';
}

} // namespace

#if defined(__ANDROID__)

namespace {

/// The Android log priority a message level belongs to.
[[nodiscard]] int AndroidPriorityUVE(const LogLevelUVE level) noexcept {
    switch (level) {
        case LogLevelUVE::Fatal:
        case LogLevelUVE::Error:
            return ANDROID_LOG_ERROR;
        case LogLevelUVE::Warning:
            return ANDROID_LOG_WARN;
        case LogLevelUVE::Info:
            return ANDROID_LOG_INFO;
        case LogLevelUVE::Debug:
            return ANDROID_LOG_DEBUG;
        case LogLevelUVE::Trace:
        default:
            return ANDROID_LOG_VERBOSE;
    }
}

} // namespace

#endif

void ConsoleSinkUVE::Write(const LogMessageUVE& message) {
    const std::lock_guard<std::mutex> lock(m_mutex);
#if defined(__ANDROID__)
    // A process's stdout and stderr do not reach logcat: an Android app has no console, so every
    // engine log written the desktop way went nowhere at all. That made a black screen on a phone
    // unanswerable - the shader that failed to compile, the GL context that could not be created
    // and the reason Load() returned false were all printed to a stream nobody could read.
    std::ostringstream line;
    WriteLineUVE(line, message, /*includeTimestamp=*/false);
    std::string text = line.str();
    // __android_log_write terminates the entry itself; keeping the shared writer's newline would
    // put an empty line between every pair of logcat entries.
    if (!text.empty() && text.back() == '\n') text.pop_back();
    __android_log_write(AndroidPriorityUVE(message.level), "UniVex", text.c_str());
#else
    std::ostream& out = (message.level >= LogLevelUVE::Warning) ? std::cerr : std::cout;
    WriteLineUVE(out, message, /*includeTimestamp=*/false);
#endif
}

void ConsoleSinkUVE::Flush() {
    const std::lock_guard<std::mutex> lock(m_mutex);
#if !defined(__ANDROID__)
    // Logcat has no client-side buffer to drain; std::cout is not used there at all.
    std::cout.flush();
    std::cerr.flush();
#endif
}

FileSinkUVE::FileSinkUVE(const std::filesystem::path& filePath)
    : m_file(filePath, std::ios::out | std::ios::app) {
    // Deliberately does not assert/throw if the file failed to open: sinks
    // are leaf components with no dependency on the logging/assert
    // machinery that is itself built on top of them. A file that failed to
    // open simply results in Write() silently discarding messages (see
    // class doc comment on FileSinkUVE).
}

FileSinkUVE::~FileSinkUVE() {
    Flush();
}

void FileSinkUVE::Write(const LogMessageUVE& message) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file.is_open()) {
        return;
    }
    WriteLineUVE(m_file, message, /*includeTimestamp=*/true);
    if (message.level == LogLevelUVE::Fatal) {
        m_file.flush();
    }
}

void FileSinkUVE::Flush() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.flush();
    }
}

void MemorySinkUVE::Write(const LogMessageUVE& message) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_messages.push_back(message);
}

void MemorySinkUVE::Flush() {
    // No buffering to flush; messages are already stored on Write().
}

std::vector<LogMessageUVE> MemorySinkUVE::GetMessagesUVE() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_messages;
}

void MemorySinkUVE::Clear() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_messages.clear();
}

} // namespace UVE::Debug
