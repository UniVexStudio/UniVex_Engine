// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/diagnostics/profiler_diagnostics_uve.h"
#include "uve/diagnostics/profiler_view_uve.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Core {

TEST(ProfilerCaptureUVETest, BeginAndEndUVE_ReturnCopiedBundleWithTypedDiagnostics) {
    ProfilerCaptureUVE capture;
    ASSERT_TRUE(capture.BeginUVE("frame-42", 100U).IsAcceptedUVE());
    EXPECT_TRUE(capture.IsActiveUVE());

    ASSERT_TRUE(capture.RecordSpanUVE(DiagnosticCategoryUVE::Animation, "evaluate", 110U, 140U,
                                      7U, 42U)
                    .IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordCounterUVE(DiagnosticCategoryUVE::Memory, "active-bytes", 145U,
                                         2048.0, 7U, 42U)
                    .IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::Task, "animation ready", 150U,
                                            7U, 42U)
                    .IsAcceptedUVE());

    DiagnosticRuntimeSnapshotUVE snapshot;
    snapshot.frameNumber = 42U;
    snapshot.activeBytes = 2048U;
    snapshot.workerCount = 3U;
    snapshot.pendingTaskCount = 2U;
    ASSERT_TRUE(capture.SetRuntimeSnapshotUVE(snapshot).IsAcceptedUVE());

    const DiagnosticCaptureStopResultUVE result = capture.EndUVE(200U);
    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_FALSE(capture.IsActiveUVE());
    EXPECT_EQ(result.bundle.sessionId, "frame-42");
    EXPECT_EQ(result.bundle.beginTimestampNanoseconds, 100U);
    EXPECT_EQ(result.bundle.endTimestampNanoseconds, 200U);
    ASSERT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.spans[0].name, "evaluate");
    EXPECT_EQ(result.bundle.spans[0].category, DiagnosticCategoryUVE::Animation);
    ASSERT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_DOUBLE_EQ(result.bundle.counters[0].value, 2048.0);
    ASSERT_EQ(result.bundle.breadcrumbs.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs[0].sequence, 0U);
    EXPECT_EQ(result.bundle.runtimeSnapshot.pendingTaskCount, 2U);
}

TEST(ProfilerCaptureUVETest, RecordBreadcrumbUVE_AssignsMonotonicSequenceNumbers) {
    ProfilerCaptureUVE capture;
    ASSERT_TRUE(capture.BeginUVE("breadcrumbs", 1U).IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::Cpu, "one", 2U, 1U, 1U)
                    .IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::Cpu, "two", 3U, 1U, 1U)
                    .IsAcceptedUVE());

    const DiagnosticCaptureStopResultUVE result = capture.EndUVE(4U);
    ASSERT_TRUE(result.IsAcceptedUVE());
    ASSERT_EQ(result.bundle.breadcrumbs.size(), 2U);
    EXPECT_EQ(result.bundle.breadcrumbs[0].sequence, 0U);
    EXPECT_EQ(result.bundle.breadcrumbs[1].sequence, 1U);
}

TEST(ProfilerCaptureUVETest, CaptureLimitsUVE_DropsExcessEventsAndPreservesDiagnostics) {
    DiagnosticCaptureLimitsUVE limits;
    limits.maximumSpans = 1U;
    limits.maximumCounters = 1U;
    limits.maximumBreadcrumbs = 1U;
    ProfilerCaptureUVE capture;
    ASSERT_TRUE(capture.BeginUVE("bounded", 10U, limits).IsAcceptedUVE());

    EXPECT_TRUE(capture.RecordSpanUVE(DiagnosticCategoryUVE::Render, "pass", 11U, 12U, 1U, 2U)
                    .IsAcceptedUVE());
    EXPECT_EQ(capture.RecordSpanUVE(DiagnosticCategoryUVE::Render, "pass-2", 13U, 14U, 1U, 2U).code,
              DiagnosticCaptureCodeUVE::CapacityExceeded);
    EXPECT_TRUE(capture.RecordCounterUVE(DiagnosticCategoryUVE::ECS, "entities", 15U, 4.0, 1U, 2U)
                    .IsAcceptedUVE());
    EXPECT_EQ(capture.RecordCounterUVE(DiagnosticCategoryUVE::ECS, "entities-2", 16U, 5.0, 1U, 2U).code,
              DiagnosticCaptureCodeUVE::CapacityExceeded);
    EXPECT_TRUE(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::VM, "step", 17U, 1U, 2U)
                    .IsAcceptedUVE());
    EXPECT_EQ(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::VM, "step-2", 18U, 1U, 2U).code,
              DiagnosticCaptureCodeUVE::CapacityExceeded);

    const DiagnosticCaptureStopResultUVE result = capture.EndUVE(20U);
    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs.size(), 1U);
    EXPECT_EQ(result.bundle.droppedSpanCount, 1U);
    EXPECT_EQ(result.bundle.droppedCounterCount, 1U);
    EXPECT_EQ(result.bundle.droppedBreadcrumbCount, 1U);
}

TEST(ProfilerCaptureUVETest, CaptureLifecycleUVE_RejectsInvalidOrOutOfOrderOperations) {
    ProfilerCaptureUVE capture;
    EXPECT_EQ(capture.RecordCounterUVE(DiagnosticCategoryUVE::Cpu, "before", 1U, 1.0, 1U, 1U).code,
              DiagnosticCaptureCodeUVE::NotActive);

    DiagnosticCaptureLimitsUVE invalidLimits;
    invalidLimits.maximumSpans = 0U;
    EXPECT_EQ(capture.BeginUVE("invalid-limits", 1U, invalidLimits).code,
              DiagnosticCaptureCodeUVE::InvalidLimits);
    EXPECT_EQ(capture.BeginUVE("", 1U).code, DiagnosticCaptureCodeUVE::InvalidSession);

    ASSERT_TRUE(capture.BeginUVE("valid", 10U).IsAcceptedUVE());
    EXPECT_EQ(capture.BeginUVE("second", 11U).code, DiagnosticCaptureCodeUVE::AlreadyActive);
    EXPECT_EQ(capture.RecordSpanUVE(DiagnosticCategoryUVE::Animation, "bad", 20U, 19U, 1U, 1U).code,
              DiagnosticCaptureCodeUVE::InvalidTimestamp);
    EXPECT_EQ(capture.RecordCounterUVE(DiagnosticCategoryUVE::Animation, "nan", 20U,
                                       std::numeric_limits<double>::quiet_NaN(), 1U, 1U)
                   .code,
              DiagnosticCaptureCodeUVE::InvalidValue);
    EXPECT_EQ(capture.EndUVE(9U).code, DiagnosticCaptureCodeUVE::InvalidTimestamp);
    EXPECT_TRUE(capture.EndUVE(20U).IsAcceptedUVE());
    EXPECT_EQ(capture.EndUVE(21U).code, DiagnosticCaptureCodeUVE::NotActive);
}


TEST(ProfilerCaptureUVETest, SnapshotUVE_SeesInProgressWithoutEnding) {
    ProfilerCaptureUVE capture;
    ASSERT_TRUE(capture.BeginUVE("live", 100U).IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordSpanUVE(DiagnosticCategoryUVE::Cpu, "frame", 110U, 140U, 7U, 42U)
                    .IsAcceptedUVE());
    ASSERT_TRUE(capture.RecordCounterUVE(DiagnosticCategoryUVE::Net, "net.sent", 145U, 3.0, 7U,
                                         42U)
                    .IsAcceptedUVE());

    const DiagnosticCaptureBundleUVE live = capture.SnapshotUVE();
    EXPECT_EQ(live.sessionId, "live");
    ASSERT_EQ(live.spans.size(), 1U);
    EXPECT_EQ(live.spans[0].name, "frame");
    ASSERT_EQ(live.counters.size(), 1U);
    EXPECT_EQ(live.counters[0].category, DiagnosticCategoryUVE::Net);
    EXPECT_TRUE(capture.IsActiveUVE());

    ASSERT_TRUE(capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::Cpu, "late", 150U, 7U, 42U)
                    .IsAcceptedUVE());
    const DiagnosticCaptureStopResultUVE result = capture.EndUVE(200U);
    ASSERT_TRUE(result.IsAcceptedUVE());
    EXPECT_EQ(result.bundle.spans.size(), 1U);
    EXPECT_EQ(result.bundle.counters.size(), 1U);
    EXPECT_EQ(result.bundle.breadcrumbs.size(), 1U);
    // End moved the bundle out: the aftermath snapshots empty.
    EXPECT_TRUE(capture.SnapshotUVE().sessionId.empty());
    EXPECT_TRUE(capture.SnapshotUVE().spans.empty());
}

TEST(ProfilerCaptureUVETest, SnapshotUVE_InactiveIsEmpty) {
    ProfilerCaptureUVE capture;
    const DiagnosticCaptureBundleUVE empty = capture.SnapshotUVE();
    EXPECT_TRUE(empty.sessionId.empty());
    EXPECT_TRUE(empty.spans.empty());
    EXPECT_TRUE(empty.counters.empty());
    EXPECT_TRUE(empty.breadcrumbs.empty());
}

TEST(ProfilerCaptureUVETest, NetCategoryUVE_AcceptedEverywhere) {
    ProfilerCaptureUVE capture;
    ASSERT_TRUE(capture.BeginUVE("net", 1U).IsAcceptedUVE());
    EXPECT_TRUE(capture.RecordSpanUVE(DiagnosticCategoryUVE::Net, "send", 2U, 5U, 1U, 1U)
                    .IsAcceptedUVE());
    EXPECT_TRUE(capture.RecordCounterUVE(DiagnosticCategoryUVE::Net, "net.sent", 3U, 1.0, 1U, 1U)
                    .IsAcceptedUVE());
    EXPECT_TRUE(
        capture.RecordBreadcrumbUVE(DiagnosticCategoryUVE::Net, "linked", 4U, 1U, 1U)
            .IsAcceptedUVE());
    EXPECT_EQ(capture.RecordSpanUVE(DiagnosticCategoryUVE::Count, "nope", 2U, 5U, 1U, 1U).code,
              DiagnosticCaptureCodeUVE::InvalidCategory);
}

TEST(ProfilerViewUVETest, CategoryNameUVE_NamesEveryCategory) {
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Cpu), "cpu");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Task), "task");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Memory), "memory");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Animation), "animation");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::ECS), "ecs");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::VM), "vm");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Render), "render");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Audio), "audio");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Streaming), "streaming");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Net), "net");
    EXPECT_EQ(ProfilerCategoryNameUVE(DiagnosticCategoryUVE::Count), "unknown");
    EXPECT_EQ(ProfilerCategoryNameUVE(static_cast<DiagnosticCategoryUVE>(250U)), "unknown");
}

TEST(ProfilerViewUVETest, DurationUVE_FormatsByMagnitude) {
    EXPECT_EQ(FormatProfilerDurationUVE(0U), "0ns");
    EXPECT_EQ(FormatProfilerDurationUVE(999U), "999ns");
    EXPECT_EQ(FormatProfilerDurationUVE(1000U), "1.00us");
    EXPECT_EQ(FormatProfilerDurationUVE(1500U), "1.50us");
    EXPECT_EQ(FormatProfilerDurationUVE(999999U), "1000.00us");
    EXPECT_EQ(FormatProfilerDurationUVE(1000000U), "1.00ms");
    EXPECT_EQ(FormatProfilerDurationUVE(3400000U), "3.40ms");
    EXPECT_EQ(FormatProfilerDurationUVE(1000000000U), "1.00s");
    EXPECT_EQ(FormatProfilerDurationUVE(2500000000U), "2.50s");
}

TEST(ProfilerViewUVETest, SpanRowsUVE_SortByBeginAndSaturate) {
    DiagnosticSpanUVE late;
    late.category = DiagnosticCategoryUVE::Render;
    late.name = "present";
    late.beginTimestampNanoseconds = 200U;
    late.endTimestampNanoseconds = 260U;
    DiagnosticSpanUVE early;
    early.category = DiagnosticCategoryUVE::Cpu;
    early.name = "frame";
    early.beginTimestampNanoseconds = 100U;
    early.endTimestampNanoseconds = 150U;
    DiagnosticSpanUVE broken;
    broken.category = DiagnosticCategoryUVE::Net;
    broken.name = "send";
    broken.beginTimestampNanoseconds = 300U;
    broken.endTimestampNanoseconds = 250U;
    const std::vector<ProfilerSpanRowUVE> rows =
        BuildProfilerSpanRowsUVE({late, early, broken});
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].name, "frame");
    EXPECT_EQ(rows[0].category, DiagnosticCategoryUVE::Cpu);
    EXPECT_EQ(rows[0].beginNanoseconds, 100U);
    EXPECT_EQ(rows[0].durationNanoseconds, 50U);
    EXPECT_EQ(rows[1].name, "present");
    EXPECT_EQ(rows[1].durationNanoseconds, 60U);
    EXPECT_EQ(rows[2].name, "send");
    EXPECT_EQ(rows[2].durationNanoseconds, 0U);
}

TEST(ProfilerViewUVETest, CounterRowsUVE_KeepLatestPerName) {
    DiagnosticCounterUVE stale;
    stale.category = DiagnosticCategoryUVE::Net;
    stale.name = "net.sent";
    stale.timestampNanoseconds = 10U;
    stale.value = 1.0;
    DiagnosticCounterUVE fresh;
    fresh.category = DiagnosticCategoryUVE::Net;
    fresh.name = "net.sent";
    fresh.timestampNanoseconds = 20U;
    fresh.value = 2.0;
    DiagnosticCounterUVE other;
    other.category = DiagnosticCategoryUVE::Memory;
    other.name = "net.sent";
    other.timestampNanoseconds = 30U;
    other.value = 9.0;
    const std::vector<ProfilerCounterRowUVE> rows =
        BuildProfilerCounterRowsUVE({stale, other, fresh});
    ASSERT_EQ(rows.size(), 2U);
    // Sorted by (category, name): memory (2) before net (9).
    EXPECT_EQ(rows[0].category, DiagnosticCategoryUVE::Memory);
    EXPECT_DOUBLE_EQ(rows[0].value, 9.0);
    EXPECT_EQ(rows[1].category, DiagnosticCategoryUVE::Net);
    EXPECT_DOUBLE_EQ(rows[1].value, 2.0);
    EXPECT_EQ(rows[1].timestampNanoseconds, 20U);
}

TEST(ProfilerViewUVETest, CounterUVE_PrintsWholeWhenIntegral) {
    EXPECT_EQ(FormatProfilerCounterUVE(2048.0), "2048");
    EXPECT_EQ(FormatProfilerCounterUVE(0.0), "0");
    EXPECT_EQ(FormatProfilerCounterUVE(-3.0), "-3");
    EXPECT_EQ(FormatProfilerCounterUVE(1.5), "1.500");
    EXPECT_EQ(FormatProfilerCounterUVE(0.123456), "0.123");
}
} // namespace UVE::Core
