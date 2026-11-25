// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "profiler_diagnostics.h"

#include <cmath>
#include <utility>

namespace UVE::Core {
namespace {

[[nodiscard]] bool IsValidCategory(DiagnosticCategory category) noexcept {
    return static_cast<std::uint8_t>(category) <
           static_cast<std::uint8_t>(DiagnosticCategory::Count);
}

[[nodiscard]] bool IsValidLimits(const DiagnosticCaptureLimits& limits) noexcept {
    return limits.maximumSpans > 0U &&
           limits.maximumSpans <= DiagnosticCaptureLimits::kMaximumSpans &&
           limits.maximumCounters > 0U &&
           limits.maximumCounters <= DiagnosticCaptureLimits::kMaximumCounters &&
           limits.maximumBreadcrumbs > 0U &&
           limits.maximumBreadcrumbs <= DiagnosticCaptureLimits::kMaximumBreadcrumbs;
}

[[nodiscard]] DiagnosticCaptureResult MakeResult(DiagnosticCaptureCode code,
                                                        const char* message) {
    return DiagnosticCaptureResult{code, message};
}

} // namespace

DiagnosticCaptureResult ProfilerCapture::Begin(
    std::string_view sessionId, std::uint64_t beginTimestampNanoseconds,
    DiagnosticCaptureLimits limits) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_active) {
        return MakeResult(DiagnosticCaptureCode::AlreadyActive,
                             "diagnostic capture session is already active");
    }
    if (sessionId.empty() || sessionId.size() > kMaximumSessionIdBytes) {
        return MakeResult(DiagnosticCaptureCode::InvalidSession,
                             "diagnostic capture session identifier is empty or too long");
    }
    if (!IsValidLimits(limits)) {
        return MakeResult(DiagnosticCaptureCode::InvalidLimits,
                             "diagnostic capture limits are outside bounded capacity");
    }

    m_limits = limits;
    m_bundle = DiagnosticCaptureBundle{};
    m_bundle.sessionId = sessionId;
    m_bundle.beginTimestampNanoseconds = beginTimestampNanoseconds;
    m_nextBreadcrumbSequence = 0U;
    m_active = true;
    return MakeResult(DiagnosticCaptureCode::Accepted, "accepted");
}

DiagnosticCaptureResult ProfilerCapture::RecordSpan(
    DiagnosticCategory category, std::string_view name, std::uint64_t beginTimestampNanoseconds,
    std::uint64_t endTimestampNanoseconds, std::uint64_t threadId, std::uint64_t frameNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) {
        return MakeResult(DiagnosticCaptureCode::NotActive,
                             "diagnostic capture session is not active");
    }
    if (!IsValidCategory(category)) {
        return MakeResult(DiagnosticCaptureCode::InvalidCategory,
                             "diagnostic span category is invalid");
    }
    if (name.empty() || name.size() > kMaximumNameBytes) {
        return MakeResult(DiagnosticCaptureCode::InvalidName,
                             "diagnostic span name is empty or too long");
    }
    if (endTimestampNanoseconds < beginTimestampNanoseconds) {
        return MakeResult(DiagnosticCaptureCode::InvalidTimestamp,
                             "diagnostic span end precedes its begin timestamp");
    }
    if (m_bundle.spans.size() >= m_limits.maximumSpans) {
        ++m_bundle.droppedSpanCount;
        return MakeResult(DiagnosticCaptureCode::CapacityExceeded,
                             "diagnostic span capacity exceeded; event was dropped");
    }

    m_bundle.spans.push_back(DiagnosticSpan{category, std::string(name),
                                               beginTimestampNanoseconds,
                                               endTimestampNanoseconds, threadId, frameNumber});
    return MakeResult(DiagnosticCaptureCode::Accepted, "accepted");
}

DiagnosticCaptureResult ProfilerCapture::RecordCounter(
    DiagnosticCategory category, std::string_view name, std::uint64_t timestampNanoseconds,
    double value, std::uint64_t threadId, std::uint64_t frameNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) {
        return MakeResult(DiagnosticCaptureCode::NotActive,
                             "diagnostic capture session is not active");
    }
    if (!IsValidCategory(category)) {
        return MakeResult(DiagnosticCaptureCode::InvalidCategory,
                             "diagnostic counter category is invalid");
    }
    if (name.empty() || name.size() > kMaximumNameBytes) {
        return MakeResult(DiagnosticCaptureCode::InvalidName,
                             "diagnostic counter name is empty or too long");
    }
    if (!std::isfinite(value)) {
        return MakeResult(DiagnosticCaptureCode::InvalidValue,
                             "diagnostic counter value must be finite");
    }
    if (m_bundle.counters.size() >= m_limits.maximumCounters) {
        ++m_bundle.droppedCounterCount;
        return MakeResult(DiagnosticCaptureCode::CapacityExceeded,
                             "diagnostic counter capacity exceeded; event was dropped");
    }

    m_bundle.counters.push_back(DiagnosticCounter{category, std::string(name),
                                                     timestampNanoseconds, value, threadId,
                                                     frameNumber});
    return MakeResult(DiagnosticCaptureCode::Accepted, "accepted");
}

DiagnosticCaptureResult ProfilerCapture::RecordBreadcrumb(
    DiagnosticCategory category, std::string_view message, std::uint64_t timestampNanoseconds,
    std::uint64_t threadId, std::uint64_t frameNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) {
        return MakeResult(DiagnosticCaptureCode::NotActive,
                             "diagnostic capture session is not active");
    }
    if (!IsValidCategory(category)) {
        return MakeResult(DiagnosticCaptureCode::InvalidCategory,
                             "diagnostic breadcrumb category is invalid");
    }
    if (message.empty() || message.size() > kMaximumMessageBytes) {
        return MakeResult(DiagnosticCaptureCode::InvalidMessage,
                             "diagnostic breadcrumb message is empty or too long");
    }
    if (m_bundle.breadcrumbs.size() >= m_limits.maximumBreadcrumbs) {
        ++m_bundle.droppedBreadcrumbCount;
        return MakeResult(DiagnosticCaptureCode::CapacityExceeded,
                             "diagnostic breadcrumb capacity exceeded; event was dropped");
    }

    m_bundle.breadcrumbs.push_back(DiagnosticBreadcrumb{
        m_nextBreadcrumbSequence++, category, std::string(message), timestampNanoseconds,
        threadId, frameNumber});
    return MakeResult(DiagnosticCaptureCode::Accepted, "accepted");
}

DiagnosticCaptureResult ProfilerCapture::SetRuntimeSnapshot(
    DiagnosticRuntimeSnapshot snapshot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) {
        return MakeResult(DiagnosticCaptureCode::NotActive,
                             "diagnostic capture session is not active");
    }
    m_bundle.runtimeSnapshot = snapshot;
    return MakeResult(DiagnosticCaptureCode::Accepted, "accepted");
}

DiagnosticCaptureStopResult ProfilerCapture::End(
    std::uint64_t endTimestampNanoseconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) {
        return DiagnosticCaptureStopResult{DiagnosticCaptureCode::NotActive, {},
                                              "diagnostic capture session is not active"};
    }
    if (endTimestampNanoseconds < m_bundle.beginTimestampNanoseconds) {
        return DiagnosticCaptureStopResult{DiagnosticCaptureCode::InvalidTimestamp, {},
                                              "diagnostic capture end precedes begin timestamp"};
    }

    m_bundle.endTimestampNanoseconds = endTimestampNanoseconds;
    DiagnosticCaptureBundle completedBundle = std::move(m_bundle);
    m_bundle = DiagnosticCaptureBundle{};
    m_active = false;
    m_nextBreadcrumbSequence = 0U;
    return DiagnosticCaptureStopResult{DiagnosticCaptureCode::Accepted,
                                          std::move(completedBundle), "accepted"};
}

DiagnosticCaptureBundle ProfilerCapture::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bundle;
}

bool ProfilerCapture::IsActive() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active;
}

} // namespace UVE::Core
