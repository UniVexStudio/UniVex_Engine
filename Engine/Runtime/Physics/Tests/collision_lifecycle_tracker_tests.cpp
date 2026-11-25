// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collision_lifecycle_tracker.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] CollisionPair Pair(const std::uint32_t firstIndex, const std::uint32_t firstGeneration,
                                       const std::uint32_t secondIndex, const std::uint32_t secondGeneration,
                                       const float depth = 0.1F) {
    return CollisionPair{{firstIndex, firstGeneration}, {secondIndex, secondGeneration},
                            Math::Vector3{1.0F, 0.0F, 0.0F}, depth};
}

TEST(CollisionLifecycleTrackerTest, Update_ReportsEnteredStableAndExitedTransitions) {
    CollisionLifecycleTracker tracker;
    const CollisionPair pair = Pair(1U, 1U, 2U, 1U);

    const CollisionLifecycleReport entered = tracker.Update({pair});
    ASSERT_EQ(entered.transitions.size(), 1U);
    EXPECT_EQ(entered.transitions.front().kind, CollisionTransitionKind::Entered);
    EXPECT_EQ(entered.transitions.front().pair, pair);
    EXPECT_EQ(entered.currentActiveCount, 1U);

    const CollisionLifecycleReport stable = tracker.Update({pair});
    EXPECT_TRUE(stable.transitions.empty());
    EXPECT_EQ(stable.previousActiveCount, 1U);
    EXPECT_EQ(stable.currentActiveCount, 1U);

    const CollisionLifecycleReport exited = tracker.Update({});
    ASSERT_EQ(exited.transitions.size(), 1U);
    EXPECT_EQ(exited.transitions.front().kind, CollisionTransitionKind::Exited);
    EXPECT_EQ(exited.transitions.front().pair, pair);
    EXPECT_EQ(exited.currentActiveCount, 0U);
}

TEST(CollisionLifecycleTrackerTest, Update_EntityGenerationChangeIsExitThenEnter) {
    CollisionLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update({Pair(3U, 1U, 4U, 1U)}).transitions.size(), 1U);

    const CollisionLifecycleReport report = tracker.Update({Pair(3U, 2U, 4U, 1U)});

    ASSERT_EQ(report.transitions.size(), 2U);
    EXPECT_EQ(report.transitions[0].kind, CollisionTransitionKind::Exited);
    EXPECT_EQ(report.transitions[0].pair.first.generation, 1U);
    EXPECT_EQ(report.transitions[1].kind, CollisionTransitionKind::Entered);
    EXPECT_EQ(report.transitions[1].pair.first.generation, 2U);
}

TEST(CollisionLifecycleTrackerTest, Update_SwappedPairOrderIsNotSpuriouslyReportedAsExitThenEnter) {
    CollisionLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update({Pair(10U, 1U, 20U, 1U)}).transitions.size(), 1U);

    // Same physical contact, reported with first/second swapped - CollisionPair has no fixed
    // role the way AreaOverlapPair's area/other does, so DetectCollisions() is free to
    // report either order from one frame to the next.
    CollisionPair swapped{};
    swapped.first = Scene::Entity{20U, 1U};
    swapped.second = Scene::Entity{10U, 1U};
    swapped.separationAxis = Math::Vector3{-1.0F, 0.0F, 0.0F};
    const CollisionLifecycleReport report = tracker.Update({swapped});

    EXPECT_TRUE(report.transitions.empty());
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST(CollisionLifecycleTrackerTest, Update_OverCapInputRetainsBaselineAndInfersNoExit) {
    CollisionLifecycleTracker tracker;
    const CollisionPair pair = Pair(5U, 1U, 6U, 1U);
    ASSERT_EQ(tracker.Update({pair}).currentActiveCount, 1U);

    const CollisionLifecycleReport report = tracker.Update({pair, Pair(7U, 1U, 8U, 1U)}, 1U);

    EXPECT_TRUE(report.inputSnapshotTruncated);
    EXPECT_TRUE(report.IsTruncated());
    EXPECT_TRUE(report.transitions.empty());
    EXPECT_EQ(report.previousActiveCount, 1U);
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST(CollisionLifecycleTrackerTest, Update_TransitionCapTruncatesReportButCommitsCompleteCurrentBaseline) {
    CollisionLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update({Pair(7U, 1U, 8U, 1U), Pair(7U, 1U, 9U, 1U)}).currentActiveCount, 2U);

    const CollisionLifecycleReport report = tracker.Update({}, kMaximumCollisionLifecycleResults, 1U);

    EXPECT_TRUE(report.transitionsTruncated);
    EXPECT_TRUE(report.IsTruncated());
    EXPECT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.currentActiveCount, 0U);
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
}

TEST(CollisionLifecycleTrackerTest, Reset_DiscardsBaselineWithoutFabricatingTransitions) {
    CollisionLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update({Pair(10U, 1U, 11U, 1U)}).currentActiveCount, 1U);

    tracker.Reset();
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
    EXPECT_TRUE(tracker.Update({}).transitions.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
