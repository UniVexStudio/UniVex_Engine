// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_interest_fanout.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] Math::Vector3 At(const float x, const float y = 0.0F, const float z = 0.0F) {
    return Math::Vector3{x, y, z};
}

// One server replicator (Owner, sampling the shared world through the fan-out's
// selection) plus its client (Proxy, applying into seen), over loopback.
struct FanoutClient final {
    LoopbackTransportPair link;
    NetworkReplicator server;
    NetworkReplicator client;
    std::map<NetworkEntityId, Math::Vector3> seen;

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
        return server.Session().State() == SessionState::Established &&
               client.Session().State() == SessionState::Established;
    }
};

[[nodiscard]] FanoutClient MakeFanoutClient() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession serverSession(SessionRole::Client,
                                    std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession clientSession(SessionRole::Server,
                                    std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkReplicator server(std::move(serverSession));
    NetworkReplicator client(std::move(clientSession));
    return FanoutClient{std::move(pair), std::move(server), std::move(client), {}};
}

void RegisterFanoutSide(FanoutClient& side,
                            std::map<NetworkEntityId, Math::Vector3>& world,
                            NetworkInterestFanout& fanout,
                            const InterestViewerId viewer) {
    for (const auto& [netId, _] : world) {
        EXPECT_TRUE(side.server.RegisterEntity(
            netId, [&world, netId]() { return world[netId]; },
            [](const Math::Vector3&) {}, ReplicationAuthority::Owner,
            [&fanout, viewer, netId]() { return fanout.IsSelected(viewer, netId); }));
        EXPECT_TRUE(side.client.RegisterEntity(
            netId, [&side, netId]() { return side.seen[netId]; },
            [&side, netId](const Math::Vector3& position) { side.seen[netId] = position; },
            ReplicationAuthority::Proxy));
    }
}

TEST(NetworkInterestFanoutTest, Rank_TiersFirstThenDistance) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(20U, At(5.0F), false, InterestTier::Critical));
    ASSERT_TRUE(interest.AddEntity(21U, At(1.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(24U, At(0.0F, 1.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(22U, At(2.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(23U, At(1.0F), false, InterestTier::Normal));
    // Rate 1 keeps every tier on every refresh: the rank reads the tier, not the rate.
    for (const NetworkEntityId netId : {20U, 21U, 24U, 22U, 23U}) {
        ASSERT_TRUE(interest.SetEntityRate(netId, 1U));
    }
    NetworkInterestFanout fanout(interest);
    EXPECT_EQ(fanout.Refresh(1U), 5U);
    EXPECT_EQ(fanout.SelectedEntities(1U),
              (std::vector<NetworkEntityId>{20U, 21U, 24U, 22U, 23U}));
}

TEST(NetworkInterestFanoutTest, Budget_CutsFromTheBottom) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(20U, At(5.0F), false, InterestTier::Critical));
    ASSERT_TRUE(interest.AddEntity(21U, At(1.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(22U, At(2.0F), false, InterestTier::High));
    for (const NetworkEntityId netId : {20U, 21U, 22U}) {
        ASSERT_TRUE(interest.SetEntityRate(netId, 1U));
    }
    NetworkInterestFanout fanout(interest);
    fanout.SetViewerBudget(1U, 2U);
    EXPECT_EQ(fanout.ViewerBudget(1U), 2U);
    EXPECT_EQ(fanout.Refresh(1U), 2U);
    EXPECT_EQ(fanout.SelectedEntities(1U), (std::vector<NetworkEntityId>{20U, 21U}));
    EXPECT_TRUE(fanout.IsSelected(1U, 20U));
    EXPECT_TRUE(fanout.IsSelected(1U, 21U));
    EXPECT_FALSE(fanout.IsSelected(1U, 22U));
    EXPECT_EQ(fanout.Stats(), (FanoutStats{1U, 2U, 1U}));
}

TEST(NetworkInterestFanoutTest, Budget_ZeroMeansUnlimited) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(20U, At(1.0F)));
    ASSERT_TRUE(interest.AddEntity(21U, At(2.0F)));
    NetworkInterestFanout fanout(interest);
    EXPECT_EQ(fanout.ViewerBudget(1U), 0U);
    EXPECT_EQ(fanout.Refresh(1U), 2U);
    fanout.SetViewerBudget(1U, 1U);
    EXPECT_EQ(fanout.Refresh(1U), 1U);
    fanout.SetViewerBudget(1U, 0U);
    EXPECT_EQ(fanout.ViewerBudget(1U), 0U);
    EXPECT_EQ(fanout.Refresh(1U), 2U);
}

TEST(NetworkInterestFanoutTest, Throttle_ShapesSelection) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(10U, At(1.0F), false, InterestTier::High));
    NetworkInterestFanout fanout(interest);
    // The even id ships on even refreshes: off-phase refreshes select nothing.
    std::vector<std::size_t> counts;
    for (int i = 0; i < 4; ++i) {
        counts.push_back(fanout.Refresh(1U));
    }
    EXPECT_EQ(counts, (std::vector<std::size_t>{1U, 0U, 1U, 0U}));
}

TEST(NetworkInterestFanoutTest, FailOpen_UnknownsAndUnrefreshedSelect) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(20U, At(1.0F)));
    ASSERT_TRUE(interest.AddEntity(21U, At(2.0F)));
    NetworkInterestFanout fanout(interest);
    EXPECT_TRUE(fanout.IsSelected(99U, 20U));
    EXPECT_TRUE(fanout.IsSelected(1U, 99U));
    EXPECT_TRUE(fanout.IsSelected(1U, 20U));
    EXPECT_TRUE(fanout.SelectedEntities(1U).empty());
    EXPECT_EQ(fanout.Refresh(99U), 2U);
    EXPECT_EQ(fanout.SelectedEntities(99U), (std::vector<NetworkEntityId>{20U, 21U}));
}

TEST(NetworkInterestFanoutTest, BudgetedUnknown_CutById) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddEntity(20U, At(1.0F)));
    ASSERT_TRUE(interest.AddEntity(21U, At(2.0F)));
    ASSERT_TRUE(interest.AddEntity(22U, At(3.0F)));
    NetworkInterestFanout fanout(interest);
    fanout.SetViewerBudget(99U, 1U);
    // No viewer position to rank by: the fail-open set cuts in id order.
    EXPECT_EQ(fanout.Refresh(99U), 1U);
    EXPECT_EQ(fanout.SelectedEntities(99U), (std::vector<NetworkEntityId>{20U}));
    EXPECT_EQ(fanout.Stats().budgetDropped, 2U);
}

TEST(NetworkInterestFanoutTest, RefreshAll_CoversKnownAndBudgeted) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddViewer(2U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(20U, At(1.0F)));
    ASSERT_TRUE(interest.AddEntity(21U, At(2.0F)));
    NetworkInterestFanout fanout(interest);
    fanout.SetViewerBudget(1U, 1U);
    fanout.SetViewerBudget(99U, 2U);
    EXPECT_EQ(fanout.RefreshAll(), 5U);
    EXPECT_EQ(fanout.SelectedEntities(1U), (std::vector<NetworkEntityId>{20U}));
    EXPECT_EQ(fanout.SelectedEntities(2U), (std::vector<NetworkEntityId>{20U, 21U}));
    EXPECT_EQ(fanout.SelectedEntities(99U), (std::vector<NetworkEntityId>{20U, 21U}));
    EXPECT_EQ(fanout.Stats().refreshes, 3U);
}

TEST(NetworkInterestFanoutTest, Split_TwoClientsShareOneCore) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddViewer(2U, 100.0F));
    // All Critical (throttle-neutral): distance alone ranks 20 > 21 > 22 > 23.
    ASSERT_TRUE(interest.AddEntity(20U, At(1.0F)));
    ASSERT_TRUE(interest.AddEntity(21U, At(2.0F)));
    ASSERT_TRUE(interest.AddEntity(22U, At(3.0F)));
    ASSERT_TRUE(interest.AddEntity(23U, At(4.0F)));
    NetworkInterestFanout fanout(interest);
    fanout.SetViewerBudget(2U, 1U);

    // The wire truth moves every tick (so deltas always fire); the interest poses above
    // stay put, holding the rank stable.
    std::map<NetworkEntityId, Math::Vector3> world{
        {20U, At(10.0F)}, {21U, At(11.0F)}, {22U, At(12.0F)}, {23U, At(13.0F)}};
    FanoutClient side1 = MakeFanoutClient();
    FanoutClient side2 = MakeFanoutClient();
    ASSERT_TRUE(side1.Establish(1000U));
    ASSERT_TRUE(side2.Establish(1000U));
    RegisterFanoutSide(side1, world, fanout, 1U);
    RegisterFanoutSide(side2, world, fanout, 2U);

    for (std::uint64_t tick = 0U; tick < 3U; ++tick) {
        for (auto& [netId, position] : world) {
            position = At(static_cast<float>(netId) + static_cast<float>(tick));
        }
        EXPECT_EQ(fanout.RefreshAll(), 5U);
        side1.UpdateBoth(1100U + tick * 100U);
        side2.UpdateBoth(1100U + tick * 100U);
    }

    for (const auto& [netId, position] : world) {
        EXPECT_EQ(side1.seen[netId], position) << "unbudgeted client net " << netId;
    }
    EXPECT_EQ(side2.seen[20U], world[20U]);
    EXPECT_EQ(side2.seen[21U], (Math::Vector3{}));
    EXPECT_EQ(side2.seen[22U], (Math::Vector3{}));
    EXPECT_EQ(side2.seen[23U], (Math::Vector3{}));
    EXPECT_EQ(fanout.Stats(), (FanoutStats{6U, 15U, 9U}));
    EXPECT_EQ(side1.server.Stats().entriesSkippedIrrelevant, 0U);
    EXPECT_EQ(side2.server.Stats().entriesSkippedIrrelevant, 9U);
}

} // namespace
} // namespace UVE::Network::Tests
