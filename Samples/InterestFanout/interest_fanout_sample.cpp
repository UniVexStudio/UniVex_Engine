// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interest_fanout_sample.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "profiler_diagnostics.h"
#include "profiler_view.h"
#include "vector3.h"
#include "network_interest_fanout.h"
#include "network_interest.h"
#include "network_profiler.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"

namespace UVE::Samples {
namespace {

constexpr std::uint64_t kEstablishMs = 1000U;
constexpr std::uint64_t kTickMs = 100U;
constexpr Network::NetworkEntityId kFirstWalker = 20U;
constexpr float kWalkerSpeed = 2.5F;
constexpr float kWalkerSpacing = 10.0F;

constexpr std::array<Network::InterestTier, kInterestFanoutEntities> kWalkerTiers{
    Network::InterestTier::Critical, Network::InterestTier::High,
    Network::InterestTier::High, Network::InterestTier::Normal,
    Network::InterestTier::Normal, Network::InterestTier::Low};

[[nodiscard]] Network::NetworkEntityId WalkerId(const std::uint64_t slot) {
    return static_cast<Network::NetworkEntityId>(kFirstWalker + slot);
}

struct FanoutSide final {
    Network::LoopbackTransportPair link;
    Network::NetworkReplicator server;
    Network::NetworkReplicator client;
    std::array<Math::Vector3, kInterestFanoutEntities> seen{};

    void UpdateBoth(const std::uint64_t nowMs) {
        server.Update(nowMs);
        client.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!server.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return server.Session().State() == Network::SessionState::Established &&
               client.Session().State() == Network::SessionState::Established;
    }
};

[[nodiscard]] FanoutSide MakeSide() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession serverSession(Network::SessionRole::Client,
                                             std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession clientSession(Network::SessionRole::Server,
                                             std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkReplicator server(std::move(serverSession));
    Network::NetworkReplicator client(std::move(clientSession));
    return FanoutSide{std::move(pair), std::move(server), std::move(client), {}};
}

[[nodiscard]] std::uint64_t Sum(
    const std::array<std::uint64_t, kInterestFanoutEntities>& counts) {
    std::uint64_t total = 0U;
    for (const std::uint64_t count : counts) {
        total += count;
    }
    return total;
}

} // namespace

InterestFanoutSampleResult RunInterestFanoutSample(
    const InterestFanoutSampleConfig& config) {
    InterestFanoutSampleResult result;
    result.stats.ticks = config.ticks;
    auto& stats = result.stats;
    auto check = [&](const bool ok, const std::string& message) {
        if (!ok) {
            result.failures.push_back(message);
        }
    };
    auto log = [&](const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    };

    Network::NetworkInterest interest;
    check(interest.AddViewer(1U, 1000.0F, Math::Vector3{0.0F, 0.0F, 0.0F}),
          "viewer A rejected");
    check(interest.AddViewer(2U, 1000.0F, Math::Vector3{0.0F, 0.0F, 0.0F}),
          "viewer B rejected");
    check(interest.AddViewer(3U, 30.0F, Math::Vector3{200.0F, 0.0F, 0.0F}),
          "viewer C rejected");
    for (std::uint64_t slot = 0U; slot < kInterestFanoutEntities; ++slot) {
        check(interest.AddEntity(WalkerId(slot),
                                    Math::Vector3{kWalkerSpacing * static_cast<float>(slot),
                                                     0.0F, 0.0F},
                                    false, kWalkerTiers[slot]),
              "walker registration rejected");
    }
    // Every walker but 25 fires every tick: 25 keeps its natural Low cadence so the
    // cadence gap shows end to end.
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntities; ++slot) {
        check(interest.SetEntityRate(WalkerId(slot), 1U), "rate override rejected");
    }
    Network::NetworkInterestFanout fanout(interest);
    fanout.SetViewerBudget(2U, 2U);

    std::array<Math::Vector3, kInterestFanoutEntities> world{};
    FanoutSide sideA = MakeSide();
    FanoutSide sideB = MakeSide();
    FanoutSide sideC = MakeSide();
    check(sideA.Establish(kEstablishMs), "client A never established");
    check(sideB.Establish(kEstablishMs), "client B never established");
    check(sideC.Establish(kEstablishMs), "client C never established");

    std::uint64_t currentTick = 0U;
    auto registerSide = [&](FanoutSide& side, const Network::InterestViewerId viewer,
                            std::array<std::uint64_t, kInterestFanoutEntities>& received,
                            std::array<std::uint64_t, kInterestFanoutEntities>& firstSeen,
                            std::array<std::uint64_t, kInterestFanoutEntities>& lastSeen,
                            const std::string& name) {
        for (std::uint64_t slot = 0U; slot < kInterestFanoutEntities; ++slot) {
            const Network::NetworkEntityId netId = WalkerId(slot);
            check(side.server.RegisterEntity(
                      netId, [&world, slot]() { return world[slot]; },
                      [](const Math::Vector3&) {}, Network::ReplicationAuthority::Owner,
                      [&fanout, viewer, netId]() {
                          return fanout.IsSelected(viewer, netId);
                      }),
                  name + " server registration rejected");
            check(side.client.RegisterEntity(
                      netId, [&side, slot]() { return side.seen[slot]; },
                      [&side, slot, &received, &firstSeen, &lastSeen,
                       &currentTick](const Math::Vector3& position) {
                          side.seen[slot] = position;
                          ++received[slot];
                          if (firstSeen[slot] == 0U) {
                              firstSeen[slot] = currentTick;
                          }
                          lastSeen[slot] = currentTick;
                      },
                      Network::ReplicationAuthority::Proxy),
                  name + " client registration rejected");
        }
    };
    Core::ProfilerCapture profile;
    check(profile.Begin("fanout", 0U).IsAccepted(), "profiling never began");

    registerSide(sideA, 1U, stats.receivedA, stats.firstSeenA, stats.lastSeenA, "A");
    registerSide(sideB, 2U, stats.receivedB, stats.firstSeenB, stats.lastSeenB, "B");
    registerSide(sideC, 3U, stats.receivedC, stats.firstSeenC, stats.lastSeenC, "C");

    for (std::uint64_t tick = 1U; tick <= config.ticks; ++tick) {
        currentTick = tick;
        for (std::uint64_t slot = 0U; slot < kInterestFanoutEntities; ++slot) {
            world[slot] = Math::Vector3{
                kWalkerSpacing * static_cast<float>(slot) +
                    kWalkerSpeed * static_cast<float>(tick),
                0.0F, 0.0F};
            check(interest.UpdateEntity(WalkerId(slot), world[slot]),
                  "walker left the interest map");
        }
        static_cast<void>(fanout.RefreshAll());
        const std::uint64_t nowMs = kEstablishMs + tick * kTickMs;
        sideA.UpdateBoth(nowMs);
        sideB.UpdateBoth(nowMs);
        sideC.UpdateBoth(nowMs);
        check(profile.RecordSpan(Core::DiagnosticCategory::Cpu, "fanout-tick",
                                    tick * 1000000U, (tick + 1U) * 1000000U, 1U, tick)
                  .IsAccepted(),
              "tick span refused");
        if (tick % 10U == 0U) {
            check(Network::RecordReplicationCounters(profile, sideA.server.Stats(), tick,
                                                       tick * 1000000U, 1U)
                      .IsAccepted(),
                  "replication counters refused");
            check(Network::RecordInterestCounters(profile, interest.Stats(), tick,
                                                     tick * 1000000U, 1U)
                      .IsAccepted(),
                  "interest counters refused");
            check(Network::RecordFanoutCounters(profile, fanout.Stats(), tick,
                                                   tick * 1000000U, 1U)
                      .IsAccepted(),
                  "fan-out counters refused");
        }
        if (tick % 20U == 0U) {
            log("tick " + std::to_string(tick) + " received A/B/C " +
                std::to_string(Sum(stats.receivedA)) + "/" +
                std::to_string(Sum(stats.receivedB)) + "/" +
                std::to_string(Sum(stats.receivedC)) + " selected " +
                std::to_string(fanout.Stats().selected) + " dropped " +
                std::to_string(fanout.Stats().budgetDropped));
        }
    }

    const Core::DiagnosticCaptureStopResult profiled =
        profile.End((config.ticks + 1U) * 1000000U);
    check(profiled.IsAccepted(), "profiling never ended");
    stats.profilerSpans = profiled.bundle.spans.size();
    stats.profilerCounters = profiled.bundle.counters.size();
    check(stats.profilerSpans == config.ticks, "tick spans drifted");
    check(stats.profilerCounters == 32U * (config.ticks / 10U), "profiled counters drifted");
    // The view builder the panel prints through reads the same bundle: the latest
    // profiled totals are the story's own totals.
    const Network::ReplicationStats serverAStats = sideA.server.Stats();
    bool foundSelected = false;
    bool foundSent = false;
    for (const Core::ProfilerCounterRow& row :
         Core::BuildProfilerCounterRows(profiled.bundle.counters)) {
        if (row.name == "net.fanout.selected") {
            foundSelected = true;
            check(row.value == static_cast<double>(fanout.Stats().selected),
                  "profiled selections disagree with the story");
        }
        if (row.name == "net.replication.snapshots-sent") {
            foundSent = true;
            check(row.value == static_cast<double>(serverAStats.snapshotsSent),
                  "profiled sends disagree with the story");
        }
    }
    check(foundSelected, "profiled selections missing");
    check(foundSent, "profiled sends missing");

    const Network::FanoutStats fanoutStats = fanout.Stats();
    stats.fanoutRefreshes = fanoutStats.refreshes;
    stats.fanoutSelected = fanoutStats.selected;
    stats.fanoutBudgetDropped = fanoutStats.budgetDropped;
    stats.skippedA = sideA.server.Stats().entriesSkippedIrrelevant;
    stats.skippedB = sideB.server.Stats().entriesSkippedIrrelevant;
    stats.skippedC = sideC.server.Stats().entriesSkippedIrrelevant;

    // A is unbudgeted with everything in radius: it converges the formation. Walker 25
    // phases, so A holds its pose from its last apply tick rather than the final one.
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntities; ++slot) {
        check(sideA.seen[slot] == world[slot], "A diverged from the formation");
    }
    check(stats.lastSeenA[5U] > 0U, "A never saw walker 25");
    check(sideA.seen[5U] ==
              Math::Vector3{kWalkerSpacing * 5.0F +
                                   kWalkerSpeed * static_cast<float>(stats.lastSeenA[5U]),
                               0.0F, 0.0F},
          "A diverged on walker 25's last apply");
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntities; ++slot) {
        check(stats.receivedA[slot] == config.ticks, "A missed a rate-1 walker");
    }
    check(stats.receivedA[5U] > 0U && stats.receivedA[5U] < stats.receivedA[0U],
          "A's Low walker cadence never showed");
    check(stats.skippedA + stats.receivedA[5U] == config.ticks,
          "A's walker 25 was neither selected nor skipped on some tick");
    // B shares A's view with a budget of 2: the top two every tick, nothing else.
    check(stats.receivedB[0U] == config.ticks && stats.receivedB[1U] == config.ticks,
          "B missed the top two");
    check(sideB.seen[0U] == world[0U] && sideB.seen[1U] == world[1U],
          "B diverged on the top two");
    for (std::uint64_t slot = 2U; slot < kInterestFanoutEntities; ++slot) {
        check(stats.receivedB[slot] == 0U && stats.firstSeenB[slot] == 0U &&
                  stats.lastSeenB[slot] == 0U,
              "B saw past its budget");
    }
    check(stats.skippedB == 4U * config.ticks, "B's skip count drifted from 4 a tick");
    // C watches the formation walk past its window: reverse discovery (nearest first),
    // everyone dwells, everyone leaves, exits in the same order.
    for (std::uint64_t slot = 0U; slot < kInterestFanoutEntities; ++slot) {
        check(stats.firstSeenC[slot] > 0U, "C never discovered a walker");
        check(stats.lastSeenC[slot] > stats.firstSeenC[slot], "C never dwelled on a walker");
        check(stats.lastSeenC[slot] < config.ticks, "a walker never left C's window");
    }
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntities; ++slot) {
        check(stats.firstSeenC[slot] > stats.firstSeenC[slot + 1U],
              "C's discovery order broke");
        check(stats.lastSeenC[slot] > stats.lastSeenC[slot + 1U], "C's exit order broke");
    }
    check(stats.receivedC[5U] > 0U && stats.receivedC[5U] < stats.receivedC[4U],
          "C's Low walker cadence never showed");
    // Fan-out accounting: every selection applied somewhere, every B skip was a budget
    // cut or an off-phase 25, and every C walker/tick applied or skipped.
    check(stats.fanoutRefreshes == 3U * config.ticks, "refresh count drifted");
    check(stats.fanoutSelected ==
              Sum(stats.receivedA) + Sum(stats.receivedB) + Sum(stats.receivedC),
          "a selection never applied");
    check(stats.skippedB ==
              stats.fanoutBudgetDropped + (config.ticks - stats.receivedA[5U]),
          "B's skips disagree with the budget drops");
    check(Sum(stats.receivedC) + stats.skippedC ==
              kInterestFanoutEntities * config.ticks,
          "a C walker/tick neither applied nor skipped");

    log("interest fan-out ran " + std::to_string(config.ticks) + " ticks, A " +
        std::to_string(Sum(stats.receivedA)) + " B " + std::to_string(Sum(stats.receivedB)) +
        " C " + std::to_string(Sum(stats.receivedC)) + " selected " +
        std::to_string(stats.fanoutSelected) + " dropped " +
        std::to_string(stats.fanoutBudgetDropped) + ", " +
        std::to_string(result.failures.size()) + " violations");
    result.success = result.failures.empty();
    return result;
}

} // namespace UVE::Samples
