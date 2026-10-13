// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interest_fanout_sample_uve.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "uve/diagnostics/profiler_diagnostics_uve.h"
#include "uve/diagnostics/profiler_view_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/network/network_interest_fanout_uve.h"
#include "uve/network/network_interest_uve.h"
#include "uve/network/network_profiler_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"

namespace UVE::Samples {
namespace {

constexpr std::uint64_t kEstablishMsUVE = 1000U;
constexpr std::uint64_t kTickMsUVE = 100U;
constexpr Network::NetworkEntityIdUVE kFirstWalkerUVE = 20U;
constexpr float kWalkerSpeedUVE = 2.5F;
constexpr float kWalkerSpacingUVE = 10.0F;

constexpr std::array<Network::InterestTierUVE, kInterestFanoutEntitiesUVE> kWalkerTiersUVE{
    Network::InterestTierUVE::Critical, Network::InterestTierUVE::High,
    Network::InterestTierUVE::High, Network::InterestTierUVE::Normal,
    Network::InterestTierUVE::Normal, Network::InterestTierUVE::Low};

[[nodiscard]] Network::NetworkEntityIdUVE WalkerIdUVE(const std::uint64_t slot) {
    return static_cast<Network::NetworkEntityIdUVE>(kFirstWalkerUVE + slot);
}

struct FanoutSideUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkReplicatorUVE server;
    Network::NetworkReplicatorUVE client;
    std::array<Math::Vector3UVE, kInterestFanoutEntitiesUVE> seen{};

    void UpdateBothUVE(const std::uint64_t nowMs) {
        server.UpdateUVE(nowMs);
        client.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!server.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return server.SessionUVE().StateUVE() == Network::SessionStateUVE::Established &&
               client.SessionUVE().StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] FanoutSideUVE MakeSideUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE serverSession(Network::SessionRoleUVE::Client,
                                             std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE clientSession(Network::SessionRoleUVE::Server,
                                             std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkReplicatorUVE server(std::move(serverSession));
    Network::NetworkReplicatorUVE client(std::move(clientSession));
    return FanoutSideUVE{std::move(pair), std::move(server), std::move(client), {}};
}

[[nodiscard]] std::uint64_t SumUVE(
    const std::array<std::uint64_t, kInterestFanoutEntitiesUVE>& counts) {
    std::uint64_t total = 0U;
    for (const std::uint64_t count : counts) {
        total += count;
    }
    return total;
}

} // namespace

InterestFanoutSampleResultUVE RunInterestFanoutSampleUVE(
    const InterestFanoutSampleConfigUVE& config) {
    InterestFanoutSampleResultUVE result;
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

    Network::NetworkInterestUVE interest;
    check(interest.AddViewerUVE(1U, 1000.0F, Math::Vector3UVE{0.0F, 0.0F, 0.0F}),
          "viewer A rejected");
    check(interest.AddViewerUVE(2U, 1000.0F, Math::Vector3UVE{0.0F, 0.0F, 0.0F}),
          "viewer B rejected");
    check(interest.AddViewerUVE(3U, 30.0F, Math::Vector3UVE{200.0F, 0.0F, 0.0F}),
          "viewer C rejected");
    for (std::uint64_t slot = 0U; slot < kInterestFanoutEntitiesUVE; ++slot) {
        check(interest.AddEntityUVE(WalkerIdUVE(slot),
                                    Math::Vector3UVE{kWalkerSpacingUVE * static_cast<float>(slot),
                                                     0.0F, 0.0F},
                                    false, kWalkerTiersUVE[slot]),
              "walker registration rejected");
    }
    // Every walker but 25 fires every tick: 25 keeps its natural Low cadence so the
    // cadence gap shows end to end.
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntitiesUVE; ++slot) {
        check(interest.SetEntityRateUVE(WalkerIdUVE(slot), 1U), "rate override rejected");
    }
    Network::NetworkInterestFanoutUVE fanout(interest);
    fanout.SetViewerBudgetUVE(2U, 2U);

    std::array<Math::Vector3UVE, kInterestFanoutEntitiesUVE> world{};
    FanoutSideUVE sideA = MakeSideUVE();
    FanoutSideUVE sideB = MakeSideUVE();
    FanoutSideUVE sideC = MakeSideUVE();
    check(sideA.EstablishUVE(kEstablishMsUVE), "client A never established");
    check(sideB.EstablishUVE(kEstablishMsUVE), "client B never established");
    check(sideC.EstablishUVE(kEstablishMsUVE), "client C never established");

    std::uint64_t currentTick = 0U;
    auto registerSide = [&](FanoutSideUVE& side, const Network::InterestViewerIdUVE viewer,
                            std::array<std::uint64_t, kInterestFanoutEntitiesUVE>& received,
                            std::array<std::uint64_t, kInterestFanoutEntitiesUVE>& firstSeen,
                            std::array<std::uint64_t, kInterestFanoutEntitiesUVE>& lastSeen,
                            const std::string& name) {
        for (std::uint64_t slot = 0U; slot < kInterestFanoutEntitiesUVE; ++slot) {
            const Network::NetworkEntityIdUVE netId = WalkerIdUVE(slot);
            check(side.server.RegisterEntityUVE(
                      netId, [&world, slot]() { return world[slot]; },
                      [](const Math::Vector3UVE&) {}, Network::ReplicationAuthorityUVE::Owner,
                      [&fanout, viewer, netId]() {
                          return fanout.IsSelectedUVE(viewer, netId);
                      }),
                  name + " server registration rejected");
            check(side.client.RegisterEntityUVE(
                      netId, [&side, slot]() { return side.seen[slot]; },
                      [&side, slot, &received, &firstSeen, &lastSeen,
                       &currentTick](const Math::Vector3UVE& position) {
                          side.seen[slot] = position;
                          ++received[slot];
                          if (firstSeen[slot] == 0U) {
                              firstSeen[slot] = currentTick;
                          }
                          lastSeen[slot] = currentTick;
                      },
                      Network::ReplicationAuthorityUVE::Proxy),
                  name + " client registration rejected");
        }
    };
    Core::ProfilerCaptureUVE profile;
    check(profile.BeginUVE("fanout", 0U).IsAcceptedUVE(), "profiling never began");

    registerSide(sideA, 1U, stats.receivedA, stats.firstSeenA, stats.lastSeenA, "A");
    registerSide(sideB, 2U, stats.receivedB, stats.firstSeenB, stats.lastSeenB, "B");
    registerSide(sideC, 3U, stats.receivedC, stats.firstSeenC, stats.lastSeenC, "C");

    for (std::uint64_t tick = 1U; tick <= config.ticks; ++tick) {
        currentTick = tick;
        for (std::uint64_t slot = 0U; slot < kInterestFanoutEntitiesUVE; ++slot) {
            world[slot] = Math::Vector3UVE{
                kWalkerSpacingUVE * static_cast<float>(slot) +
                    kWalkerSpeedUVE * static_cast<float>(tick),
                0.0F, 0.0F};
            check(interest.UpdateEntityUVE(WalkerIdUVE(slot), world[slot]),
                  "walker left the interest map");
        }
        static_cast<void>(fanout.RefreshAllUVE());
        const std::uint64_t nowMs = kEstablishMsUVE + tick * kTickMsUVE;
        sideA.UpdateBothUVE(nowMs);
        sideB.UpdateBothUVE(nowMs);
        sideC.UpdateBothUVE(nowMs);
        check(profile.RecordSpanUVE(Core::DiagnosticCategoryUVE::Cpu, "fanout-tick",
                                    tick * 1000000U, (tick + 1U) * 1000000U, 1U, tick)
                  .IsAcceptedUVE(),
              "tick span refused");
        if (tick % 10U == 0U) {
            check(Network::RecordReplicationCountersUVE(profile, sideA.server.StatsUVE(), tick,
                                                       tick * 1000000U, 1U)
                      .IsAcceptedUVE(),
                  "replication counters refused");
            check(Network::RecordInterestCountersUVE(profile, interest.StatsUVE(), tick,
                                                     tick * 1000000U, 1U)
                      .IsAcceptedUVE(),
                  "interest counters refused");
            check(Network::RecordFanoutCountersUVE(profile, fanout.StatsUVE(), tick,
                                                   tick * 1000000U, 1U)
                      .IsAcceptedUVE(),
                  "fan-out counters refused");
        }
        if (tick % 20U == 0U) {
            log("tick " + std::to_string(tick) + " received A/B/C " +
                std::to_string(SumUVE(stats.receivedA)) + "/" +
                std::to_string(SumUVE(stats.receivedB)) + "/" +
                std::to_string(SumUVE(stats.receivedC)) + " selected " +
                std::to_string(fanout.StatsUVE().selected) + " dropped " +
                std::to_string(fanout.StatsUVE().budgetDropped));
        }
    }

    const Core::DiagnosticCaptureStopResultUVE profiled =
        profile.EndUVE((config.ticks + 1U) * 1000000U);
    check(profiled.IsAcceptedUVE(), "profiling never ended");
    stats.profilerSpans = profiled.bundle.spans.size();
    stats.profilerCounters = profiled.bundle.counters.size();
    check(stats.profilerSpans == config.ticks, "tick spans drifted");
    check(stats.profilerCounters == 32U * (config.ticks / 10U), "profiled counters drifted");
    // The view builder the panel prints through reads the same bundle: the latest
    // profiled totals are the story's own totals.
    const Network::ReplicationStatsUVE serverAStats = sideA.server.StatsUVE();
    bool foundSelected = false;
    bool foundSent = false;
    for (const Core::ProfilerCounterRowUVE& row :
         Core::BuildProfilerCounterRowsUVE(profiled.bundle.counters)) {
        if (row.name == "net.fanout.selected") {
            foundSelected = true;
            check(row.value == static_cast<double>(fanout.StatsUVE().selected),
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

    const Network::FanoutStatsUVE fanoutStats = fanout.StatsUVE();
    stats.fanoutRefreshes = fanoutStats.refreshes;
    stats.fanoutSelected = fanoutStats.selected;
    stats.fanoutBudgetDropped = fanoutStats.budgetDropped;
    stats.skippedA = sideA.server.StatsUVE().entriesSkippedIrrelevant;
    stats.skippedB = sideB.server.StatsUVE().entriesSkippedIrrelevant;
    stats.skippedC = sideC.server.StatsUVE().entriesSkippedIrrelevant;

    // A is unbudgeted with everything in radius: it converges the formation. Walker 25
    // phases, so A holds its pose from its last apply tick rather than the final one.
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntitiesUVE; ++slot) {
        check(sideA.seen[slot] == world[slot], "A diverged from the formation");
    }
    check(stats.lastSeenA[5U] > 0U, "A never saw walker 25");
    check(sideA.seen[5U] ==
              Math::Vector3UVE{kWalkerSpacingUVE * 5.0F +
                                   kWalkerSpeedUVE * static_cast<float>(stats.lastSeenA[5U]),
                               0.0F, 0.0F},
          "A diverged on walker 25's last apply");
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntitiesUVE; ++slot) {
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
    for (std::uint64_t slot = 2U; slot < kInterestFanoutEntitiesUVE; ++slot) {
        check(stats.receivedB[slot] == 0U && stats.firstSeenB[slot] == 0U &&
                  stats.lastSeenB[slot] == 0U,
              "B saw past its budget");
    }
    check(stats.skippedB == 4U * config.ticks, "B's skip count drifted from 4 a tick");
    // C watches the formation walk past its window: reverse discovery (nearest first),
    // everyone dwells, everyone leaves, exits in the same order.
    for (std::uint64_t slot = 0U; slot < kInterestFanoutEntitiesUVE; ++slot) {
        check(stats.firstSeenC[slot] > 0U, "C never discovered a walker");
        check(stats.lastSeenC[slot] > stats.firstSeenC[slot], "C never dwelled on a walker");
        check(stats.lastSeenC[slot] < config.ticks, "a walker never left C's window");
    }
    for (std::uint64_t slot = 0U; slot + 1U < kInterestFanoutEntitiesUVE; ++slot) {
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
              SumUVE(stats.receivedA) + SumUVE(stats.receivedB) + SumUVE(stats.receivedC),
          "a selection never applied");
    check(stats.skippedB ==
              stats.fanoutBudgetDropped + (config.ticks - stats.receivedA[5U]),
          "B's skips disagree with the budget drops");
    check(SumUVE(stats.receivedC) + stats.skippedC ==
              kInterestFanoutEntitiesUVE * config.ticks,
          "a C walker/tick neither applied nor skipped");

    log("interest fan-out ran " + std::to_string(config.ticks) + " ticks, A " +
        std::to_string(SumUVE(stats.receivedA)) + " B " + std::to_string(SumUVE(stats.receivedB)) +
        " C " + std::to_string(SumUVE(stats.receivedC)) + " selected " +
        std::to_string(stats.fanoutSelected) + " dropped " +
        std::to_string(stats.fanoutBudgetDropped) + ", " +
        std::to_string(result.failures.size()) + " violations");
    result.success = result.failures.empty();
    return result;
}

} // namespace UVE::Samples
