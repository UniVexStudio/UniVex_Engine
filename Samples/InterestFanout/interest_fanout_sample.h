// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Samples/InterestFanout: the interest fan-out demo - one server, three bot clients, the
// whole interest stack (grid relevance, priority tiers, per-entity rates, per-viewer
// fan-out budgets) at sample scale:
//
//   formation: six walkers (net 20-25, Critical through Low) march +X in formation past
//              every client, 2.5 units a tick;
//   views:     A sits at the origin unbudgeted (sees all, always); B shares A's view
//              with a budget of 2 (the top two every tick); C watches from x=200
//              through a radius-30 window (walkers enter mid-run and leave late);
//   cadence:   every walker but 25 fires every tick (rate 1), while 25 keeps its
//              natural Low cadence (every 8th tick) - the cadence gap shows end to end.
//
// Transports are loopback (in-process, deterministic, no ports and no clock but the
// sim's). RunInterestFanoutSampleUVE() runs the whole story synchronously and answers a
// verdict: success plus statistics, or the list of violated invariants. The binary prints
// the log and exits by the verdict; the test suite runs it silent and asserts green.
//

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Samples {

/// Walker count (net ids 20-25, slot = net id - 20).
inline constexpr std::uint64_t kInterestFanoutEntitiesUVE = 6U;

struct InterestFanoutSampleConfigUVE final {
    /// Fixed server ticks to run. The formation crosses C's window between ticks 48
    /// and 92, so runs below ~95 ticks fail the checks that need the full pass.
    std::uint64_t ticks = 100U;
    /// One human-readable log line. Empty silences the story; the verdict never needs it.
    std::function<void(const std::string&)> log;
};

struct InterestFanoutSampleStatsUVE final {
    std::uint64_t ticks = 0U;
    /// Applies received, per client per walker slot.
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> receivedA{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> receivedB{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> receivedC{};
    /// First apply tick (1-based, 0 = never): the discovery order.
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> firstSeenA{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> firstSeenB{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> firstSeenC{};
    /// Last apply tick (0 = never): walkers leave C's window late.
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> lastSeenA{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> lastSeenB{};
    std::array<std::uint64_t, kInterestFanoutEntitiesUVE> lastSeenC{};
    /// The fan-out's cumulative counters (refreshes, kept, shed by budget).
    std::uint64_t fanoutRefreshes = 0U;
    std::uint64_t fanoutSelected = 0U;
    std::uint64_t fanoutBudgetDropped = 0U;
    /// Per-server-replicator irrelevant skips (budget cuts, culled walkers, off-phase 25).
    std::uint64_t skippedA = 0U;
    std::uint64_t skippedB = 0U;
    std::uint64_t skippedC = 0U;
    /// The profiling chapter: one tick span per tick, plus a 32-counter network
    /// snapshot (23 replication + 6 interest + 3 fan-out) every 10th tick.
    std::uint64_t profilerSpans = 0U;
    std::uint64_t profilerCounters = 0U;
};

struct InterestFanoutSampleResultUVE final {
    bool success = false;
    std::vector<std::string> failures;
    InterestFanoutSampleStatsUVE stats;
};

[[nodiscard]] InterestFanoutSampleResultUVE RunInterestFanoutSampleUVE(
    const InterestFanoutSampleConfigUVE& config = InterestFanoutSampleConfigUVE{});

} // namespace UVE::Samples
