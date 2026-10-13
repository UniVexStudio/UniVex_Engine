// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Samples/DedicatedServer: the headless dedicated-server sample - one server, three bot
// clients, every adapter composed the way a production game composes them:
//
//   lobby:      bots create and join rooms through the typed client facade;
//   relay:      room-scoped shouts, bound to lobby rooms by the server's ClientRoomUVE sync
//               loop (roommates hear each other; nobody else does);
//   prediction: each bot predicts its pawn through TickPredictedPawnUVE() against a floored
//               server stepper, reconciling every landing it cannot see;
//   replication: predicted pawns ride as Owner against the server's proxy dolls, and a
//               server-owned patrol NPC rides back down to every bot's doll;
//   mover:      an untagged local walker steps through the real StepCharacter3DUVE() beside
//               the dolls - while the tagged pawns keep refusing it, proving the two never
//               double-step.
//
// Transports are loopback (in-process, deterministic, no ports and no clock but the sim's),
// written against the session seam where the pairs are built - real endpoints drop into the
// same construction when the UDP listener lands. Default session timeouts stay on: the story
// is traffic-rich (ticks, snapshots, shouts, roster refreshes), so a silent regression fails
// loudly instead of idling past it.
//
// RunDedicatedServerSampleUVE() runs the whole story synchronously and answers a verdict:
// success plus statistics, or the list of violated invariants. The binary prints the log and
// exits by the verdict; the test suite runs it silent and asserts green.
//

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Samples {

/// One human-readable log line. Empty sinks silence the story; the verdict never needs them.
using SampleLogSinkUVE = std::function<void(const std::string&)>;

struct DedicatedServerSampleConfigUVE final {
    /// Fixed steps to run. Shouts go out every 30 ticks and roster refreshes every 60, so
    /// short runs still exercise every path - but runs below ~60 ticks fail the checks that
    /// need a landing (reconciles) or a walk (the walker crossing).
    std::uint64_t ticks = 600U;
    SampleLogSinkUVE log;
};

struct DedicatedServerSampleStatsUVE final {
    std::uint64_t ticks = 0U;
    std::uint64_t reconcilesAmy = 0U;
    std::uint64_t reconcilesBob = 0U;
    std::uint64_t reconcilesCat = 0U;
    std::uint64_t shoutsHeardAmy = 0U;
    std::uint64_t shoutsHeardBob = 0U;
    std::uint64_t shoutsHeardCat = 0U;
    /// Snapshots the server's three replicators received, summed: the traffic proof.
    std::uint64_t snapshotsReceived = 0U;
    /// Final rest height of every pawn (all land on the server floor at 0).
    float restY = 0.0F;
    /// How far the untagged walker got (it never stops walking +x).
    float walkerX = 0.0F;
};

struct DedicatedServerSampleResultUVE final {
    bool success = false;
    std::vector<std::string> failures;
    DedicatedServerSampleStatsUVE stats;
};

[[nodiscard]] DedicatedServerSampleResultUVE RunDedicatedServerSampleUVE(
    const DedicatedServerSampleConfigUVE& config = DedicatedServerSampleConfigUVE{});

} // namespace UVE::Samples
