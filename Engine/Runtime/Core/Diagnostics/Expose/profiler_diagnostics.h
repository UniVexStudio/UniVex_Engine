// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/// Live diagnostics capture: bounded spans, counters and breadcrumbs by
/// `DiagnosticCategory`, behind the editor's Profiler dock and the per-system emitters.
///
/// THREE DELIBERATE CONSTRAINTS. First, the capture is bounded (`DiagnosticCaptureLimits`:
/// 8K spans, 4K counters, 2K breadcrumbs) with explicit dropped-record counters rather than
/// unbounded growth or silent loss — an overloaded capture reports what it dropped. Second, the
/// module owns no clock: timestamps are caller-supplied nanoseconds, which keeps captures
/// deterministic and testable (tests hand in synthetic timestamps). Third, the module depends
/// on zero `uve/*` targets (see its CMakeLists): it sits below everything so anything can emit
/// without a layering edge.
///
/// The one visible consequence of the third constraint is that thread ids are raw `std::uint64_t`
/// here, not `Threading::ThreadId` — naming that type would be the forbidden edge. The bridge
/// is `ThreadId::Value()` at emission and `ThreadId{value}` for keying maps on the
/// consuming side; thread id 0 keeps its "unknown" meaning on both sides of the bridge.
namespace UVE::Core {

enum class DiagnosticCategory : std::uint8_t {
    Cpu = 0,
    Task,
    Memory,
    Animation,
    ECS,
    VM,
    Render,
    Audio,
    Streaming,
    Net,
    Count,
};

struct DiagnosticCaptureLimits final {
    static constexpr std::size_t kMaximumSpans = 8192U;
    static constexpr std::size_t kMaximumCounters = 4096U;
    static constexpr std::size_t kMaximumBreadcrumbs = 2048U;

    std::size_t maximumSpans = kMaximumSpans;
    std::size_t maximumCounters = kMaximumCounters;
    std::size_t maximumBreadcrumbs = kMaximumBreadcrumbs;
};

struct DiagnosticRuntimeSnapshot final {
    std::uint64_t frameNumber = 0U;
    std::size_t activeAllocationCount = 0U;
    std::size_t activeBytes = 0U;
    std::size_t peakBytes = 0U;
    std::size_t workerCount = 0U;
    std::size_t pendingTaskCount = 0U;
    std::size_t activeWorkerCount = 0U;
    std::uint64_t stolenJobCount = 0U;
    std::uint64_t animationEvaluationCount = 0U;
    std::uint64_t ecsSystemCount = 0U;
    std::uint64_t vmInstructionCount = 0U;
    std::uint64_t renderPassCount = 0U;
};

struct DiagnosticSpan final {
    DiagnosticCategory category = DiagnosticCategory::Cpu;
    std::string name;
    std::uint64_t beginTimestampNanoseconds = 0U;
    std::uint64_t endTimestampNanoseconds = 0U;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticCounter final {
    DiagnosticCategory category = DiagnosticCategory::Cpu;
    std::string name;
    std::uint64_t timestampNanoseconds = 0U;
    double value = 0.0;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticBreadcrumb final {
    std::uint64_t sequence = 0U;
    DiagnosticCategory category = DiagnosticCategory::Cpu;
    std::string message;
    std::uint64_t timestampNanoseconds = 0U;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticCaptureBundle final {
    std::string sessionId;
    std::uint64_t beginTimestampNanoseconds = 0U;
    std::uint64_t endTimestampNanoseconds = 0U;
    std::vector<DiagnosticSpan> spans;
    std::vector<DiagnosticCounter> counters;
    std::vector<DiagnosticBreadcrumb> breadcrumbs;
    DiagnosticRuntimeSnapshot runtimeSnapshot;
    std::size_t droppedSpanCount = 0U;
    std::size_t droppedCounterCount = 0U;
    std::size_t droppedBreadcrumbCount = 0U;
};

enum class DiagnosticCaptureCode : std::uint8_t {
    Accepted = 0,
    AlreadyActive,
    NotActive,
    InvalidSession,
    InvalidLimits,
    InvalidCategory,
    InvalidName,
    InvalidMessage,
    InvalidTimestamp,
    InvalidValue,
    CapacityExceeded,
};

struct DiagnosticCaptureResult final {
    DiagnosticCaptureCode code = DiagnosticCaptureCode::NotActive;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DiagnosticCaptureCode::Accepted;
    }
};

struct DiagnosticCaptureStopResult final {
    DiagnosticCaptureCode code = DiagnosticCaptureCode::NotActive;
    DiagnosticCaptureBundle bundle;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DiagnosticCaptureCode::Accepted;
    }
};

class ProfilerCapture final {
public:
    static constexpr std::size_t kMaximumSessionIdBytes = 128U;
    static constexpr std::size_t kMaximumNameBytes = 128U;
    static constexpr std::size_t kMaximumMessageBytes = 256U;

    ProfilerCapture() = default;
    ProfilerCapture(const ProfilerCapture&) = delete;
    ProfilerCapture& operator=(const ProfilerCapture&) = delete;

    [[nodiscard]] DiagnosticCaptureResult Begin(
        std::string_view sessionId, std::uint64_t beginTimestampNanoseconds,
        DiagnosticCaptureLimits limits = {});

    [[nodiscard]] DiagnosticCaptureResult RecordSpan(
        DiagnosticCategory category, std::string_view name,
        std::uint64_t beginTimestampNanoseconds, std::uint64_t endTimestampNanoseconds,
        std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResult RecordCounter(
        DiagnosticCategory category, std::string_view name, std::uint64_t timestampNanoseconds,
        double value, std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResult RecordBreadcrumb(
        DiagnosticCategory category, std::string_view message, std::uint64_t timestampNanoseconds,
        std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResult SetRuntimeSnapshot(
        DiagnosticRuntimeSnapshot snapshot);

    [[nodiscard]] DiagnosticCaptureStopResult End(std::uint64_t endTimestampNanoseconds);

    /// Copies the in-progress capture without ending it: the live panel's feed. Empty
    /// when no capture is active (End moves the bundle out, so a completed session
    /// snapshots empty too - keep the End result to show it).
    [[nodiscard]] DiagnosticCaptureBundle Snapshot() const;

    [[nodiscard]] bool IsActive() const noexcept;

private:
    mutable std::mutex m_mutex;
    bool m_active = false;
    DiagnosticCaptureLimits m_limits;
    DiagnosticCaptureBundle m_bundle;
    std::size_t m_nextBreadcrumbSequence = 0U;
};

} // namespace UVE::Core
