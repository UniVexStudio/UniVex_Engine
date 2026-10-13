// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_interest_fanout_uve.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] Math::Vector3UVE AtUVE(const float x, const float y = 0.0F, const float z = 0.0F) {
    return Math::Vector3UVE{x, y, z};
}

// One server replicator (Owner, sampling the shared world through the fan-out's
// selection) plus its client (Proxy, applying into seen), over loopback.
struct FanoutClientUVE final {
    LoopbackTransportPairUVE link;
    NetworkReplicatorUVE server;
    NetworkReplicatorUVE client;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> seen;

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
        return server.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               client.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] FanoutClientUVE MakeFanoutClientUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE serverSession(SessionRoleUVE::Client,
                                    std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE clientSession(SessionRoleUVE::Server,
                                    std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkReplicatorUVE server(std::move(serverSession));
    NetworkReplicatorUVE client(std::move(clientSession));
    return FanoutClientUVE{std::move(pair), std::move(server), std::move(client), {}};
}

void RegisterFanoutSideUVE(FanoutClientUVE& side,
                            std::map<NetworkEntityIdUVE, Math::Vector3UVE>& world,
                            NetworkInterestFanoutUVE& fanout,
                            const InterestViewerIdUVE viewer) {
    for (const auto& [netId, _] : world) {
        EXPECT_TRUE(side.server.RegisterEntityUVE(
            netId, [&world, netId]() { return world[netId]; },
            [](const Math::Vector3UVE&) {}, ReplicationAuthorityUVE::Owner,
            [&fanout, viewer, netId]() { return fanout.IsSelectedUVE(viewer, netId); }));
        EXPECT_TRUE(side.client.RegisterEntityUVE(
            netId, [&side, netId]() { return side.seen[netId]; },
            [&side, netId](const Math::Vector3UVE& position) { side.seen[netId] = position; },
            ReplicationAuthorityUVE::Proxy));
    }
}

TEST(NetworkInterestFanoutUVETest, Rank_TiersFirstThenDistance) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(5.0F), false, InterestTierUVE::Critical));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(1.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(24U, AtUVE(0.0F, 1.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(22U, AtUVE(2.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(23U, AtUVE(1.0F), false, InterestTierUVE::Normal));
    // Rate 1 keeps every tier on every refresh: the rank reads the tier, not the rate.
    for (const NetworkEntityIdUVE netId : {20U, 21U, 24U, 22U, 23U}) {
        ASSERT_TRUE(interest.SetEntityRateUVE(netId, 1U));
    }
    NetworkInterestFanoutUVE fanout(interest);
    EXPECT_EQ(fanout.RefreshUVE(1U), 5U);
    EXPECT_EQ(fanout.SelectedEntitiesUVE(1U),
              (std::vector<NetworkEntityIdUVE>{20U, 21U, 24U, 22U, 23U}));
}

TEST(NetworkInterestFanoutUVETest, Budget_CutsFromTheBottom) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(5.0F), false, InterestTierUVE::Critical));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(1.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(22U, AtUVE(2.0F), false, InterestTierUVE::High));
    for (const NetworkEntityIdUVE netId : {20U, 21U, 22U}) {
        ASSERT_TRUE(interest.SetEntityRateUVE(netId, 1U));
    }
    NetworkInterestFanoutUVE fanout(interest);
    fanout.SetViewerBudgetUVE(1U, 2U);
    EXPECT_EQ(fanout.ViewerBudgetUVE(1U), 2U);
    EXPECT_EQ(fanout.RefreshUVE(1U), 2U);
    EXPECT_EQ(fanout.SelectedEntitiesUVE(1U), (std::vector<NetworkEntityIdUVE>{20U, 21U}));
    EXPECT_TRUE(fanout.IsSelectedUVE(1U, 20U));
    EXPECT_TRUE(fanout.IsSelectedUVE(1U, 21U));
    EXPECT_FALSE(fanout.IsSelectedUVE(1U, 22U));
    EXPECT_EQ(fanout.StatsUVE(), (FanoutStatsUVE{1U, 2U, 1U}));
}

TEST(NetworkInterestFanoutUVETest, Budget_ZeroMeansUnlimited) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(1.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(2.0F)));
    NetworkInterestFanoutUVE fanout(interest);
    EXPECT_EQ(fanout.ViewerBudgetUVE(1U), 0U);
    EXPECT_EQ(fanout.RefreshUVE(1U), 2U);
    fanout.SetViewerBudgetUVE(1U, 1U);
    EXPECT_EQ(fanout.RefreshUVE(1U), 1U);
    fanout.SetViewerBudgetUVE(1U, 0U);
    EXPECT_EQ(fanout.ViewerBudgetUVE(1U), 0U);
    EXPECT_EQ(fanout.RefreshUVE(1U), 2U);
}

TEST(NetworkInterestFanoutUVETest, Throttle_ShapesSelection) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(10U, AtUVE(1.0F), false, InterestTierUVE::High));
    NetworkInterestFanoutUVE fanout(interest);
    // The even id ships on even refreshes: off-phase refreshes select nothing.
    std::vector<std::size_t> counts;
    for (int i = 0; i < 4; ++i) {
        counts.push_back(fanout.RefreshUVE(1U));
    }
    EXPECT_EQ(counts, (std::vector<std::size_t>{1U, 0U, 1U, 0U}));
}

TEST(NetworkInterestFanoutUVETest, FailOpen_UnknownsAndUnrefreshedSelect) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(1.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(2.0F)));
    NetworkInterestFanoutUVE fanout(interest);
    EXPECT_TRUE(fanout.IsSelectedUVE(99U, 20U));
    EXPECT_TRUE(fanout.IsSelectedUVE(1U, 99U));
    EXPECT_TRUE(fanout.IsSelectedUVE(1U, 20U));
    EXPECT_TRUE(fanout.SelectedEntitiesUVE(1U).empty());
    EXPECT_EQ(fanout.RefreshUVE(99U), 2U);
    EXPECT_EQ(fanout.SelectedEntitiesUVE(99U), (std::vector<NetworkEntityIdUVE>{20U, 21U}));
}

TEST(NetworkInterestFanoutUVETest, BudgetedUnknown_CutById) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(1.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(2.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(22U, AtUVE(3.0F)));
    NetworkInterestFanoutUVE fanout(interest);
    fanout.SetViewerBudgetUVE(99U, 1U);
    // No viewer position to rank by: the fail-open set cuts in id order.
    EXPECT_EQ(fanout.RefreshUVE(99U), 1U);
    EXPECT_EQ(fanout.SelectedEntitiesUVE(99U), (std::vector<NetworkEntityIdUVE>{20U}));
    EXPECT_EQ(fanout.StatsUVE().budgetDropped, 2U);
}

TEST(NetworkInterestFanoutUVETest, RefreshAll_CoversKnownAndBudgeted) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddViewerUVE(2U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(1.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(2.0F)));
    NetworkInterestFanoutUVE fanout(interest);
    fanout.SetViewerBudgetUVE(1U, 1U);
    fanout.SetViewerBudgetUVE(99U, 2U);
    EXPECT_EQ(fanout.RefreshAllUVE(), 5U);
    EXPECT_EQ(fanout.SelectedEntitiesUVE(1U), (std::vector<NetworkEntityIdUVE>{20U}));
    EXPECT_EQ(fanout.SelectedEntitiesUVE(2U), (std::vector<NetworkEntityIdUVE>{20U, 21U}));
    EXPECT_EQ(fanout.SelectedEntitiesUVE(99U), (std::vector<NetworkEntityIdUVE>{20U, 21U}));
    EXPECT_EQ(fanout.StatsUVE().refreshes, 3U);
}

TEST(NetworkInterestFanoutUVETest, Split_TwoClientsShareOneCore) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddViewerUVE(2U, 100.0F));
    // All Critical (throttle-neutral): distance alone ranks 20 > 21 > 22 > 23.
    ASSERT_TRUE(interest.AddEntityUVE(20U, AtUVE(1.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(21U, AtUVE(2.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(22U, AtUVE(3.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(23U, AtUVE(4.0F)));
    NetworkInterestFanoutUVE fanout(interest);
    fanout.SetViewerBudgetUVE(2U, 1U);

    // The wire truth moves every tick (so deltas always fire); the interest poses above
    // stay put, holding the rank stable.
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> world{
        {20U, AtUVE(10.0F)}, {21U, AtUVE(11.0F)}, {22U, AtUVE(12.0F)}, {23U, AtUVE(13.0F)}};
    FanoutClientUVE side1 = MakeFanoutClientUVE();
    FanoutClientUVE side2 = MakeFanoutClientUVE();
    ASSERT_TRUE(side1.EstablishUVE(1000U));
    ASSERT_TRUE(side2.EstablishUVE(1000U));
    RegisterFanoutSideUVE(side1, world, fanout, 1U);
    RegisterFanoutSideUVE(side2, world, fanout, 2U);

    for (std::uint64_t tick = 0U; tick < 3U; ++tick) {
        for (auto& [netId, position] : world) {
            position = AtUVE(static_cast<float>(netId) + static_cast<float>(tick));
        }
        EXPECT_EQ(fanout.RefreshAllUVE(), 5U);
        side1.UpdateBothUVE(1100U + tick * 100U);
        side2.UpdateBothUVE(1100U + tick * 100U);
    }

    for (const auto& [netId, position] : world) {
        EXPECT_EQ(side1.seen[netId], position) << "unbudgeted client net " << netId;
    }
    EXPECT_EQ(side2.seen[20U], world[20U]);
    EXPECT_EQ(side2.seen[21U], (Math::Vector3UVE{}));
    EXPECT_EQ(side2.seen[22U], (Math::Vector3UVE{}));
    EXPECT_EQ(side2.seen[23U], (Math::Vector3UVE{}));
    EXPECT_EQ(fanout.StatsUVE(), (FanoutStatsUVE{6U, 15U, 9U}));
    EXPECT_EQ(side1.server.StatsUVE().entriesSkippedIrrelevant, 0U);
    EXPECT_EQ(side2.server.StatsUVE().entriesSkippedIrrelevant, 9U);
}

} // namespace
} // namespace UVE::Network::Tests
