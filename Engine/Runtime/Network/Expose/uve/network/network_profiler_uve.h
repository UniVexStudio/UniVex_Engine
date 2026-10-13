// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the network emitters for the diagnostics capture - one Net counter per stats
// field, so a server loop can snapshot its replication, interest, and fan-out counters
// into the same capture the profiler panel shows. The emitters read stats structs, not
// live objects: pure translation, and unit tests feed them hand-built numbers.
//

#include <cstdint>

#include "uve/diagnostics/profiler_diagnostics_uve.h"
#include "uve/network/network_interest_fanout_uve.h"
#include "uve/network/network_interest_uve.h"
#include "uve/network/network_replication_uve.h"

namespace UVE::Network {

/// Records every ReplicationStatsUVE field as `net.replication.*`. Answers the first
/// failure (an inactive capture names NotActive) or Accepted when all 23 landed.
[[nodiscard]] Core::DiagnosticCaptureResultUVE RecordReplicationCountersUVE(
    Core::ProfilerCaptureUVE& capture, const ReplicationStatsUVE& stats,
    std::uint64_t frameNumber, std::uint64_t timestampNanoseconds, std::uint64_t threadId);

/// Records every InterestStatsUVE field as `net.interest.*` (6 counters).
[[nodiscard]] Core::DiagnosticCaptureResultUVE RecordInterestCountersUVE(
    Core::ProfilerCaptureUVE& capture, const InterestStatsUVE& stats, std::uint64_t frameNumber,
    std::uint64_t timestampNanoseconds, std::uint64_t threadId);

/// Records every FanoutStatsUVE field as `net.fanout.*` (3 counters).
[[nodiscard]] Core::DiagnosticCaptureResultUVE RecordFanoutCountersUVE(
    Core::ProfilerCaptureUVE& capture, const FanoutStatsUVE& stats, std::uint64_t frameNumber,
    std::uint64_t timestampNanoseconds, std::uint64_t threadId);

} // namespace UVE::Network
