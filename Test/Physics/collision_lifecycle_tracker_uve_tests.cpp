// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/collision_lifecycle_tracker_uve.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] CollisionPairUVE PairUVE(const std::uint32_t firstIndex, const std::uint32_t firstGeneration,
                                       const std::uint32_t secondIndex, const std::uint32_t secondGeneration,
                                       const float depth = 0.1F) {
    return CollisionPairUVE{{firstIndex, firstGeneration}, {secondIndex, secondGeneration},
                            Math::Vector3UVE{1.0F, 0.0F, 0.0F}, depth};
}

TEST(CollisionLifecycleTrackerUVETest, UpdateUVE_ReportsEnteredStableAndExitedTransitions) {
    CollisionLifecycleTrackerUVE tracker;
    const CollisionPairUVE pair = PairUVE(1U, 1U, 2U, 1U);

    const CollisionLifecycleReportUVE entered = tracker.UpdateUVE({pair});
    ASSERT_EQ(entered.transitions.size(), 1U);
    EXPECT_EQ(entered.transitions.front().kind, CollisionTransitionKindUVE::Entered);
    EXPECT_EQ(entered.transitions.front().pair, pair);
    EXPECT_EQ(entered.currentActiveCount, 1U);

    const CollisionLifecycleReportUVE stable = tracker.UpdateUVE({pair});
    EXPECT_TRUE(stable.transitions.empty());
    EXPECT_EQ(stable.previousActiveCount, 1U);
    EXPECT_EQ(stable.currentActiveCount, 1U);

    const CollisionLifecycleReportUVE exited = tracker.UpdateUVE({});
    ASSERT_EQ(exited.transitions.size(), 1U);
    EXPECT_EQ(exited.transitions.front().kind, CollisionTransitionKindUVE::Exited);
    EXPECT_EQ(exited.transitions.front().pair, pair);
    EXPECT_EQ(exited.currentActiveCount, 0U);
}

TEST(CollisionLifecycleTrackerUVETest, UpdateUVE_EntityGenerationChangeIsExitThenEnter) {
    CollisionLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE({PairUVE(3U, 1U, 4U, 1U)}).transitions.size(), 1U);

    const CollisionLifecycleReportUVE report = tracker.UpdateUVE({PairUVE(3U, 2U, 4U, 1U)});

    ASSERT_EQ(report.transitions.size(), 2U);
    EXPECT_EQ(report.transitions[0].kind, CollisionTransitionKindUVE::Exited);
    EXPECT_EQ(report.transitions[0].pair.first.generation, 1U);
    EXPECT_EQ(report.transitions[1].kind, CollisionTransitionKindUVE::Entered);
    EXPECT_EQ(report.transitions[1].pair.first.generation, 2U);
}

TEST(CollisionLifecycleTrackerUVETest, UpdateUVE_SwappedPairOrderIsNotSpuriouslyReportedAsExitThenEnter) {
    CollisionLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE({PairUVE(10U, 1U, 20U, 1U)}).transitions.size(), 1U);

    // Same physical contact, reported with first/second swapped - CollisionPairUVE has no fixed
    // role the way AreaOverlapPairUVE's area/other does, so DetectCollisionsUVE() is free to
    // report either order from one frame to the next.
    CollisionPairUVE swapped{};
    swapped.first = Scene::EntityUVE{20U, 1U};
    swapped.second = Scene::EntityUVE{10U, 1U};
    swapped.separationAxis = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
    const CollisionLifecycleReportUVE report = tracker.UpdateUVE({swapped});

    EXPECT_TRUE(report.transitions.empty());
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST(CollisionLifecycleTrackerUVETest, UpdateUVE_OverCapInputRetainsBaselineAndInfersNoExit) {
    CollisionLifecycleTrackerUVE tracker;
    const CollisionPairUVE pair = PairUVE(5U, 1U, 6U, 1U);
    ASSERT_EQ(tracker.UpdateUVE({pair}).currentActiveCount, 1U);

    const CollisionLifecycleReportUVE report = tracker.UpdateUVE({pair, PairUVE(7U, 1U, 8U, 1U)}, 1U);

    EXPECT_TRUE(report.inputSnapshotTruncated);
    EXPECT_TRUE(report.IsTruncatedUVE());
    EXPECT_TRUE(report.transitions.empty());
    EXPECT_EQ(report.previousActiveCount, 1U);
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST(CollisionLifecycleTrackerUVETest, UpdateUVE_TransitionCapTruncatesReportButCommitsCompleteCurrentBaseline) {
    CollisionLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE({PairUVE(7U, 1U, 8U, 1U), PairUVE(7U, 1U, 9U, 1U)}).currentActiveCount, 2U);

    const CollisionLifecycleReportUVE report = tracker.UpdateUVE({}, kMaximumCollisionLifecycleResultsUVE, 1U);

    EXPECT_TRUE(report.transitionsTruncated);
    EXPECT_TRUE(report.IsTruncatedUVE());
    EXPECT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.currentActiveCount, 0U);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);
}

TEST(CollisionLifecycleTrackerUVETest, ResetUVE_DiscardsBaselineWithoutFabricatingTransitions) {
    CollisionLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE({PairUVE(10U, 1U, 11U, 1U)}).currentActiveCount, 1U);

    tracker.ResetUVE();
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);
    EXPECT_TRUE(tracker.UpdateUVE({}).transitions.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
