// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Samples/LagCompServer: the lag-compensation server demo - one server, one lagged bot,
// the whole 487 stack firing at sample scale:
//
//   record:  the server numbers its lag snapshots with the ticks its prediction layer
//            applied (ShooterViewTickUVE's no-mapping-table contract), filling every tick;
//   lag:     the bot pumps its prediction stream every 10th server tick, so its acked view
//            trails the server by ~10-20 ticks while the victim walks on;
//   shots:   each tick the bot fires a shot intent (stale view tick + fire fraction):
//            the live scan misses where the victim escaped to, the rewound resolve hits
//            where it stood, and the merge enters the saved pair through the strike
//            tracker exactly once;
//   rays:    the transported fraction drives the rewound ray too, landing damage and
//            health events with live parity.
//
// Transports are loopback (in-process, deterministic, no ports and no clock but the sim's).
// The victim never dies (10k health): the story is about saved hits, not kill handling.
//
// RunLagCompServerSampleUVE() runs the whole story synchronously and answers a verdict:
// success plus statistics, or the list of violated invariants. The binary prints the log
// and exits by the verdict; the test suite runs it silent and asserts green.
//

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Samples {

struct LagCompServerSampleConfigUVE final {
    /// Fixed server ticks to run. The victim crosses the turret's overlap window around
    /// tick 80 and exits ray range around tick 347, so runs below ~360 ticks fail the
    /// checks that need both edges (shot saves need the window, ray saves the exit).
    std::uint64_t ticks = 500U;
    /// One human-readable log line. Empty silences the story; the verdict never needs it.
    std::function<void(const std::string&)> log;
};

struct LagCompServerSampleStatsUVE final {
    std::uint64_t ticks = 0U;
    /// Ticks the bot fired (every tick once the server applied anything).
    std::uint64_t shotsFired = 0U;
    /// Fires skipped for lack of a view (tick 0 fails closed: no ack yet, no resolve).
    std::uint64_t shotsNoView = 0U;
    /// Fires where the live scan held the victim.
    std::uint64_t shotLiveHits = 0U;
    /// Fires where the rewound resolve held the victim.
    std::uint64_t shotRewoundHits = 0U;
    /// Rewound-only fires: the live scan missed, lag compensation saved the hit.
    std::uint64_t shotSaves = 0U;
    /// Fractional-tick shots that blended poses (the interpolation path fired).
    std::uint64_t fracInterpolated = 0U;
    std::uint64_t raysFired = 0U;
    std::uint64_t rayLiveHits = 0U;
    std::uint64_t rayRewoundHits = 0U;
    std::uint64_t raySaves = 0U;
    /// Strike-tracker Entered transitions from merged reports (lagged hits entering live).
    std::uint64_t enteredEvents = 0U;
    /// Widest the bot's view ever trailed the server's applied tick (the lag proof).
    std::uint64_t maxViewLag = 0U;
    /// The victim's remaining health (10k minus strikes at 1.0 and rays at 2.0).
    float victimHealth = 0.0F;
};

struct LagCompServerSampleResultUVE final {
    bool success = false;
    std::vector<std::string> failures;
    LagCompServerSampleStatsUVE stats;
};

[[nodiscard]] LagCompServerSampleResultUVE RunLagCompServerSampleUVE(
    const LagCompServerSampleConfigUVE& config = LagCompServerSampleConfigUVE{});

} // namespace UVE::Samples
