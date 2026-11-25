// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "logger.h"

#include <chrono>
#include <thread>
#include <utility>

namespace UVE::Debug {

std::atomic<Logger*> Logger::s_activeInstance{nullptr};

Logger::Logger() = default;

Logger::~Logger() {
    Shutdown();
}

void Logger::Init(LogLevel minLevel) {
    m_minLevel.store(minLevel, std::memory_order_relaxed);
    s_activeInstance.store(this, std::memory_order_release);
}

void Logger::Shutdown() {
    Logger* expected = this;
    s_activeInstance.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

    const std::lock_guard<std::mutex> lock(m_sinkMutex);
    FlushAllSinksLocked();
    m_sinks.clear();
}

void Logger::AddSink(std::unique_ptr<ILogSink> sink) {
    const std::lock_guard<std::mutex> lock(m_sinkMutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::SetMinLevel(LogLevel level) {
    m_minLevel.store(level, std::memory_order_relaxed);
}

LogLevel Logger::GetMinLevel() const {
    return m_minLevel.load(std::memory_order_relaxed);
}

void Logger::LogFormatted(LogLevel level,
                              std::string_view category,
                              const char* sourceFile,
                              int sourceLine,
                              std::string formattedMessage) {
    if (level < GetMinLevel()) {
        return;
    }

    LogMessage message;
    message.level = level;
    message.category = std::string(category);
    message.message = std::move(formattedMessage);
    message.sourceFile = sourceFile;
    message.sourceLine = sourceLine;
    message.timestamp = std::chrono::system_clock::now();
    message.threadId = std::this_thread::get_id();

    const std::lock_guard<std::mutex> lock(m_sinkMutex);
    for (const std::unique_ptr<ILogSink>& sink : m_sinks) {
        sink->Write(message);
    }
    if (level == LogLevel::Fatal) {
        FlushAllSinksLocked();
    }
}

void Logger::FlushAllSinksLocked() {
    for (const std::unique_ptr<ILogSink>& sink : m_sinks) {
        sink->Flush();
    }
}

Logger* Logger::GetActiveInstance() noexcept {
    return s_activeInstance.load(std::memory_order_acquire);
}

} // namespace UVE::Debug
