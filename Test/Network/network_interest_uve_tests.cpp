// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_interest_uve.h"

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

[[nodiscard]] Math::Vector3UVE AtUVE(const float x, const float y = 0.0F, const float z = 0.0F) {
    return Math::Vector3UVE{x, y, z};
}

TEST(NetworkInterestUVETest, Registration_DuplicatesAndBadParametersRejected) {
    NetworkInterestUVE interest;
    EXPECT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    EXPECT_FALSE(interest.AddViewerUVE(1U, 10.0F));
    EXPECT_FALSE(interest.AddViewerUVE(kInvalidInterestViewerUVE, 10.0F));
    EXPECT_FALSE(interest.AddViewerUVE(2U, 0.0F));
    EXPECT_FALSE(interest.AddViewerUVE(2U, -5.0F));
    EXPECT_FALSE(
        interest.AddViewerUVE(2U, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(interest.AddViewerUVE(2U, 10.0F, AtUVE(2000000.0F, 0.0F, 0.0F)));
    EXPECT_EQ(interest.ViewerCountUVE(), 1U);

    EXPECT_TRUE(interest.AddEntityUVE(7U, AtUVE(1.0F, 2.0F, 3.0F)));
    EXPECT_FALSE(interest.AddEntityUVE(7U));
    EXPECT_FALSE(interest.AddEntityUVE(8U, AtUVE(0.0F, 0.0F, 2000000.0F)));
    EXPECT_EQ(interest.EntityCountUVE(), 1U);

    EXPECT_FALSE(interest.RemoveViewerUVE(99U));
    EXPECT_FALSE(interest.RemoveEntityUVE(99U));
    EXPECT_TRUE(interest.RemoveViewerUVE(1U));
    EXPECT_TRUE(interest.RemoveEntityUVE(7U));
    EXPECT_EQ(interest.ViewerCountUVE(), 0U);
    EXPECT_EQ(interest.EntityCountUVE(), 0U);
    // Removal frees the id.
    EXPECT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    EXPECT_TRUE(interest.AddEntityUVE(7U));
}

TEST(NetworkInterestUVETest, Distance_InsideBoundaryAndOutside) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(6.0F, 8.0F)));   // d = 10, the boundary: in.
    ASSERT_TRUE(interest.AddEntityUVE(9U, AtUVE(3.0F, 4.0F)));   // d = 5: in.
    ASSERT_TRUE(interest.AddEntityUVE(11U, AtUVE(10.5F)));       // band, never judged: out.
    ASSERT_TRUE(interest.AddEntityUVE(13U, AtUVE(15.0F)));       // past radius + margin: out.
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 9U));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 11U));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 13U));
}

TEST(NetworkInterestUVETest, AlwaysRelevant_SkipsDistanceEntirely) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(1000.0F, 0.0F, 0.0F), true));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_EQ(interest.RelevantEntitiesUVE(1U), std::vector<NetworkEntityIdUVE>{7U});
    EXPECT_EQ(interest.StatsUVE().culled, 0U);
}

TEST(NetworkInterestUVETest, Hysteresis_BandHoldsBothWays) {
    NetworkInterestUVE interest; // Radius 10, margin 2: the band is (10, 12].
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(5.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    // Drifts into the band: still relevant.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(11.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    // Past the margin: culled, and the band forgets the other way too.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(13.0F)));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(11.0F)));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
    // Back inside: relevant again.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(9.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
}

TEST(NetworkInterestUVETest, Grid_AdjacentCellsAndSameCellJudgeByDistance) {
    NetworkInterestUVE interest; // 16m cells: the viewer sits in cell 1, not cell 0.
    ASSERT_TRUE(interest.AddViewerUVE(1U, 5.0F, AtUVE(16.0F)));
    // Cell 0, four metres off: relevant across the cell border.
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(12.0F)));
    // Cell 1 like the viewer, fourteen metres off: culled within the cell.
    ASSERT_TRUE(interest.AddEntityUVE(9U, AtUVE(30.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 9U));
    EXPECT_EQ(interest.RelevantEntitiesUVE(1U), std::vector<NetworkEntityIdUVE>{7U});
}

TEST(NetworkInterestUVETest, RelevantEntities_ExactSortedSetAcrossCells) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(11U, AtUVE(3.0F)));        // in, cell 0.
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(6.0F, 8.0F)));   // in (d = 10), cell 0.
    ASSERT_TRUE(interest.AddEntityUVE(15U, AtUVE(-6.0F, -8.0F))); // in, negative cells.
    ASSERT_TRUE(interest.AddEntityUVE(13U, AtUVE(11.0F)));       // band, unseen: out.
    ASSERT_TRUE(interest.AddEntityUVE(9U, AtUVE(50.0F)));        // far cell: out.
    EXPECT_EQ(interest.RelevantEntitiesUVE(1U),
              (std::vector<NetworkEntityIdUVE>{7U, 11U, 15U}));
}

TEST(NetworkInterestUVETest, RelevantEntities_LearnsBandVerdictsLikeQueries) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(5.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    // Relevant, then drifts into the band: the refresh keeps it, like a query would.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(11.0F)));
    EXPECT_EQ(interest.RelevantEntitiesUVE(1U), std::vector<NetworkEntityIdUVE>{7U});
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    // Past the margin: the refresh drops it, and queries agree.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(20.0F)));
    EXPECT_TRUE(interest.RelevantEntitiesUVE(1U).empty());
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
}

TEST(NetworkInterestUVETest, Moves_CrossCellsAndRefuseBadUpdates) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(2.0F)));
    ASSERT_EQ(interest.RelevantEntitiesUVE(1U), std::vector<NetworkEntityIdUVE>{7U});

    // Walks from cell 0 to cell 3: the grid follows, the verdict flips.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(50.0F)));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_TRUE(interest.RelevantEntitiesUVE(1U).empty());
    // The viewer walks after it: relevant again from the new cell.
    ASSERT_TRUE(interest.UpdateViewerUVE(1U, AtUVE(45.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    EXPECT_FALSE(interest.UpdateEntityUVE(99U, AtUVE(0.0F)));
    EXPECT_FALSE(interest.UpdateViewerUVE(99U, AtUVE(0.0F)));
    EXPECT_FALSE(interest.UpdateEntityUVE(7U, AtUVE(2000000.0F, 0.0F, 0.0F)));
    EXPECT_FALSE(interest.UpdateViewerUVE(
        1U, AtUVE(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F)));
    // Refused updates move nothing: the pair is still relevant.
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
}

TEST(NetworkInterestUVETest, Removal_ForgetsLearnedVerdicts) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(5.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    // Re-added in the band: no ghost of the old verdict, so the unseen rule applies.
    ASSERT_TRUE(interest.RemoveEntityUVE(7U));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(11.0F)));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));

    // Same for viewers: a fresh viewer has no memory of the band.
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(5.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    ASSERT_TRUE(interest.RemoveViewerUVE(1U));
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, AtUVE(11.0F)));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
}

TEST(NetworkInterestUVETest, Config_BadCellsAndMarginsFallBack) {
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{0.0F, -1.0F}).CellSizeUVE(),
              kInterestDefaultCellSizeUVE);
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{0.0F, -1.0F}).HysteresisMarginUVE(), 0.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{nan, nan}).CellSizeUVE(),
              kInterestDefaultCellSizeUVE);
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{nan, nan}).HysteresisMarginUVE(), 0.0F);
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{8.0F, 5.0F}).CellSizeUVE(), 8.0F);
    EXPECT_EQ(NetworkInterestUVE(InterestConfigUVE{8.0F, 5.0F}).HysteresisMarginUVE(), 5.0F);

    // Zero margin means no band: past the radius is culled, on it is relevant.
    NetworkInterestUVE crisp(InterestConfigUVE{16.0F, 0.0F});
    ASSERT_TRUE(crisp.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(crisp.AddEntityUVE(7U, AtUVE(10.0F)));
    ASSERT_TRUE(crisp.AddEntityUVE(9U, AtUVE(10.5F)));
    EXPECT_TRUE(crisp.IsRelevantUVE(1U, 7U));
    EXPECT_FALSE(crisp.IsRelevantUVE(1U, 9U));
}

TEST(NetworkInterestUVETest, Stats_CountQueriesCullsAndUnknowns) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(5.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(9U, AtUVE(50.0F)));

    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 9U));
    // Unknown ids fail open - each way, and both at once.
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 99U));
    EXPECT_TRUE(interest.IsRelevantUVE(99U, 7U));
    EXPECT_TRUE(interest.IsRelevantUVE(99U, 99U));
    EXPECT_EQ(interest.RelevantEntitiesUVE(1U), std::vector<NetworkEntityIdUVE>{7U});
    // An unknown viewer gets every entity: fail-open over the whole world.
    EXPECT_EQ(interest.RelevantEntitiesUVE(99U),
              (std::vector<NetworkEntityIdUVE>{7U, 9U}));
    EXPECT_EQ(interest.StatsUVE(), (InterestStatsUVE{
                                      .queries = 5U,
                                      .relevant = 4U,
                                      .culled = 1U,
                                      .refreshes = 2U,
                                      .unknownIds = 4U,
                                  }));
}

TEST(NetworkInterestUVETest, EmptyWorld_AnswersHonestly) {
    NetworkInterestUVE interest;
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_TRUE(interest.RelevantEntitiesUVE(1U).empty());
    EXPECT_EQ(interest.StatsUVE().unknownIds, 2U);
}

TEST(NetworkInterestUVETest, Integration_RelevancyFiltersReplicateOnlyWhatMatters) {
    // The seam the authority slice left behind: per-entity relevancy closures ask interest,
    // and the replicator sends only what matters - near ships, far skips, and walking
    // across the boundary flips the flow live.
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkReplicatorUVE a(std::move(sessionA), 0U);
    NetworkReplicatorUVE b(std::move(sessionB), 0U);
    ASSERT_TRUE(a.SessionUVE().ConnectUVE(1000U));
    a.UpdateUVE(1000U);
    b.UpdateUVE(1000U);
    a.UpdateUVE(1000U);
    b.UpdateUVE(1000U);
    ASSERT_EQ(a.SessionUVE().StateUVE(), SessionStateUVE::Established);

    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positions{{7U, AtUVE(2.0F)},
                                                             {9U, AtUVE(50.0F)}};
    ASSERT_TRUE(interest.AddEntityUVE(7U, positions.at(7U)));
    ASSERT_TRUE(interest.AddEntityUVE(9U, positions.at(9U)));
    const auto sampler = [&positions](const NetworkEntityIdUVE netId) {
        return positions.at(netId);
    };
    ASSERT_TRUE(a.RegisterEntityUVE(
        7U, [&sampler] { return sampler(7U); }, [](const Math::Vector3UVE&) {},
        ReplicationAuthorityUVE::Owner,
        [&interest] { return interest.IsRelevantUVE(1U, 7U); }));
    ASSERT_TRUE(a.RegisterEntityUVE(
        9U, [&sampler] { return sampler(9U); }, [](const Math::Vector3UVE&) {},
        ReplicationAuthorityUVE::Owner,
        [&interest] { return interest.IsRelevantUVE(1U, 9U); }));
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> applied;
    ASSERT_TRUE(b.RegisterEntityUVE(
        7U, [] { return Math::Vector3UVE{}; },
        [&applied](const Math::Vector3UVE& position) { applied[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(b.RegisterEntityUVE(
        9U, [] { return Math::Vector3UVE{}; },
        [&applied](const Math::Vector3UVE& position) { applied[9U] = position; },
        ReplicationAuthorityUVE::Proxy));

    // Near ships, far skips.
    a.UpdateUVE(1100U);
    b.UpdateUVE(1100U);
    EXPECT_EQ(applied.count(7U), 1U);
    EXPECT_EQ(applied.at(7U), AtUVE(2.0F));
    EXPECT_EQ(applied.count(9U), 0U);
    EXPECT_EQ(a.StatsUVE().entriesSkippedIrrelevant, 1U);

    // The far entity walks into range: both ship now.
    positions.at(9U) = AtUVE(3.0F);
    ASSERT_TRUE(interest.UpdateEntityUVE(9U, positions.at(9U)));
    a.UpdateUVE(1200U);
    b.UpdateUVE(1200U);
    EXPECT_EQ(applied.at(9U), AtUVE(3.0F));
    EXPECT_EQ(a.StatsUVE().entriesSkippedIrrelevant, 1U);

    // The near one walks out: its last pose goes stale on the peer, provably unsent.
    positions.at(7U) = AtUVE(50.0F);
    ASSERT_TRUE(interest.UpdateEntityUVE(7U, positions.at(7U)));
    a.UpdateUVE(1300U);
    b.UpdateUVE(1300U);
    EXPECT_EQ(applied.at(7U), AtUVE(2.0F));
    EXPECT_EQ(a.StatsUVE().entriesSkippedIrrelevant, 2U);
}


TEST(NetworkInterestUVETest, TierDivisors_ThrottleToCadence) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(10U, AtUVE(1.0F), false, InterestTierUVE::Critical));
    ASSERT_TRUE(interest.AddEntityUVE(11U, AtUVE(1.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(12U, AtUVE(1.0F), false, InterestTierUVE::Normal));
    ASSERT_TRUE(interest.AddEntityUVE(13U, AtUVE(1.0F), false, InterestTierUVE::Low));
    std::uint64_t counts[4] = {};
    for (int i = 0; i < 8; ++i) {
        counts[0] += interest.IsRelevantUVE(1U, 10U) ? 1U : 0U;
        counts[1] += interest.IsRelevantUVE(1U, 11U) ? 1U : 0U;
        counts[2] += interest.IsRelevantUVE(1U, 12U) ? 1U : 0U;
        counts[3] += interest.IsRelevantUVE(1U, 13U) ? 1U : 0U;
    }
    EXPECT_EQ(counts[0], 8U);
    EXPECT_EQ(counts[1], 4U);
    EXPECT_EQ(counts[2], 2U);
    EXPECT_EQ(counts[3], 1U);
    EXPECT_EQ(interest.StatsUVE().throttled, 17U);
    EXPECT_EQ(interest.StatsUVE().culled, 0U);
}

TEST(NetworkInterestUVETest, Stagger_IdsShareTheTierSmoothly) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(10U, AtUVE(1.0F), false, InterestTierUVE::High));
    ASSERT_TRUE(interest.AddEntityUVE(11U, AtUVE(1.0F), false, InterestTierUVE::High));
    // Divisor 2: the even id ships on even ticks, the odd id on odd ticks - never together.
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(interest.IsRelevantUVE(1U, 10U), i % 2 == 0) << "tick " << i;
        EXPECT_EQ(interest.IsRelevantUVE(1U, 11U), i % 2 != 0) << "tick " << i;
    }
}

TEST(NetworkInterestUVETest, DefaultTier_CriticalSendsImmediately) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(1.0F)));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
    ASSERT_TRUE(interest.EntityTierUVE(7U).has_value());
    EXPECT_EQ(*interest.EntityTierUVE(7U), InterestTierUVE::Critical);
    ASSERT_TRUE(interest.EntityRateUVE(7U).has_value());
    EXPECT_EQ(*interest.EntityRateUVE(7U), 0U);
}

TEST(NetworkInterestUVETest, RateOverride_BeatsTier) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(8U, AtUVE(1.0F), false, InterestTierUVE::Normal));
    ASSERT_TRUE(interest.SetEntityRateUVE(8U, 3U));
    std::uint64_t sends = 0U;
    for (int i = 0; i < 9; ++i) {
        sends += interest.IsRelevantUVE(1U, 8U) ? 1U : 0U;
    }
    EXPECT_EQ(sends, 3U);
    // Zero clears back to the tier - and the phase carries over, so the next tier tick
    // is the 4th query, not the 1st.
    ASSERT_TRUE(interest.SetEntityRateUVE(8U, 0U));
    std::vector<bool> pattern;
    for (int i = 0; i < 4; ++i) {
        pattern.push_back(interest.IsRelevantUVE(1U, 8U));
    }
    EXPECT_EQ(pattern, (std::vector<bool>{false, false, false, true}));
}

TEST(NetworkInterestUVETest, AlwaysRelevant_ThrottledButNeverCulled) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(
        interest.AddEntityUVE(8U, AtUVE(1000.0F, 0.0F, 0.0F), true, InterestTierUVE::Low));
    std::uint64_t sends = 0U;
    for (int i = 0; i < 8; ++i) {
        sends += interest.IsRelevantUVE(1U, 8U) ? 1U : 0U;
    }
    EXPECT_EQ(sends, 1U);
    EXPECT_EQ(interest.StatsUVE().throttled, 7U);
    EXPECT_EQ(interest.StatsUVE().culled, 0U);
    EXPECT_EQ(interest.StatsUVE().relevant, 1U);
}

TEST(NetworkInterestUVETest, Bulk_RespectsThrottle) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(10U, AtUVE(1.0F), false, InterestTierUVE::Critical));
    ASSERT_TRUE(interest.AddEntityUVE(11U, AtUVE(1.0F), false, InterestTierUVE::Low));
    std::uint64_t critical = 0U;
    std::uint64_t low = 0U;
    for (int i = 0; i < 8; ++i) {
        const std::vector<NetworkEntityIdUVE> relevant = interest.RelevantEntitiesUVE(1U);
        for (const NetworkEntityIdUVE netId : relevant) {
            critical += netId == 10U ? 1U : 0U;
            low += netId == 11U ? 1U : 0U;
        }
    }
    EXPECT_EQ(critical, 8U);
    EXPECT_EQ(low, 1U);
    // Fail-open bulk stays unthrottled: an unknown viewer gets everything.
    EXPECT_EQ(interest.RelevantEntitiesUVE(99U),
              (std::vector<NetworkEntityIdUVE>{10U, 11U}));
}

TEST(NetworkInterestUVETest, UnknownIds_NeverThrottled) {
    NetworkInterestUVE interest;
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(interest.IsRelevantUVE(99U, 7U));
    }
    EXPECT_EQ(interest.StatsUVE().unknownIds, 8U);
    EXPECT_EQ(interest.StatsUVE().throttled, 0U);
}

TEST(NetworkInterestUVETest, CulledTicks_ConsumePhase) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(8U, AtUVE(100.0F), false, InterestTierUVE::Low));
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(interest.IsRelevantUVE(1U, 8U));
    }
    ASSERT_TRUE(interest.UpdateEntityUVE(8U, AtUVE(1.0F)));
    std::vector<bool> pattern;
    for (int i = 0; i < 8; ++i) {
        pattern.push_back(interest.IsRelevantUVE(1U, 8U));
    }
    // Ticks 0-2 were culled but counted: the phase resumes at tick 3, sending on the 6th.
    EXPECT_EQ(pattern,
              (std::vector<bool>{false, false, false, false, false, true, false, false}));
    EXPECT_EQ(interest.StatsUVE().culled, 3U);
    EXPECT_EQ(interest.StatsUVE().throttled, 7U);
    EXPECT_EQ(interest.StatsUVE().relevant, 1U);
}

TEST(NetworkInterestUVETest, Removal_ResetsPhase) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 100.0F));
    ASSERT_TRUE(interest.AddEntityUVE(8U, AtUVE(1.0F), false, InterestTierUVE::Low));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 8U));
    ASSERT_TRUE(interest.RemoveEntityUVE(8U));
    ASSERT_TRUE(interest.AddEntityUVE(8U, AtUVE(1.0F), false, InterestTierUVE::Low));
    // The clock was swept with the pair: tick 0 sends again instead of skipping to tick 8.
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 8U));
}

TEST(NetworkInterestUVETest, TierAndRate_UnknownIdsRefused) {
    NetworkInterestUVE interest;
    EXPECT_FALSE(interest.SetEntityTierUVE(99U, InterestTierUVE::High));
    EXPECT_FALSE(interest.SetEntityRateUVE(99U, 3U));
    EXPECT_FALSE(interest.EntityTierUVE(99U).has_value());
    EXPECT_FALSE(interest.EntityRateUVE(99U).has_value());
}

TEST(NetworkInterestUVETest, Viewers_ListsSorted) {
    NetworkInterestUVE interest;
    EXPECT_TRUE(interest.ViewersUVE().empty());
    ASSERT_TRUE(interest.AddViewerUVE(3U, 10.0F));
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddViewerUVE(2U, 10.0F));
    EXPECT_EQ(interest.ViewersUVE(), (std::vector<InterestViewerIdUVE>{1U, 2U, 3U}));
    ASSERT_TRUE(interest.RemoveViewerUVE(2U));
    EXPECT_EQ(interest.ViewersUVE(), (std::vector<InterestViewerIdUVE>{1U, 3U}));
}

TEST(NetworkInterestUVETest, Positions_ReadBackAndRefuseUnknowns) {
    NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F, AtUVE(4.0F, 5.0F, 6.0F)));
    ASSERT_TRUE(interest.AddEntityUVE(7U, AtUVE(1.0F, 2.0F, 3.0F)));
    ASSERT_TRUE(interest.ViewerPositionUVE(1U).has_value());
    EXPECT_EQ(*interest.ViewerPositionUVE(1U), AtUVE(4.0F, 5.0F, 6.0F));
    ASSERT_TRUE(interest.EntityPositionUVE(7U).has_value());
    EXPECT_EQ(*interest.EntityPositionUVE(7U), AtUVE(1.0F, 2.0F, 3.0F));
    EXPECT_FALSE(interest.ViewerPositionUVE(99U).has_value());
    EXPECT_FALSE(interest.EntityPositionUVE(99U).has_value());
}
} // namespace
} // namespace UVE::Network::Tests
