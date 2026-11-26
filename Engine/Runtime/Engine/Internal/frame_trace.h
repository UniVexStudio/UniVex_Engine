// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

/// Frame-time trace for benchmarking: one CSV row per frame, written once at Flush().
///
/// Disabled unless the environment variable UVE_FRAME_TRACE names the CSV path, so an ordinary
/// run pays one cached bool load per frame. Rows are buffered in memory (bounded by
/// UVE_FRAME_TRACE_MAX, default 20000) and written in a single pass during shutdown, which keeps
/// file I/O out of the frames being measured.
class FrameTrace final {
public:
    static constexpr std::size_t kDefaultMaximumFrames = 20000U;

    static FrameTrace& Instance();

    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
    [[nodiscard]] std::size_t FrameCount() const noexcept { return m_records.size(); }

    void Record(std::uint64_t frameNumber, double frameSeconds, double deltaSeconds,
                double updateSeconds, double lateUpdateSeconds, double renderSeconds);

    /// Writes the buffered rows. Returns false when a path was requested but could not be
    /// written; a second call after a successful write does nothing and returns true. A trace
    /// that recorded no frames writes no file, so an engine that never ticked leaves no artifact.
    bool Flush();

private:
    struct FrameRecord final {
        std::uint64_t frameNumber = 0U;
        double frameSeconds = 0.0;
        double deltaSeconds = 0.0;
        double updateSeconds = 0.0;
        double lateUpdateSeconds = 0.0;
        double renderSeconds = 0.0;
    };

    FrameTrace();
    ~FrameTrace();
    FrameTrace(const FrameTrace&) = delete;
    FrameTrace& operator=(const FrameTrace&) = delete;

    bool m_enabled = false;
    bool m_written = false;
    std::size_t m_maximumFrames = kDefaultMaximumFrames;
    std::string m_path;
    std::vector<FrameRecord> m_records;
};

} // namespace UVE::Core
