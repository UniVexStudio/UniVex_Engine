// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_profiler.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

void ExpectCounter(const Core::DiagnosticCounter& counter, const std::string& name,
                       const double value, const std::uint64_t frameNumber,
                       const std::uint64_t timestampNanoseconds, const std::uint64_t threadId) {
    EXPECT_EQ(counter.name, name);
    EXPECT_EQ(counter.category, Core::DiagnosticCategory::Net);
    EXPECT_DOUBLE_EQ(counter.value, value);
    EXPECT_EQ(counter.frameNumber, frameNumber);
    EXPECT_EQ(counter.timestampNanoseconds, timestampNanoseconds);
    EXPECT_EQ(counter.threadId, threadId);
}

TEST(NetworkProfilerTest, Replication_NamesEveryField) {
    ReplicationStats stats;
    stats.snapshotsSent = 1U;
    stats.snapshotsReceived = 2U;
    stats.entriesApplied = 3U;
    stats.entriesIgnoredUnknown = 4U;
    stats.entriesTruncated = 5U;
    stats.messagesIgnoredMalformed = 6U;
    stats.entriesSkippedIrrelevant = 7U;
    stats.entriesIgnoredOwned = 8U;
    stats.deltaSnapshotsSent = 9U;
    stats.deltaSnapshotsReceived = 10U;
    stats.entriesSkippedUnchanged = 11U;
    stats.transfersRequested = 12U;
    stats.transfersCompleted = 13U;
    stats.transfersDenied = 14U;
    stats.transfersTimedOut = 15U;
    stats.transfersIgnoredStale = 16U;
    stats.propertyFramesSent = 17U;
    stats.propertyFramesReceived = 18U;
    stats.propertiesApplied = 19U;
    stats.propertiesIgnoredUnknown = 20U;
    stats.propertiesIgnoredOwned = 21U;
    stats.propertiesSkippedOversized = 22U;
    stats.propertiesTruncated = 23U;

    Core::ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("net", 1U).IsAccepted());
    EXPECT_TRUE(RecordReplicationCounters(capture, stats, 42U, 100U, 7U).IsAccepted());

    const std::vector<std::string> names{
        "net.replication.snapshots-sent", "net.replication.snapshots-received",
        "net.replication.entries-applied", "net.replication.entries-ignored-unknown",
        "net.replication.entries-truncated", "net.replication.messages-ignored-malformed",
        "net.replication.entries-skipped-irrelevant", "net.replication.entries-ignored-owned",
        "net.replication.delta-snapshots-sent", "net.replication.delta-snapshots-received",
        "net.replication.entries-skipped-unchanged", "net.replication.transfers-requested",
        "net.replication.transfers-completed", "net.replication.transfers-denied",
        "net.replication.transfers-timed-out", "net.replication.transfers-ignored-stale",
        "net.replication.property-frames-sent", "net.replication.property-frames-received",
        "net.replication.properties-applied", "net.replication.properties-ignored-unknown",
        "net.replication.properties-ignored-owned",
        "net.replication.properties-skipped-oversized", "net.replication.properties-truncated"};
    const Core::DiagnosticCaptureBundle live = capture.Snapshot();
    ASSERT_EQ(live.counters.size(), names.size());
    for (std::size_t i = 0U; i < names.size(); ++i) {
        ExpectCounter(live.counters[i], names[i], static_cast<double>(i + 1U), 42U, 100U, 7U);
    }
}

TEST(NetworkProfilerTest, InterestAndFanout_NamesEveryField) {
    InterestStats interest;
    interest.queries = 1U;
    interest.refreshes = 2U;
    interest.relevant = 3U;
    interest.culled = 4U;
    interest.unknownIds = 5U;
    interest.throttled = 6U;
    FanoutStats fanout;
    fanout.refreshes = 7U;
    fanout.selected = 8U;
    fanout.budgetDropped = 9U;

    Core::ProfilerCapture capture;
    ASSERT_TRUE(capture.Begin("net", 1U).IsAccepted());
    EXPECT_TRUE(RecordInterestCounters(capture, interest, 42U, 100U, 7U).IsAccepted());
    EXPECT_TRUE(RecordFanoutCounters(capture, fanout, 42U, 100U, 7U).IsAccepted());

    const std::vector<std::string> names{"net.interest.queries",
                                         "net.interest.refreshes",
                                         "net.interest.relevant",
                                         "net.interest.culled",
                                         "net.interest.unknown-ids",
                                         "net.interest.throttled",
                                         "net.fanout.refreshes",
                                         "net.fanout.selected",
                                         "net.fanout.budget-dropped"};
    const Core::DiagnosticCaptureBundle live = capture.Snapshot();
    ASSERT_EQ(live.counters.size(), names.size());
    for (std::size_t i = 0U; i < names.size(); ++i) {
        ExpectCounter(live.counters[i], names[i], static_cast<double>(i + 1U), 42U, 100U, 7U);
    }
}

TEST(NetworkProfilerTest, InactiveCapture_NamesTheFailure) {
    Core::ProfilerCapture capture;
    EXPECT_EQ(RecordReplicationCounters(capture, ReplicationStats{}, 1U, 1U, 1U).code,
              Core::DiagnosticCaptureCode::NotActive);
    EXPECT_EQ(RecordInterestCounters(capture, InterestStats{}, 1U, 1U, 1U).code,
              Core::DiagnosticCaptureCode::NotActive);
    EXPECT_EQ(RecordFanoutCounters(capture, FanoutStats{}, 1U, 1U, 1U).code,
              Core::DiagnosticCaptureCode::NotActive);
    EXPECT_TRUE(capture.Snapshot().counters.empty());
}

} // namespace
} // namespace UVE::Network::Tests
