// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mobile_gesture_recognizer.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Input::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(MobileGestureRecognizerTest, ClassifiesTouchChordLifecycleAcrossCopiedFrames) {
    constexpr std::size_t kRequiredTouches = 2U;
    MobileInputSnapshot previous{};
    MobileInputSnapshot current{};
    current.touches[0U] = TouchPointState{true, 11U, Math::Vector2{0.0F, 2.0F}, {}, 1.0F};
    current.touches[3U] = TouchPointState{true, 22U, Math::Vector2{4.0F, 6.0F}, {}, 1.0F};
    TouchChordLifecycleTransition transition = TouchChordLifecycleTransition::Replaced;
    ASSERT_TRUE(EvaluateTouchChordLifecycleTransition(
        previous, current, kRequiredTouches, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Began);

    previous = current;
    current.touches[0U].position.x = 1.0F;
    ASSERT_TRUE(EvaluateTouchChordLifecycleTransition(
        previous, current, kRequiredTouches, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Moved);

    previous = current;
    ASSERT_TRUE(EvaluateTouchChordLifecycleTransition(
        previous, current, kRequiredTouches, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::None);

    current.touches[3U].identifier = 33U;
    ASSERT_TRUE(EvaluateTouchChordLifecycleTransition(
        previous, current, kRequiredTouches, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Replaced);

    previous = current;
    current.touches[3U] = {};
    ASSERT_TRUE(EvaluateTouchChordLifecycleTransition(
        previous, current, kRequiredTouches, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Ended);
}

TEST(MobileGestureRecognizerTest, RejectsInvalidTouchChordLifecycleAtomically) {
    MobileInputSnapshot previous{};
    MobileInputSnapshot current{};
    current.touches[0U] = TouchPointState{true, 11U, Math::Vector2{}, {}, 1.0F};
    current.touches[1U] = TouchPointState{true, 11U, Math::Vector2{1.0F, 1.0F}, {}, 1.0F};
    TouchChordLifecycleTransition transition = TouchChordLifecycleTransition::Moved;
    EXPECT_FALSE(EvaluateTouchChordLifecycleTransition(previous, current, 2U, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Moved);

    current.touches[1U].identifier = 22U;
    current.touches[1U].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(EvaluateTouchChordLifecycleTransition(previous, current, 2U, transition));
    EXPECT_EQ(transition, TouchChordLifecycleTransition::Moved);
    EXPECT_FALSE(EvaluateTouchChordLifecycleTransition(previous, current, 1U, transition));
}

TEST(MobileGestureRecognizerTest, EvaluatesExactTouchChordCentroid) {
    MobileInputSnapshot snapshot{};
    snapshot.touches[0U] = TouchPointState{true, 11U, Math::Vector2{0.0F, 2.0F}, {}, 1.0F};
    snapshot.touches[3U] = TouchPointState{true, 22U, Math::Vector2{4.0F, 6.0F}, {}, 1.0F};
    Math::Vector2 centroid{9.0F, 9.0F};
    ASSERT_TRUE(EvaluateTouchChord(snapshot, 2U, centroid));
    EXPECT_NEAR(centroid.x, 2.0F, kEpsilon);
    EXPECT_NEAR(centroid.y, 4.0F, kEpsilon);
}

TEST(MobileGestureRecognizerTest, RejectsOverflowedChordCentroidAtomically) {
    MobileInputSnapshot snapshot{};
    snapshot.touches[0U] = TouchPointState{
        true, 11U, Math::Vector2{std::numeric_limits<float>::max(), 0.0F}, {}, 1.0F};
    snapshot.touches[1U] = TouchPointState{
        true, 22U, Math::Vector2{std::numeric_limits<float>::max(), 0.0F}, {}, 1.0F};
    Math::Vector2 centroid{9.0F, 9.0F};
    EXPECT_FALSE(EvaluateTouchChord(snapshot, 2U, centroid));
    EXPECT_EQ(centroid, (Math::Vector2{9.0F, 9.0F}));
}

TEST(MobileGestureRecognizerTest, RejectsChordCountDuplicatesAndInvalidPositionAtomically) {
    MobileInputSnapshot snapshot{};
    snapshot.touches[0U] = TouchPointState{true, 11U, Math::Vector2{0.0F, 2.0F}, {}, 1.0F};
    snapshot.touches[1U] = TouchPointState{true, 11U, Math::Vector2{4.0F, 6.0F}, {}, 1.0F};
    Math::Vector2 centroid{9.0F, 9.0F};
    EXPECT_FALSE(EvaluateTouchChord(snapshot, 2U, centroid));
    EXPECT_EQ(centroid, (Math::Vector2{9.0F, 9.0F}));
    snapshot.touches[1U].identifier = 22U;
    snapshot.touches[1U].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(EvaluateTouchChord(snapshot, 2U, centroid));
    EXPECT_EQ(centroid, (Math::Vector2{9.0F, 9.0F}));
    EXPECT_FALSE(EvaluateTouchChord(snapshot, 1U, centroid));
}

TEST(MobileGestureRecognizerTest, ClassifiesTapOnReleaseAndSuppressesDuplicateFrame) {
    MobileGestureRecognizer recognizer;

    MobileInputSnapshot pressed{};
    pressed.frameNumber = 1U;
    pressed.touches[0U] = TouchPointState{true, 7U, Math::Vector2{25.0F, 30.0F}, {}, 1.0F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(pressed, 0.016F).count, 0U);

    MobileInputSnapshot released{};
    released.frameNumber = 2U;
    const MobileGestureReport report = recognizer.ConsumeSnapshot(released, 0.016F);
    ASSERT_EQ(report.count, 1U);
    EXPECT_FALSE(report.truncated);
    EXPECT_EQ(report.events[0U].type, MobileGestureType::Tap);
    EXPECT_EQ(report.events[0U].touchIdentifier, 7U);
    EXPECT_NEAR(report.events[0U].startPosition.x, 25.0F, kEpsilon);
    EXPECT_NEAR(report.events[0U].endPosition.y, 30.0F, kEpsilon);

    EXPECT_EQ(recognizer.ConsumeSnapshot(released, 0.016F).count, 0U);
}

TEST(MobileGestureRecognizerTest, ClassifiesDirectionalSwipeFromStableTouchIdentity) {
    MobileGestureRecognizer recognizer;

    MobileInputSnapshot start{};
    start.frameNumber = 10U;
    start.touches[2U] = TouchPointState{true, 99U, Math::Vector2{0.0F, 0.0F}, {}, 0.5F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(start, 0.1F).count, 0U);

    MobileInputSnapshot moved{};
    moved.frameNumber = 11U;
    moved.touches[2U] = TouchPointState{true, 99U, Math::Vector2{60.0F, 10.0F}, {}, 0.5F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(moved, 0.2F).count, 0U);

    MobileInputSnapshot released{};
    released.frameNumber = 12U;
    const MobileGestureReport report = recognizer.ConsumeSnapshot(released, 0.2F);
    ASSERT_EQ(report.count, 1U);
    EXPECT_EQ(report.events[0U].type, MobileGestureType::Swipe);
    EXPECT_EQ(report.events[0U].direction, MobileSwipeDirection::PositiveX);
    EXPECT_EQ(report.events[0U].touchIdentifier, 99U);
    EXPECT_NEAR(report.events[0U].delta.x, 60.0F, kEpsilon);
    EXPECT_NEAR(report.events[0U].delta.y, 10.0F, kEpsilon);
    EXPECT_NEAR(report.events[0U].durationSeconds, 0.2F, kEpsilon);
}

TEST(MobileGestureRecognizerTest, RejectsOverflowedReleaseDeltaWithoutPublishingGesture) {
    MobileGestureRecognizer recognizer;
    MobileInputSnapshot pressed{};
    pressed.frameNumber = 30U;
    pressed.touches[0U] = TouchPointState{
        true, 5U, Math::Vector2{-std::numeric_limits<float>::max(), 0.0F}, {}, 0.0F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(pressed, 0.1F).count, 0U);

    MobileInputSnapshot moved = pressed;
    moved.frameNumber = 31U;
    moved.touches[0U].position = Math::Vector2{std::numeric_limits<float>::max(), 0.0F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(moved, 0.1F).count, 0U);

    MobileInputSnapshot released{};
    released.frameNumber = 32U;
    const MobileGestureReport report = recognizer.ConsumeSnapshot(released, 0.1F);
    EXPECT_EQ(report.count, 0U);
    EXPECT_FALSE(report.truncated);
}

TEST(MobileGestureRecognizerTest, RejectsInvalidDirectSnapshotAtomically) {
    MobileGestureRecognizer recognizer;
    MobileInputSnapshot pressed{};
    pressed.frameNumber = 1U;
    pressed.touches[0U] = TouchPointState{true, 7U, Math::Vector2{25.0F, 30.0F}, {}, 0.5F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(pressed, 0.1F).count, 0U);

    MobileInputSnapshot invalidIdentifier = pressed;
    invalidIdentifier.frameNumber = 2U;
    invalidIdentifier.touches[0U].identifier = 0U;
    EXPECT_EQ(recognizer.ConsumeSnapshot(invalidIdentifier, 0.1F).count, 0U);

    MobileInputSnapshot invalidPressure = pressed;
    invalidPressure.frameNumber = 3U;
    invalidPressure.touches[0U].pressure = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(recognizer.ConsumeSnapshot(invalidPressure, 0.1F).count, 0U);

    MobileInputSnapshot released{};
    released.frameNumber = 4U;
    const MobileGestureReport report = recognizer.ConsumeSnapshot(released, 0.1F);
    ASSERT_EQ(report.count, 1U);
    EXPECT_EQ(report.events[0U].type, MobileGestureType::Tap);
    EXPECT_EQ(report.events[0U].touchIdentifier, 7U);
}

TEST(MobileGestureRecognizerTest, RejectsInvalidFrameDeltaAndResetClearsTrackedTouch) {
    MobileGestureRecognizer recognizer;
    MobileInputSnapshot pressed{};
    pressed.frameNumber = 20U;
    pressed.touches[0U] = TouchPointState{true, 5U, Math::Vector2{1.0F, 1.0F}, {}, 0.0F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(pressed, 0.1F).count, 0U);

    MobileInputSnapshot next = pressed;
    next.frameNumber = 21U;
    next.touches[0U].position = Math::Vector2{100.0F, 0.0F};
    EXPECT_EQ(recognizer.ConsumeSnapshot(next, std::numeric_limits<float>::quiet_NaN()).count, 0U);

    recognizer.Reset();
    MobileInputSnapshot released{};
    released.frameNumber = 22U;
    EXPECT_EQ(recognizer.ConsumeSnapshot(released, 0.1F).count, 0U);
}

} // namespace
} // namespace UVE::Input::Tests
