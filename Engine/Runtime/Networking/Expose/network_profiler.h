// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the network emitters for the diagnostics capture - one Net counter per stats
// field, so a server loop can snapshot its replication, interest, and fan-out counters
// into the same capture the profiler panel shows. The emitters read stats structs, not
// live objects: pure translation, and unit tests feed them hand-built numbers.
//

#include <cstdint>

#include "profiler_diagnostics.h"
#include "network_interest_fanout.h"
#include "network_interest.h"
#include "network_replication.h"

namespace UVE::Network {

/// Records every ReplicationStats field as `net.replication.*`. Answers the first
/// failure (an inactive capture names NotActive) or Accepted when all 23 landed.
[[nodiscard]] Core::DiagnosticCaptureResult RecordReplicationCounters(
    Core::ProfilerCapture& capture, const ReplicationStats& stats,
    std::uint64_t frameNumber, std::uint64_t timestampNanoseconds, std::uint64_t threadId);

/// Records every InterestStats field as `net.interest.*` (6 counters).
[[nodiscard]] Core::DiagnosticCaptureResult RecordInterestCounters(
    Core::ProfilerCapture& capture, const InterestStats& stats, std::uint64_t frameNumber,
    std::uint64_t timestampNanoseconds, std::uint64_t threadId);

/// Records every FanoutStats field as `net.fanout.*` (3 counters).
[[nodiscard]] Core::DiagnosticCaptureResult RecordFanoutCounters(
    Core::ProfilerCapture& capture, const FanoutStats& stats, std::uint64_t frameNumber,
    std::uint64_t timestampNanoseconds, std::uint64_t threadId);

} // namespace UVE::Network
