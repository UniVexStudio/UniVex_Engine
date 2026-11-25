// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_overlap_lifecycle_tracker.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] AreaOverlapPair Pair(const std::uint32_t areaIndex, const std::uint32_t areaGeneration,
                                         const std::uint32_t otherIndex, const std::uint32_t otherGeneration,
                                         const float depth = 0.5F) {
    return {{areaIndex, areaGeneration}, {otherIndex, otherGeneration}, depth};
}

TEST(AreaOverlapLifecycleTrackerTest, Update_ReportsEnteredStableAndExitedTransitions) {
    AreaOverlapLifecycleTracker tracker;
    const AreaOverlapPair pair = Pair(1U, 1U, 2U, 1U);

    AreaOverlapQueryResult enteredSnapshot;
    enteredSnapshot.overlaps = {pair};
    const AreaOverlapLifecycleReport entered = tracker.Update(enteredSnapshot);
    ASSERT_EQ(entered.transitions.size(), 1U);
    EXPECT_EQ(entered.transitions.front().kind, AreaOverlapTransitionKind::Entered);
    EXPECT_EQ(entered.transitions.front().pair, pair);
    EXPECT_EQ(entered.currentActiveCount, 1U);

    const AreaOverlapLifecycleReport stable = tracker.Update(enteredSnapshot);
    EXPECT_TRUE(stable.transitions.empty());
    EXPECT_EQ(stable.previousActiveCount, 1U);
    EXPECT_EQ(stable.currentActiveCount, 1U);

    const AreaOverlapLifecycleReport exited = tracker.Update({});
    ASSERT_EQ(exited.transitions.size(), 1U);
    EXPECT_EQ(exited.transitions.front().kind, AreaOverlapTransitionKind::Exited);
    EXPECT_EQ(exited.transitions.front().pair, pair);
    EXPECT_EQ(exited.currentActiveCount, 0U);
}

TEST(AreaOverlapLifecycleTrackerTest, Update_EntityGenerationChangeIsExitThenEnter) {
    AreaOverlapLifecycleTracker tracker;
    AreaOverlapQueryResult firstSnapshot;
    firstSnapshot.overlaps = {Pair(3U, 1U, 4U, 1U)};
    ASSERT_EQ(tracker.Update(firstSnapshot).transitions.size(), 1U);

    AreaOverlapQueryResult replacementSnapshot;
    replacementSnapshot.overlaps = {Pair(3U, 2U, 4U, 1U)};
    const AreaOverlapLifecycleReport report = tracker.Update(replacementSnapshot);

    ASSERT_EQ(report.transitions.size(), 2U);
    EXPECT_EQ(report.transitions[0].kind, AreaOverlapTransitionKind::Exited);
    EXPECT_EQ(report.transitions[0].pair.area.generation, 1U);
    EXPECT_EQ(report.transitions[1].kind, AreaOverlapTransitionKind::Entered);
    EXPECT_EQ(report.transitions[1].pair.area.generation, 2U);
}

TEST(AreaOverlapLifecycleTrackerTest, Update_TruncatedInputRetainsBaselineAndInfersNoExit) {
    AreaOverlapLifecycleTracker tracker;
    const AreaOverlapPair pair = Pair(5U, 1U, 6U, 1U);
    AreaOverlapQueryResult initial;
    initial.overlaps = {pair};
    ASSERT_EQ(tracker.Update(initial).currentActiveCount, 1U);

    AreaOverlapQueryResult truncated;
    truncated.truncated = true;
    const AreaOverlapLifecycleReport report = tracker.Update(truncated);

    EXPECT_TRUE(report.inputSnapshotTruncated);
    EXPECT_TRUE(report.IsTruncated());
    EXPECT_TRUE(report.transitions.empty());
    EXPECT_EQ(report.previousActiveCount, 1U);
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST(AreaOverlapLifecycleTrackerTest, Update_TransitionCapTruncatesReportButCommitsCompleteCurrentBaseline) {
    AreaOverlapLifecycleTracker tracker;
    AreaOverlapQueryResult initial;
    initial.overlaps = {Pair(7U, 1U, 8U, 1U), Pair(7U, 1U, 9U, 1U)};
    ASSERT_EQ(tracker.Update(initial).currentActiveCount, 2U);

    const AreaOverlapLifecycleReport report = tracker.Update({}, 1U);

    EXPECT_TRUE(report.transitionsTruncated);
    EXPECT_TRUE(report.IsTruncated());
    EXPECT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.currentActiveCount, 0U);
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
}

TEST(AreaOverlapLifecycleTrackerTest, Reset_DiscardsBaselineWithoutFabricatingTransitions) {
    AreaOverlapLifecycleTracker tracker;
    AreaOverlapQueryResult initial;
    initial.overlaps = {Pair(10U, 1U, 11U, 1U)};
    ASSERT_EQ(tracker.Update(initial).currentActiveCount, 1U);

    tracker.Reset();
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
    EXPECT_TRUE(tracker.Update({}).transitions.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
