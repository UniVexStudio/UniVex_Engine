// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "frame_trace.h"

#include <cstdlib>
#include <fstream>

namespace UVE::Core {
namespace {

[[nodiscard]] std::size_t ReadMaximumFrames() {
    const char* const raw = std::getenv("UVE_FRAME_TRACE_MAX");
    if (raw == nullptr || *raw == '\0') {
        return FrameTrace::kDefaultMaximumFrames;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed == 0ULL) {
        return FrameTrace::kDefaultMaximumFrames;
    }
    return static_cast<std::size_t>(parsed);
}

} // namespace

FrameTrace& FrameTrace::Instance() {
    static FrameTrace instance;
    return instance;
}

FrameTrace::FrameTrace() : m_maximumFrames(ReadMaximumFrames()) {
    const char* const path = std::getenv("UVE_FRAME_TRACE");
    if (path == nullptr || *path == '\0') {
        return;
    }
    m_path = path;
    m_enabled = true;
    m_records.reserve(4096U);
}

FrameTrace::~FrameTrace() {
    static_cast<void>(Flush());
}

void FrameTrace::Record(const std::uint64_t frameNumber, const double frameSeconds,
                        const double deltaSeconds, const double updateSeconds,
                        const double lateUpdateSeconds, const double renderSeconds) {
    if (!m_enabled || m_records.size() >= m_maximumFrames) {
        return;
    }
    FrameRecord record;
    record.frameNumber = frameNumber;
    record.frameSeconds = frameSeconds;
    record.deltaSeconds = deltaSeconds;
    record.updateSeconds = updateSeconds;
    record.lateUpdateSeconds = lateUpdateSeconds;
    record.renderSeconds = renderSeconds;
    m_records.push_back(record);
}

bool FrameTrace::Flush() {
    if (!m_enabled || m_written) {
        return true;
    }
    if (m_records.empty()) {
        m_written = true;
        return true;
    }
    std::ofstream out(m_path, std::ios::out | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "frame,frame_ms,delta_ms,fps,update_ms,lateupdate_ms,render_ms\n";
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(6);
    for (const FrameRecord& record : m_records) {
        const double frameMilliseconds = record.frameSeconds * 1000.0;
        const double fps = record.deltaSeconds > 0.0 ? 1.0 / record.deltaSeconds : 0.0;
        out << record.frameNumber << ',' << frameMilliseconds << ','
            << record.deltaSeconds * 1000.0 << ',' << fps << ','
            << record.updateSeconds * 1000.0 << ',' << record.lateUpdateSeconds * 1000.0 << ','
            << record.renderSeconds * 1000.0 << '\n';
    }
    out.flush();
    if (!out) {
        return false;
    }
    m_written = true;
    return true;
}

} // namespace UVE::Core
