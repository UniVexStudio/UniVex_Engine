// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_profiler_uve.h"

namespace UVE::Network {
namespace {

struct CounterSinkUVE final {
    Core::ProfilerCaptureUVE& capture;
    std::uint64_t frameNumber;
    std::uint64_t timestampNanoseconds;
    std::uint64_t threadId;
    Core::DiagnosticCaptureResultUVE first{
        Core::DiagnosticCaptureCodeUVE::Accepted, "accepted"};

    void RecordUVE(const char* name, const std::uint64_t value) {
        const Core::DiagnosticCaptureResultUVE one = capture.RecordCounterUVE(
            Core::DiagnosticCategoryUVE::Net, name, timestampNanoseconds,
            static_cast<double>(value), threadId, frameNumber);
        if (!one.IsAcceptedUVE() && first.IsAcceptedUVE()) {
            first = one;
        }
    }
};

} // namespace

Core::DiagnosticCaptureResultUVE RecordReplicationCountersUVE(
    Core::ProfilerCaptureUVE& capture, const ReplicationStatsUVE& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSinkUVE sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.RecordUVE("net.replication.snapshots-sent", stats.snapshotsSent);
    sink.RecordUVE("net.replication.snapshots-received", stats.snapshotsReceived);
    sink.RecordUVE("net.replication.entries-applied", stats.entriesApplied);
    sink.RecordUVE("net.replication.entries-ignored-unknown", stats.entriesIgnoredUnknown);
    sink.RecordUVE("net.replication.entries-truncated", stats.entriesTruncated);
    sink.RecordUVE("net.replication.messages-ignored-malformed", stats.messagesIgnoredMalformed);
    sink.RecordUVE("net.replication.entries-skipped-irrelevant", stats.entriesSkippedIrrelevant);
    sink.RecordUVE("net.replication.entries-ignored-owned", stats.entriesIgnoredOwned);
    sink.RecordUVE("net.replication.delta-snapshots-sent", stats.deltaSnapshotsSent);
    sink.RecordUVE("net.replication.delta-snapshots-received", stats.deltaSnapshotsReceived);
    sink.RecordUVE("net.replication.entries-skipped-unchanged", stats.entriesSkippedUnchanged);
    sink.RecordUVE("net.replication.transfers-requested", stats.transfersRequested);
    sink.RecordUVE("net.replication.transfers-completed", stats.transfersCompleted);
    sink.RecordUVE("net.replication.transfers-denied", stats.transfersDenied);
    sink.RecordUVE("net.replication.transfers-timed-out", stats.transfersTimedOut);
    sink.RecordUVE("net.replication.transfers-ignored-stale", stats.transfersIgnoredStale);
    sink.RecordUVE("net.replication.property-frames-sent", stats.propertyFramesSent);
    sink.RecordUVE("net.replication.property-frames-received", stats.propertyFramesReceived);
    sink.RecordUVE("net.replication.properties-applied", stats.propertiesApplied);
    sink.RecordUVE("net.replication.properties-ignored-unknown", stats.propertiesIgnoredUnknown);
    sink.RecordUVE("net.replication.properties-ignored-owned", stats.propertiesIgnoredOwned);
    sink.RecordUVE("net.replication.properties-skipped-oversized",
                    stats.propertiesSkippedOversized);
    sink.RecordUVE("net.replication.properties-truncated", stats.propertiesTruncated);
    return sink.first;
}

Core::DiagnosticCaptureResultUVE RecordInterestCountersUVE(
    Core::ProfilerCaptureUVE& capture, const InterestStatsUVE& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSinkUVE sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.RecordUVE("net.interest.queries", stats.queries);
    sink.RecordUVE("net.interest.refreshes", stats.refreshes);
    sink.RecordUVE("net.interest.relevant", stats.relevant);
    sink.RecordUVE("net.interest.culled", stats.culled);
    sink.RecordUVE("net.interest.unknown-ids", stats.unknownIds);
    sink.RecordUVE("net.interest.throttled", stats.throttled);
    return sink.first;
}

Core::DiagnosticCaptureResultUVE RecordFanoutCountersUVE(
    Core::ProfilerCaptureUVE& capture, const FanoutStatsUVE& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSinkUVE sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.RecordUVE("net.fanout.refreshes", stats.refreshes);
    sink.RecordUVE("net.fanout.selected", stats.selected);
    sink.RecordUVE("net.fanout.budget-dropped", stats.budgetDropped);
    return sink.first;
}

} // namespace UVE::Network
