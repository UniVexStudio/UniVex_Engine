// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_profiler.h"

namespace UVE::Network {
namespace {

struct CounterSink final {
    Core::ProfilerCapture& capture;
    std::uint64_t frameNumber;
    std::uint64_t timestampNanoseconds;
    std::uint64_t threadId;
    Core::DiagnosticCaptureResult first{
        Core::DiagnosticCaptureCode::Accepted, "accepted"};

    void Record(const char* name, const std::uint64_t value) {
        const Core::DiagnosticCaptureResult one = capture.RecordCounter(
            Core::DiagnosticCategory::Net, name, timestampNanoseconds,
            static_cast<double>(value), threadId, frameNumber);
        if (!one.IsAccepted() && first.IsAccepted()) {
            first = one;
        }
    }
};

} // namespace

Core::DiagnosticCaptureResult RecordReplicationCounters(
    Core::ProfilerCapture& capture, const ReplicationStats& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSink sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.Record("net.replication.snapshots-sent", stats.snapshotsSent);
    sink.Record("net.replication.snapshots-received", stats.snapshotsReceived);
    sink.Record("net.replication.entries-applied", stats.entriesApplied);
    sink.Record("net.replication.entries-ignored-unknown", stats.entriesIgnoredUnknown);
    sink.Record("net.replication.entries-truncated", stats.entriesTruncated);
    sink.Record("net.replication.messages-ignored-malformed", stats.messagesIgnoredMalformed);
    sink.Record("net.replication.entries-skipped-irrelevant", stats.entriesSkippedIrrelevant);
    sink.Record("net.replication.entries-ignored-owned", stats.entriesIgnoredOwned);
    sink.Record("net.replication.delta-snapshots-sent", stats.deltaSnapshotsSent);
    sink.Record("net.replication.delta-snapshots-received", stats.deltaSnapshotsReceived);
    sink.Record("net.replication.entries-skipped-unchanged", stats.entriesSkippedUnchanged);
    sink.Record("net.replication.transfers-requested", stats.transfersRequested);
    sink.Record("net.replication.transfers-completed", stats.transfersCompleted);
    sink.Record("net.replication.transfers-denied", stats.transfersDenied);
    sink.Record("net.replication.transfers-timed-out", stats.transfersTimedOut);
    sink.Record("net.replication.transfers-ignored-stale", stats.transfersIgnoredStale);
    sink.Record("net.replication.property-frames-sent", stats.propertyFramesSent);
    sink.Record("net.replication.property-frames-received", stats.propertyFramesReceived);
    sink.Record("net.replication.properties-applied", stats.propertiesApplied);
    sink.Record("net.replication.properties-ignored-unknown", stats.propertiesIgnoredUnknown);
    sink.Record("net.replication.properties-ignored-owned", stats.propertiesIgnoredOwned);
    sink.Record("net.replication.properties-skipped-oversized",
                    stats.propertiesSkippedOversized);
    sink.Record("net.replication.properties-truncated", stats.propertiesTruncated);
    return sink.first;
}

Core::DiagnosticCaptureResult RecordInterestCounters(
    Core::ProfilerCapture& capture, const InterestStats& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSink sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.Record("net.interest.queries", stats.queries);
    sink.Record("net.interest.refreshes", stats.refreshes);
    sink.Record("net.interest.relevant", stats.relevant);
    sink.Record("net.interest.culled", stats.culled);
    sink.Record("net.interest.unknown-ids", stats.unknownIds);
    sink.Record("net.interest.throttled", stats.throttled);
    return sink.first;
}

Core::DiagnosticCaptureResult RecordFanoutCounters(
    Core::ProfilerCapture& capture, const FanoutStats& stats,
    const std::uint64_t frameNumber, const std::uint64_t timestampNanoseconds,
    const std::uint64_t threadId) {
    CounterSink sink{capture, frameNumber, timestampNanoseconds, threadId};
    sink.Record("net.fanout.refreshes", stats.refreshes);
    sink.Record("net.fanout.selected", stats.selected);
    sink.Record("net.fanout.budget-dropped", stats.budgetDropped);
    return sink.first;
}

} // namespace UVE::Network
