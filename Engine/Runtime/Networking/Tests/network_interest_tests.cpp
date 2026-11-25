// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_interest.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Distances below use axis-aligned and 3-4-5 layouts, so every squared distance is exactly
// representable and every verdict is arithmetic, never rounding luck.

[[nodiscard]] Math::Vector3 At(const float x, const float y = 0.0F, const float z = 0.0F) {
    return Math::Vector3{x, y, z};
}

TEST(NetworkInterestTest, Registration_DuplicatesAndBadParametersRejected) {
    NetworkInterest interest;
    EXPECT_TRUE(interest.AddViewer(1U, 10.0F));
    EXPECT_FALSE(interest.AddViewer(1U, 10.0F));
    EXPECT_FALSE(interest.AddViewer(kInvalidInterestViewer, 10.0F));
    EXPECT_FALSE(interest.AddViewer(2U, 0.0F));
    EXPECT_FALSE(interest.AddViewer(2U, -5.0F));
    EXPECT_FALSE(
        interest.AddViewer(2U, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(interest.AddViewer(2U, 10.0F, At(2000000.0F, 0.0F, 0.0F)));
    EXPECT_EQ(interest.ViewerCount(), 1U);

    EXPECT_TRUE(interest.AddEntity(7U, At(1.0F, 2.0F, 3.0F)));
    EXPECT_FALSE(interest.AddEntity(7U));
    EXPECT_FALSE(interest.AddEntity(8U, At(0.0F, 0.0F, 2000000.0F)));
    EXPECT_EQ(interest.EntityCount(), 1U);

    EXPECT_FALSE(interest.RemoveViewer(99U));
    EXPECT_FALSE(interest.RemoveEntity(99U));
    EXPECT_TRUE(interest.RemoveViewer(1U));
    EXPECT_TRUE(interest.RemoveEntity(7U));
    EXPECT_EQ(interest.ViewerCount(), 0U);
    EXPECT_EQ(interest.EntityCount(), 0U);
    // Removal frees the id.
    EXPECT_TRUE(interest.AddViewer(1U, 10.0F));
    EXPECT_TRUE(interest.AddEntity(7U));
}

TEST(NetworkInterestTest, Distance_InsideBoundaryAndOutside) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(6.0F, 8.0F)));   // d = 10, the boundary: in.
    ASSERT_TRUE(interest.AddEntity(9U, At(3.0F, 4.0F)));   // d = 5: in.
    ASSERT_TRUE(interest.AddEntity(11U, At(10.5F)));       // band, never judged: out.
    ASSERT_TRUE(interest.AddEntity(13U, At(15.0F)));       // past radius + margin: out.
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    EXPECT_TRUE(interest.IsRelevant(1U, 9U));
    EXPECT_FALSE(interest.IsRelevant(1U, 11U));
    EXPECT_FALSE(interest.IsRelevant(1U, 13U));
}

TEST(NetworkInterestTest, AlwaysRelevant_SkipsDistanceEntirely) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(1000.0F, 0.0F, 0.0F), true));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    EXPECT_EQ(interest.RelevantEntities(1U), std::vector<NetworkEntityId>{7U});
    EXPECT_EQ(interest.Stats().culled, 0U);
}

TEST(NetworkInterestTest, Hysteresis_BandHoldsBothWays) {
    NetworkInterest interest; // Radius 10, margin 2: the band is (10, 12].
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(5.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    // Drifts into the band: still relevant.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(11.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    // Past the margin: culled, and the band forgets the other way too.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(13.0F)));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
    ASSERT_TRUE(interest.UpdateEntity(7U, At(11.0F)));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
    // Back inside: relevant again.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(9.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
}

TEST(NetworkInterestTest, Grid_AdjacentCellsAndSameCellJudgeByDistance) {
    NetworkInterest interest; // 16m cells: the viewer sits in cell 1, not cell 0.
    ASSERT_TRUE(interest.AddViewer(1U, 5.0F, At(16.0F)));
    // Cell 0, four metres off: relevant across the cell border.
    ASSERT_TRUE(interest.AddEntity(7U, At(12.0F)));
    // Cell 1 like the viewer, fourteen metres off: culled within the cell.
    ASSERT_TRUE(interest.AddEntity(9U, At(30.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    EXPECT_FALSE(interest.IsRelevant(1U, 9U));
    EXPECT_EQ(interest.RelevantEntities(1U), std::vector<NetworkEntityId>{7U});
}

TEST(NetworkInterestTest, RelevantEntities_ExactSortedSetAcrossCells) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(11U, At(3.0F)));        // in, cell 0.
    ASSERT_TRUE(interest.AddEntity(7U, At(6.0F, 8.0F)));   // in (d = 10), cell 0.
    ASSERT_TRUE(interest.AddEntity(15U, At(-6.0F, -8.0F))); // in, negative cells.
    ASSERT_TRUE(interest.AddEntity(13U, At(11.0F)));       // band, unseen: out.
    ASSERT_TRUE(interest.AddEntity(9U, At(50.0F)));        // far cell: out.
    EXPECT_EQ(interest.RelevantEntities(1U),
              (std::vector<NetworkEntityId>{7U, 11U, 15U}));
}

TEST(NetworkInterestTest, RelevantEntities_LearnsBandVerdictsLikeQueries) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(5.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    // Relevant, then drifts into the band: the refresh keeps it, like a query would.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(11.0F)));
    EXPECT_EQ(interest.RelevantEntities(1U), std::vector<NetworkEntityId>{7U});
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    // Past the margin: the refresh drops it, and queries agree.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(20.0F)));
    EXPECT_TRUE(interest.RelevantEntities(1U).empty());
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
}

TEST(NetworkInterestTest, Moves_CrossCellsAndRefuseBadUpdates) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(2.0F)));
    ASSERT_EQ(interest.RelevantEntities(1U), std::vector<NetworkEntityId>{7U});

    // Walks from cell 0 to cell 3: the grid follows, the verdict flips.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(50.0F)));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
    EXPECT_TRUE(interest.RelevantEntities(1U).empty());
    // The viewer walks after it: relevant again from the new cell.
    ASSERT_TRUE(interest.UpdateViewer(1U, At(45.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    EXPECT_FALSE(interest.UpdateEntity(99U, At(0.0F)));
    EXPECT_FALSE(interest.UpdateViewer(99U, At(0.0F)));
    EXPECT_FALSE(interest.UpdateEntity(7U, At(2000000.0F, 0.0F, 0.0F)));
    EXPECT_FALSE(interest.UpdateViewer(
        1U, At(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F)));
    // Refused updates move nothing: the pair is still relevant.
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
}

TEST(NetworkInterestTest, Removal_ForgetsLearnedVerdicts) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(5.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    // Re-added in the band: no ghost of the old verdict, so the unseen rule applies.
    ASSERT_TRUE(interest.RemoveEntity(7U));
    ASSERT_TRUE(interest.AddEntity(7U, At(11.0F)));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));

    // Same for viewers: a fresh viewer has no memory of the band.
    ASSERT_TRUE(interest.UpdateEntity(7U, At(5.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    ASSERT_TRUE(interest.RemoveViewer(1U));
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.UpdateEntity(7U, At(11.0F)));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
}

TEST(NetworkInterestTest, Config_BadCellsAndMarginsFallBack) {
    EXPECT_EQ(NetworkInterest(InterestConfig{0.0F, -1.0F}).CellSize(),
              kInterestDefaultCellSize);
    EXPECT_EQ(NetworkInterest(InterestConfig{0.0F, -1.0F}).HysteresisMargin(), 0.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(NetworkInterest(InterestConfig{nan, nan}).CellSize(),
              kInterestDefaultCellSize);
    EXPECT_EQ(NetworkInterest(InterestConfig{nan, nan}).HysteresisMargin(), 0.0F);
    EXPECT_EQ(NetworkInterest(InterestConfig{8.0F, 5.0F}).CellSize(), 8.0F);
    EXPECT_EQ(NetworkInterest(InterestConfig{8.0F, 5.0F}).HysteresisMargin(), 5.0F);

    // Zero margin means no band: past the radius is culled, on it is relevant.
    NetworkInterest crisp(InterestConfig{16.0F, 0.0F});
    ASSERT_TRUE(crisp.AddViewer(1U, 10.0F));
    ASSERT_TRUE(crisp.AddEntity(7U, At(10.0F)));
    ASSERT_TRUE(crisp.AddEntity(9U, At(10.5F)));
    EXPECT_TRUE(crisp.IsRelevant(1U, 7U));
    EXPECT_FALSE(crisp.IsRelevant(1U, 9U));
}

TEST(NetworkInterestTest, Stats_CountQueriesCullsAndUnknowns) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(5.0F)));
    ASSERT_TRUE(interest.AddEntity(9U, At(50.0F)));

    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    EXPECT_FALSE(interest.IsRelevant(1U, 9U));
    // Unknown ids fail open - each way, and both at once.
    EXPECT_TRUE(interest.IsRelevant(1U, 99U));
    EXPECT_TRUE(interest.IsRelevant(99U, 7U));
    EXPECT_TRUE(interest.IsRelevant(99U, 99U));
    EXPECT_EQ(interest.RelevantEntities(1U), std::vector<NetworkEntityId>{7U});
    // An unknown viewer gets every entity: fail-open over the whole world.
    EXPECT_EQ(interest.RelevantEntities(99U),
              (std::vector<NetworkEntityId>{7U, 9U}));
    EXPECT_EQ(interest.Stats(), (InterestStats{
                                      .queries = 5U,
                                      .relevant = 4U,
                                      .culled = 1U,
                                      .refreshes = 2U,
                                      .unknownIds = 4U,
                                  }));
}

TEST(NetworkInterestTest, EmptyWorld_AnswersHonestly) {
    NetworkInterest interest;
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    EXPECT_TRUE(interest.RelevantEntities(1U).empty());
    EXPECT_EQ(interest.Stats().unknownIds, 2U);
}

TEST(NetworkInterestTest, Integration_RelevancyFiltersReplicateOnlyWhatMatters) {
    // The seam the authority slice left behind: per-entity relevancy closures ask interest,
    // and the replicator sends only what matters - near ships, far skips, and walking
    // across the boundary flips the flow live.
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkReplicator a(std::move(sessionA), 0U);
    NetworkReplicator b(std::move(sessionB), 0U);
    ASSERT_TRUE(a.Session().Connect(1000U));
    a.Update(1000U);
    b.Update(1000U);
    a.Update(1000U);
    b.Update(1000U);
    ASSERT_EQ(a.Session().State(), SessionState::Established);

    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    std::map<NetworkEntityId, Math::Vector3> positions{{7U, At(2.0F)},
                                                             {9U, At(50.0F)}};
    ASSERT_TRUE(interest.AddEntity(7U, positions.at(7U)));
    ASSERT_TRUE(interest.AddEntity(9U, positions.at(9U)));
    const auto sampler = [&positions](const NetworkEntityId netId) {
        return positions.at(netId);
    };
    ASSERT_TRUE(a.RegisterEntity(
        7U, [&sampler] { return sampler(7U); }, [](const Math::Vector3&) {},
        ReplicationAuthority::Owner,
        [&interest] { return interest.IsRelevant(1U, 7U); }));
    ASSERT_TRUE(a.RegisterEntity(
        9U, [&sampler] { return sampler(9U); }, [](const Math::Vector3&) {},
        ReplicationAuthority::Owner,
        [&interest] { return interest.IsRelevant(1U, 9U); }));
    std::map<NetworkEntityId, Math::Vector3> applied;
    ASSERT_TRUE(b.RegisterEntity(
        7U, [] { return Math::Vector3{}; },
        [&applied](const Math::Vector3& position) { applied[7U] = position; },
        ReplicationAuthority::Proxy));
    ASSERT_TRUE(b.RegisterEntity(
        9U, [] { return Math::Vector3{}; },
        [&applied](const Math::Vector3& position) { applied[9U] = position; },
        ReplicationAuthority::Proxy));

    // Near ships, far skips.
    a.Update(1100U);
    b.Update(1100U);
    EXPECT_EQ(applied.count(7U), 1U);
    EXPECT_EQ(applied.at(7U), At(2.0F));
    EXPECT_EQ(applied.count(9U), 0U);
    EXPECT_EQ(a.Stats().entriesSkippedIrrelevant, 1U);

    // The far entity walks into range: both ship now.
    positions.at(9U) = At(3.0F);
    ASSERT_TRUE(interest.UpdateEntity(9U, positions.at(9U)));
    a.Update(1200U);
    b.Update(1200U);
    EXPECT_EQ(applied.at(9U), At(3.0F));
    EXPECT_EQ(a.Stats().entriesSkippedIrrelevant, 1U);

    // The near one walks out: its last pose goes stale on the peer, provably unsent.
    positions.at(7U) = At(50.0F);
    ASSERT_TRUE(interest.UpdateEntity(7U, positions.at(7U)));
    a.Update(1300U);
    b.Update(1300U);
    EXPECT_EQ(applied.at(7U), At(2.0F));
    EXPECT_EQ(a.Stats().entriesSkippedIrrelevant, 2U);
}


TEST(NetworkInterestTest, TierDivisors_ThrottleToCadence) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(10U, At(1.0F), false, InterestTier::Critical));
    ASSERT_TRUE(interest.AddEntity(11U, At(1.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(12U, At(1.0F), false, InterestTier::Normal));
    ASSERT_TRUE(interest.AddEntity(13U, At(1.0F), false, InterestTier::Low));
    std::uint64_t counts[4] = {};
    for (int i = 0; i < 8; ++i) {
        counts[0] += interest.IsRelevant(1U, 10U) ? 1U : 0U;
        counts[1] += interest.IsRelevant(1U, 11U) ? 1U : 0U;
        counts[2] += interest.IsRelevant(1U, 12U) ? 1U : 0U;
        counts[3] += interest.IsRelevant(1U, 13U) ? 1U : 0U;
    }
    EXPECT_EQ(counts[0], 8U);
    EXPECT_EQ(counts[1], 4U);
    EXPECT_EQ(counts[2], 2U);
    EXPECT_EQ(counts[3], 1U);
    EXPECT_EQ(interest.Stats().throttled, 17U);
    EXPECT_EQ(interest.Stats().culled, 0U);
}

TEST(NetworkInterestTest, Stagger_IdsShareTheTierSmoothly) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(10U, At(1.0F), false, InterestTier::High));
    ASSERT_TRUE(interest.AddEntity(11U, At(1.0F), false, InterestTier::High));
    // Divisor 2: the even id ships on even ticks, the odd id on odd ticks - never together.
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(interest.IsRelevant(1U, 10U), i % 2 == 0) << "tick " << i;
        EXPECT_EQ(interest.IsRelevant(1U, 11U), i % 2 != 0) << "tick " << i;
    }
}

TEST(NetworkInterestTest, DefaultTier_CriticalSendsImmediately) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(7U, At(1.0F)));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
    ASSERT_TRUE(interest.EntityTier(7U).has_value());
    EXPECT_EQ(*interest.EntityTier(7U), InterestTier::Critical);
    ASSERT_TRUE(interest.EntityRate(7U).has_value());
    EXPECT_EQ(*interest.EntityRate(7U), 0U);
}

TEST(NetworkInterestTest, RateOverride_BeatsTier) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(8U, At(1.0F), false, InterestTier::Normal));
    ASSERT_TRUE(interest.SetEntityRate(8U, 3U));
    std::uint64_t sends = 0U;
    for (int i = 0; i < 9; ++i) {
        sends += interest.IsRelevant(1U, 8U) ? 1U : 0U;
    }
    EXPECT_EQ(sends, 3U);
    // Zero clears back to the tier - and the phase carries over, so the next tier tick
    // is the 4th query, not the 1st.
    ASSERT_TRUE(interest.SetEntityRate(8U, 0U));
    std::vector<bool> pattern;
    for (int i = 0; i < 4; ++i) {
        pattern.push_back(interest.IsRelevant(1U, 8U));
    }
    EXPECT_EQ(pattern, (std::vector<bool>{false, false, false, true}));
}

TEST(NetworkInterestTest, AlwaysRelevant_ThrottledButNeverCulled) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(
        interest.AddEntity(8U, At(1000.0F, 0.0F, 0.0F), true, InterestTier::Low));
    std::uint64_t sends = 0U;
    for (int i = 0; i < 8; ++i) {
        sends += interest.IsRelevant(1U, 8U) ? 1U : 0U;
    }
    EXPECT_EQ(sends, 1U);
    EXPECT_EQ(interest.Stats().throttled, 7U);
    EXPECT_EQ(interest.Stats().culled, 0U);
    EXPECT_EQ(interest.Stats().relevant, 1U);
}

TEST(NetworkInterestTest, Bulk_RespectsThrottle) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(10U, At(1.0F), false, InterestTier::Critical));
    ASSERT_TRUE(interest.AddEntity(11U, At(1.0F), false, InterestTier::Low));
    std::uint64_t critical = 0U;
    std::uint64_t low = 0U;
    for (int i = 0; i < 8; ++i) {
        const std::vector<NetworkEntityId> relevant = interest.RelevantEntities(1U);
        for (const NetworkEntityId netId : relevant) {
            critical += netId == 10U ? 1U : 0U;
            low += netId == 11U ? 1U : 0U;
        }
    }
    EXPECT_EQ(critical, 8U);
    EXPECT_EQ(low, 1U);
    // Fail-open bulk stays unthrottled: an unknown viewer gets everything.
    EXPECT_EQ(interest.RelevantEntities(99U),
              (std::vector<NetworkEntityId>{10U, 11U}));
}

TEST(NetworkInterestTest, UnknownIds_NeverThrottled) {
    NetworkInterest interest;
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(interest.IsRelevant(99U, 7U));
    }
    EXPECT_EQ(interest.Stats().unknownIds, 8U);
    EXPECT_EQ(interest.Stats().throttled, 0U);
}

TEST(NetworkInterestTest, CulledTicks_ConsumePhase) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(8U, At(100.0F), false, InterestTier::Low));
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(interest.IsRelevant(1U, 8U));
    }
    ASSERT_TRUE(interest.UpdateEntity(8U, At(1.0F)));
    std::vector<bool> pattern;
    for (int i = 0; i < 8; ++i) {
        pattern.push_back(interest.IsRelevant(1U, 8U));
    }
    // Ticks 0-2 were culled but counted: the phase resumes at tick 3, sending on the 6th.
    EXPECT_EQ(pattern,
              (std::vector<bool>{false, false, false, false, false, true, false, false}));
    EXPECT_EQ(interest.Stats().culled, 3U);
    EXPECT_EQ(interest.Stats().throttled, 7U);
    EXPECT_EQ(interest.Stats().relevant, 1U);
}

TEST(NetworkInterestTest, Removal_ResetsPhase) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntity(8U, At(1.0F), false, InterestTier::Low));
    EXPECT_TRUE(interest.IsRelevant(1U, 8U));
    ASSERT_TRUE(interest.RemoveEntity(8U));
    ASSERT_TRUE(interest.AddEntity(8U, At(1.0F), false, InterestTier::Low));
    // The clock was swept with the pair: tick 0 sends again instead of skipping to tick 8.
    EXPECT_TRUE(interest.IsRelevant(1U, 8U));
}

TEST(NetworkInterestTest, TierAndRate_UnknownIdsRefused) {
    NetworkInterest interest;
    EXPECT_FALSE(interest.SetEntityTier(99U, InterestTier::High));
    EXPECT_FALSE(interest.SetEntityRate(99U, 3U));
    EXPECT_FALSE(interest.EntityTier(99U).has_value());
    EXPECT_FALSE(interest.EntityRate(99U).has_value());
}

TEST(NetworkInterestTest, Viewers_ListsSorted) {
    NetworkInterest interest;
    EXPECT_TRUE(interest.Viewers().empty());
    ASSERT_TRUE(interest.AddViewer(3U, 10.0F));
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddViewer(2U, 10.0F));
    EXPECT_EQ(interest.Viewers(), (std::vector<InterestViewerId>{1U, 2U, 3U}));
    ASSERT_TRUE(interest.RemoveViewer(2U));
    EXPECT_EQ(interest.Viewers(), (std::vector<InterestViewerId>{1U, 3U}));
}

TEST(NetworkInterestTest, Positions_ReadBackAndRefuseUnknowns) {
    NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F, At(4.0F, 5.0F, 6.0F)));
    ASSERT_TRUE(interest.AddEntity(7U, At(1.0F, 2.0F, 3.0F)));
    ASSERT_TRUE(interest.ViewerPosition(1U).has_value());
    EXPECT_EQ(*interest.ViewerPosition(1U), At(4.0F, 5.0F, 6.0F));
    ASSERT_TRUE(interest.EntityPosition(7U).has_value());
    EXPECT_EQ(*interest.EntityPosition(7U), At(1.0F, 2.0F, 3.0F));
    EXPECT_FALSE(interest.ViewerPosition(99U).has_value());
    EXPECT_FALSE(interest.EntityPosition(99U).has_value());
}
} // namespace
} // namespace UVE::Network::Tests
