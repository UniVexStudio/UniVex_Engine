// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/// Live diagnostics capture: bounded spans, counters and breadcrumbs by
/// `DiagnosticCategoryUVE`, behind the editor's Profiler dock and the per-system emitters.
///
/// THREE DELIBERATE CONSTRAINTS. First, the capture is bounded (`DiagnosticCaptureLimitsUVE`:
/// 8K spans, 4K counters, 2K breadcrumbs) with explicit dropped-record counters rather than
/// unbounded growth or silent loss — an overloaded capture reports what it dropped. Second, the
/// module owns no clock: timestamps are caller-supplied nanoseconds, which keeps captures
/// deterministic and testable (tests hand in synthetic timestamps). Third, the module depends
/// on zero `uve/*` targets (see its CMakeLists): it sits below everything so anything can emit
/// without a layering edge.
///
/// The one visible consequence of the third constraint is that thread ids are raw `std::uint64_t`
/// here, not `Threading::ThreadIdUVE` — naming that type would be the forbidden edge. The bridge
/// is `ThreadIdUVE::ValueUVE()` at emission and `ThreadIdUVE{value}` for keying maps on the
/// consuming side; thread id 0 keeps its "unknown" meaning on both sides of the bridge.
namespace UVE::Core {

enum class DiagnosticCategoryUVE : std::uint8_t {
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

struct DiagnosticCaptureLimitsUVE final {
    static constexpr std::size_t kMaximumSpansUVE = 8192U;
    static constexpr std::size_t kMaximumCountersUVE = 4096U;
    static constexpr std::size_t kMaximumBreadcrumbsUVE = 2048U;

    std::size_t maximumSpans = kMaximumSpansUVE;
    std::size_t maximumCounters = kMaximumCountersUVE;
    std::size_t maximumBreadcrumbs = kMaximumBreadcrumbsUVE;
};

struct DiagnosticRuntimeSnapshotUVE final {
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

struct DiagnosticSpanUVE final {
    DiagnosticCategoryUVE category = DiagnosticCategoryUVE::Cpu;
    std::string name;
    std::uint64_t beginTimestampNanoseconds = 0U;
    std::uint64_t endTimestampNanoseconds = 0U;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticCounterUVE final {
    DiagnosticCategoryUVE category = DiagnosticCategoryUVE::Cpu;
    std::string name;
    std::uint64_t timestampNanoseconds = 0U;
    double value = 0.0;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticBreadcrumbUVE final {
    std::uint64_t sequence = 0U;
    DiagnosticCategoryUVE category = DiagnosticCategoryUVE::Cpu;
    std::string message;
    std::uint64_t timestampNanoseconds = 0U;
    std::uint64_t threadId = 0U;
    std::uint64_t frameNumber = 0U;
};

struct DiagnosticCaptureBundleUVE final {
    std::string sessionId;
    std::uint64_t beginTimestampNanoseconds = 0U;
    std::uint64_t endTimestampNanoseconds = 0U;
    std::vector<DiagnosticSpanUVE> spans;
    std::vector<DiagnosticCounterUVE> counters;
    std::vector<DiagnosticBreadcrumbUVE> breadcrumbs;
    DiagnosticRuntimeSnapshotUVE runtimeSnapshot;
    std::size_t droppedSpanCount = 0U;
    std::size_t droppedCounterCount = 0U;
    std::size_t droppedBreadcrumbCount = 0U;
};

enum class DiagnosticCaptureCodeUVE : std::uint8_t {
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

struct DiagnosticCaptureResultUVE final {
    DiagnosticCaptureCodeUVE code = DiagnosticCaptureCodeUVE::NotActive;
    std::string message;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return code == DiagnosticCaptureCodeUVE::Accepted;
    }
};

struct DiagnosticCaptureStopResultUVE final {
    DiagnosticCaptureCodeUVE code = DiagnosticCaptureCodeUVE::NotActive;
    DiagnosticCaptureBundleUVE bundle;
    std::string message;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return code == DiagnosticCaptureCodeUVE::Accepted;
    }
};

class ProfilerCaptureUVE final {
public:
    static constexpr std::size_t kMaximumSessionIdBytesUVE = 128U;
    static constexpr std::size_t kMaximumNameBytesUVE = 128U;
    static constexpr std::size_t kMaximumMessageBytesUVE = 256U;

    ProfilerCaptureUVE() = default;
    ProfilerCaptureUVE(const ProfilerCaptureUVE&) = delete;
    ProfilerCaptureUVE& operator=(const ProfilerCaptureUVE&) = delete;

    [[nodiscard]] DiagnosticCaptureResultUVE BeginUVE(
        std::string_view sessionId, std::uint64_t beginTimestampNanoseconds,
        DiagnosticCaptureLimitsUVE limits = {});

    [[nodiscard]] DiagnosticCaptureResultUVE RecordSpanUVE(
        DiagnosticCategoryUVE category, std::string_view name,
        std::uint64_t beginTimestampNanoseconds, std::uint64_t endTimestampNanoseconds,
        std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResultUVE RecordCounterUVE(
        DiagnosticCategoryUVE category, std::string_view name, std::uint64_t timestampNanoseconds,
        double value, std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResultUVE RecordBreadcrumbUVE(
        DiagnosticCategoryUVE category, std::string_view message, std::uint64_t timestampNanoseconds,
        std::uint64_t threadId, std::uint64_t frameNumber);

    [[nodiscard]] DiagnosticCaptureResultUVE SetRuntimeSnapshotUVE(
        DiagnosticRuntimeSnapshotUVE snapshot);

    [[nodiscard]] DiagnosticCaptureStopResultUVE EndUVE(std::uint64_t endTimestampNanoseconds);

    /// Copies the in-progress capture without ending it: the live panel's feed. Empty
    /// when no capture is active (End moves the bundle out, so a completed session
    /// snapshots empty too - keep the End result to show it).
    [[nodiscard]] DiagnosticCaptureBundleUVE SnapshotUVE() const;

    [[nodiscard]] bool IsActiveUVE() const noexcept;

private:
    mutable std::mutex m_mutex;
    bool m_active = false;
    DiagnosticCaptureLimitsUVE m_limits;
    DiagnosticCaptureBundleUVE m_bundle;
    std::size_t m_nextBreadcrumbSequence = 0U;
};

} // namespace UVE::Core
