// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "profiler_diagnostics.h"
#include "profiler_view.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Core {

TEST(ProfilerCaptureTest, BeginAndEnd_ReturnCopiedBundleWithTypedDiagnostics) {
    ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("frame-42", 100U).IsAccepted());
    EXPECT_TRUE(capture.IsActive());

    ASSERT_TRUE(capture.RecordSpan(DiagnosticCategory::Animation, "evaluate", 110U, 140U,
                                      7U, 42U)
                    .IsAccepted());
    ASSERT_TRUE(capture.RecordCounter(DiagnosticCategory::Memory, "active-bytes", 145U,
                                         2048.0, 7U, 42U)
                    .IsAccepted());
    ASSERT_TRUE(capture.RecordBreadcrumb(DiagnosticCategory::Task, "animation ready", 150U,
                                            7U, 42U)
                    .IsAccepted());

    DiagnosticRuntimeSnapshot snapshot;
    snapshot.frameNumber = 42U;
    snapshot.activeBytes = 2048U;
    snapshot.workerCount = 3U;
    snapshot.pendingTaskCount = 2U;
    ASSERT_TRUE(capture.SetRuntimeSnapshot(snapshot).IsAccepted());

    const DiagnosticCaptureStopResult result = capture.End(200U);
    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(capture.IsActive());
    EXPECT_EQ(result.bundle.sessionId, "frame-42");
    EXPECT_EQ(result.bundle.beginTimestampNanoseconds, 100U);
    EXPECT_EQ(result.bundle.endTimestampNanoseconds, 200U);
    ASSERT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.spans[0].name, "evaluate");
    EXPECT_EQ(result.bundle.spans[0].category, DiagnosticCategory::Animation);
    ASSERT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_DOUBLE_EQ(result.bundle.counters[0].value, 2048.0);
    ASSERT_EQ(result.bundle.breadcrumbs.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs[0].sequence, 0U);
    EXPECT_EQ(result.bundle.runtimeSnapshot.pendingTaskCount, 2U);
}

TEST(ProfilerCaptureTest, RecordBreadcrumb_AssignsMonotonicSequenceNumbers) {
    ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("breadcrumbs", 1U).IsAccepted());
    ASSERT_TRUE(capture.RecordBreadcrumb(DiagnosticCategory::Cpu, "one", 2U, 1U, 1U)
                    .IsAccepted());
    ASSERT_TRUE(capture.RecordBreadcrumb(DiagnosticCategory::Cpu, "two", 3U, 1U, 1U)
                    .IsAccepted());

    const DiagnosticCaptureStopResult result = capture.End(4U);
    ASSERT_TRUE(result.IsAccepted());
    ASSERT_EQ(result.bundle.breadcrumbs.size(), 2U);
    EXPECT_EQ(result.bundle.breadcrumbs[0].sequence, 0U);
    EXPECT_EQ(result.bundle.breadcrumbs[1].sequence, 1U);
}

TEST(ProfilerCaptureTest, CaptureLimits_DropsExcessEventsAndPreservesDiagnostics) {
    DiagnosticCaptureLimits limits;
    limits.maximumSpans = 1U;
    limits.maximumCounters = 1U;
    limits.maximumBreadcrumbs = 1U;
    ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("bounded", 10U, limits).IsAccepted());

    EXPECT_TRUE(capture.RecordSpan(DiagnosticCategory::Render, "pass", 11U, 12U, 1U, 2U)
                    .IsAccepted());
    EXPECT_EQ(capture.RecordSpan(DiagnosticCategory::Render, "pass-2", 13U, 14U, 1U, 2U).code,
              DiagnosticCaptureCode::CapacityExceeded);
    EXPECT_TRUE(capture.RecordCounter(DiagnosticCategory::ECS, "entities", 15U, 4.0, 1U, 2U)
                    .IsAccepted());
    EXPECT_EQ(capture.RecordCounter(DiagnosticCategory::ECS, "entities-2", 16U, 5.0, 1U, 2U).code,
              DiagnosticCaptureCode::CapacityExceeded);
    EXPECT_TRUE(capture.RecordBreadcrumb(DiagnosticCategory::VM, "step", 17U, 1U, 2U)
                    .IsAccepted());
    EXPECT_EQ(capture.RecordBreadcrumb(DiagnosticCategory::VM, "step-2", 18U, 1U, 2U).code,
              DiagnosticCaptureCode::CapacityExceeded);

    const DiagnosticCaptureStopResult result = capture.End(20U);
    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs.size(), 1U);
    EXPECT_EQ(result.bundle.droppedSpanCount, 1U);
    EXPECT_EQ(result.bundle.droppedCounterCount, 1U);
    EXPECT_EQ(result.bundle.droppedBreadcrumbCount, 1U);
}

TEST(ProfilerCaptureTest, CaptureLifecycle_RejectsInvalidOrOutOfOrderOperations) {
    ProfilerCapture capture;
    EXPECT_EQ(capture.RecordCounter(DiagnosticCategory::Cpu, "before", 1U, 1.0, 1U, 1U).code,
              DiagnosticCaptureCode::NotActive);

    DiagnosticCaptureLimits invalidLimits;
    invalidLimits.maximumSpans = 0U;
    EXPECT_EQ(capture.Begin("invalid-limits", 1U, invalidLimits).code,
              DiagnosticCaptureCode::InvalidLimits);
    EXPECT_EQ(capture.Begin("", 1U).code, DiagnosticCaptureCode::InvalidSession);

    ASSERT_TRUE(capture.Begin("valid", 10U).IsAccepted());
    EXPECT_EQ(capture.Begin("second", 11U).code, DiagnosticCaptureCode::AlreadyActive);
    EXPECT_EQ(capture.RecordSpan(DiagnosticCategory::Animation, "bad", 20U, 19U, 1U, 1U).code,
              DiagnosticCaptureCode::InvalidTimestamp);
    EXPECT_EQ(capture.RecordCounter(DiagnosticCategory::Animation, "nan", 20U,
                                       std::numeric_limits<double>::quiet_NaN(), 1U, 1U)
                   .code,
              DiagnosticCaptureCode::InvalidValue);
    EXPECT_EQ(capture.End(9U).code, DiagnosticCaptureCode::InvalidTimestamp);
    EXPECT_TRUE(capture.End(20U).IsAccepted());
    EXPECT_EQ(capture.End(21U).code, DiagnosticCaptureCode::NotActive);
}


TEST(ProfilerCaptureTest, Snapshot_SeesInProgressWithoutEnding) {
    ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("live", 100U).IsAccepted());
    ASSERT_TRUE(capture.RecordSpan(DiagnosticCategory::Cpu, "frame", 110U, 140U, 7U, 42U)
                    .IsAccepted());
    ASSERT_TRUE(capture.RecordCounter(DiagnosticCategory::Net, "net.sent", 145U, 3.0, 7U,
                                         42U)
                    .IsAccepted());

    const DiagnosticCaptureBundle live = capture.Snapshot();
    EXPECT_EQ(live.sessionId, "live");
    ASSERT_EQ(live.spans.size(), 1U);
    EXPECT_EQ(live.spans[0].name, "frame");
    ASSERT_EQ(live.counters.size(), 1U);
    EXPECT_EQ(live.counters[0].category, DiagnosticCategory::Net);
    EXPECT_TRUE(capture.IsActive());

    ASSERT_TRUE(capture.RecordBreadcrumb(DiagnosticCategory::Cpu, "late", 150U, 7U, 42U)
                    .IsAccepted());
    const DiagnosticCaptureStopResult result = capture.End(200U);
    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs.size(), 1U);
    // End moved the bundle out: the aftermath snapshots empty.
    EXPECT_TRUE(capture.Snapshot().sessionId.empty());
    EXPECT_TRUE(capture.Snapshot().spans.empty());
}

TEST(ProfilerCaptureTest, Snapshot_InactiveIsEmpty) {
    ProfilerCapture capture;
    const DiagnosticCaptureBundle empty = capture.Snapshot();
    EXPECT_TRUE(empty.sessionId.empty());
    EXPECT_TRUE(empty.spans.empty());
    EXPECT_TRUE(empty.counters.empty());
    EXPECT_TRUE(empty.breadcrumbs.empty());
}

TEST(ProfilerCaptureTest, NetCategory_AcceptedEverywhere) {
    ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("net", 1U).IsAccepted());
    EXPECT_TRUE(capture.RecordSpan(DiagnosticCategory::Net, "send", 2U, 5U, 1U, 1U)
                    .IsAccepted());
    EXPECT_TRUE(capture.RecordCounter(DiagnosticCategory::Net, "net.sent", 3U, 1.0, 1U, 1U)
                    .IsAccepted());
    EXPECT_TRUE(
        capture.RecordBreadcrumb(DiagnosticCategory::Net, "linked", 4U, 1U, 1U)
            .IsAccepted());
    EXPECT_EQ(capture.RecordSpan(DiagnosticCategory::Count, "nope", 2U, 5U, 1U, 1U).code,
              DiagnosticCaptureCode::InvalidCategory);
}

TEST(ProfilerViewTest, CategoryName_NamesEveryCategory) {
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Cpu), "cpu");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Task), "task");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Memory), "memory");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Animation), "animation");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::ECS), "ecs");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::VM), "vm");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Render), "render");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Audio), "audio");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Streaming), "streaming");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Net), "net");
    EXPECT_EQ(ProfilerCategoryName(DiagnosticCategory::Count), "unknown");
    EXPECT_EQ(ProfilerCategoryName(static_cast<DiagnosticCategory>(250U)), "unknown");
}

TEST(ProfilerViewTest, Duration_FormatsByMagnitude) {
    EXPECT_EQ(FormatProfilerDuration(0U), "0ns");
    EXPECT_EQ(FormatProfilerDuration(999U), "999ns");
    EXPECT_EQ(FormatProfilerDuration(1000U), "1.00us");
    EXPECT_EQ(FormatProfilerDuration(1500U), "1.50us");
    EXPECT_EQ(FormatProfilerDuration(999999U), "1000.00us");
    EXPECT_EQ(FormatProfilerDuration(1000000U), "1.00ms");
    EXPECT_EQ(FormatProfilerDuration(3400000U), "3.40ms");
    EXPECT_EQ(FormatProfilerDuration(1000000000U), "1.00s");
    EXPECT_EQ(FormatProfilerDuration(2500000000U), "2.50s");
}

TEST(ProfilerViewTest, SpanRows_SortByBeginAndSaturate) {
    DiagnosticSpan late;
    late.category = DiagnosticCategory::Render;
    late.name = "present";
    late.beginTimestampNanoseconds = 200U;
    late.endTimestampNanoseconds = 260U;
    DiagnosticSpan early;
    early.category = DiagnosticCategory::Cpu;
    early.name = "frame";
    early.beginTimestampNanoseconds = 100U;
    early.endTimestampNanoseconds = 150U;
    DiagnosticSpan broken;
    broken.category = DiagnosticCategory::Net;
    broken.name = "send";
    broken.beginTimestampNanoseconds = 300U;
    broken.endTimestampNanoseconds = 250U;
    const std::vector<ProfilerSpanRow> rows =
        BuildProfilerSpanRows({late, early, broken});
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].name, "frame");
    EXPECT_EQ(rows[0].category, DiagnosticCategory::Cpu);
    EXPECT_EQ(rows[0].beginNanoseconds, 100U);
    EXPECT_EQ(rows[0].durationNanoseconds, 50U);
    EXPECT_EQ(rows[1].name, "present");
    EXPECT_EQ(rows[1].durationNanoseconds, 60U);
    EXPECT_EQ(rows[2].name, "send");
    EXPECT_EQ(rows[2].durationNanoseconds, 0U);
}

TEST(ProfilerViewTest, CounterRows_KeepLatestPerName) {
    DiagnosticCounter stale;
    stale.category = DiagnosticCategory::Net;
    stale.name = "net.sent";
    stale.timestampNanoseconds = 10U;
    stale.value = 1.0;
    DiagnosticCounter fresh;
    fresh.category = DiagnosticCategory::Net;
    fresh.name = "net.sent";
    fresh.timestampNanoseconds = 20U;
    fresh.value = 2.0;
    DiagnosticCounter other;
    other.category = DiagnosticCategory::Memory;
    other.name = "net.sent";
    other.timestampNanoseconds = 30U;
    other.value = 9.0;
    const std::vector<ProfilerCounterRow> rows =
        BuildProfilerCounterRows({stale, other, fresh});
    ASSERT_EQ(rows.size(), 2U);
    // Sorted by (category, name): memory (2) before net (9).
    EXPECT_EQ(rows[0].category, DiagnosticCategory::Memory);
    EXPECT_DOUBLE_EQ(rows[0].value, 9.0);
    EXPECT_EQ(rows[1].category, DiagnosticCategory::Net);
    EXPECT_DOUBLE_EQ(rows[1].value, 2.0);
    EXPECT_EQ(rows[1].timestampNanoseconds, 20U);
}

TEST(ProfilerViewTest, Counter_PrintsWholeWhenIntegral) {
    EXPECT_EQ(FormatProfilerCounter(2048.0), "2048");
    EXPECT_EQ(FormatProfilerCounter(0.0), "0");
    EXPECT_EQ(FormatProfilerCounter(-3.0), "-3");
    EXPECT_EQ(FormatProfilerCounter(1.5), "1.500");
    EXPECT_EQ(FormatProfilerCounter(0.123456), "0.123");
}
} // namespace UVE::Core
