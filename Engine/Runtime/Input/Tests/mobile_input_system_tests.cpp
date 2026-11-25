// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mobile_input_system.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Input::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(MobileInputSystemTest, EvaluateMobileInputPolicy_ClassifiesAvailability) {
    MobileInputAvailability availability = MobileInputAvailability::TouchAndGyroscope;
    ASSERT_TRUE(EvaluateMobileInputPolicy(
        MobileLifecycleState::Active, true, false, false, availability));
    EXPECT_EQ(availability, MobileInputAvailability::TouchAndGyroscope);
    ASSERT_TRUE(EvaluateMobileInputPolicy(
        MobileLifecycleState::Active, false, true, false, availability));
    EXPECT_EQ(availability, MobileInputAvailability::TouchOnly);
    ASSERT_TRUE(EvaluateMobileInputPolicy(
        MobileLifecycleState::Suspended, true, true, true, availability));
    EXPECT_EQ(availability, MobileInputAvailability::Disabled);
}

TEST(MobileInputSystemTest, EvaluateMobileInputPolicy_RejectsInvalidLifecycleAtomically) {
    MobileInputAvailability availability = MobileInputAvailability::TouchOnly;
    const auto invalidState = static_cast<MobileLifecycleState>(
        static_cast<std::uint8_t>(MobileLifecycleState::Count));
    EXPECT_FALSE(EvaluateMobileInputPolicy(invalidState, true, true, true, availability));
    EXPECT_EQ(availability, MobileInputAvailability::TouchOnly);
}

TEST(MobileInputSystemTest, EvaluateMobileLifecycleTransition_ClassifiesPlatformEdges) {
    MobileLifecycleTransition transition = MobileLifecycleTransition::Terminated;
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Inactive, MobileLifecycleState::Active, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Activated);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Active, MobileLifecycleState::Suspended, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Suspended);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Suspended, MobileLifecycleState::Active, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Resumed);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Active, MobileLifecycleState::Inactive, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Deactivated);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Active, MobileLifecycleState::Terminated, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Terminated);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Terminated, MobileLifecycleState::Active, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Reinitialized);
    ASSERT_TRUE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Active, MobileLifecycleState::Active, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::None);
}

TEST(MobileInputSystemTest, EvaluateMobileLifecycleTransition_RejectsInvalidStatesAtomically) {
    MobileLifecycleTransition transition = MobileLifecycleTransition::Resumed;
    const auto invalidState = static_cast<MobileLifecycleState>(
        static_cast<std::uint8_t>(MobileLifecycleState::Count));
    EXPECT_FALSE(EvaluateMobileLifecycleTransition(
        invalidState, MobileLifecycleState::Active, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Resumed);
    EXPECT_FALSE(EvaluateMobileLifecycleTransition(
        MobileLifecycleState::Active, invalidState, transition));
    EXPECT_EQ(transition, MobileLifecycleTransition::Resumed);
}

TEST(MobileInputSystemTest, EvaluateTouchLifecycleTransition_ClassifiesSlotChanges) {
    TouchLifecycleTransition transition = TouchLifecycleTransition::Replaced;
    const TouchPointState inactive{};
    const TouchPointState began{true, 7U, Math::Vector2{1.0F, 2.0F}, {}, 0.5F};
    const TouchPointState moved{true, 7U, Math::Vector2{2.0F, 2.0F}, Math::Vector2{1.0F, 0.0F}, 0.75F};
    const TouchPointState replaced{true, 8U, Math::Vector2{2.0F, 2.0F}, {}, 0.75F};
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(inactive, inactive, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::None);
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(inactive, began, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Began);
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(began, moved, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Moved);
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(moved, inactive, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Ended);
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(moved, replaced, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Replaced);
    ASSERT_TRUE(EvaluateTouchLifecycleTransition(began, began, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::None);
}

TEST(MobileInputSystemTest, EvaluateTouchLifecycleTransition_RejectsInvalidInputsAtomically) {
    TouchLifecycleTransition transition = TouchLifecycleTransition::Moved;
    const TouchPointState invalidIdentifier{true, 0U, Math::Vector2{}, {}, 0.5F};
    const TouchPointState invalidPressure{true, 1U, Math::Vector2{}, {}, 1.1F};
    EXPECT_FALSE(EvaluateTouchLifecycleTransition(invalidIdentifier, TouchPointState{}, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Moved);
    EXPECT_FALSE(EvaluateTouchLifecycleTransition(invalidPressure, TouchPointState{}, transition));
    EXPECT_EQ(transition, TouchLifecycleTransition::Moved);
}

TEST(MobileInputSystemTest, SnapshotCopiesTouchDeltaPressureAndGyroscopeDeterministically) {
    MobileInputSystem mobileInput;

    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{100.0F, 200.0F}, 1.5F);
    mobileInput.SetGyroscopeRotationRate(Math::Vector3{1.0F, 2.0F, 3.0F});
    mobileInput.Update();

    const MobileInputSnapshot first = mobileInput.GetSnapshot();
    EXPECT_EQ(first.frameNumber, 1U);
    EXPECT_TRUE(first.touches[0U].active);
    EXPECT_EQ(first.touches[0U].identifier, 42U);
    EXPECT_NEAR(first.touches[0U].position.x, 100.0F, kEpsilon);
    EXPECT_NEAR(first.touches[0U].position.y, 200.0F, kEpsilon);
    EXPECT_NEAR(first.touches[0U].delta.x, 0.0F, kEpsilon);
    EXPECT_NEAR(first.touches[0U].delta.y, 0.0F, kEpsilon);
    EXPECT_NEAR(first.touches[0U].pressure, 1.0F, kEpsilon);
    EXPECT_NEAR(first.gyroscopeRotationRate.z, 3.0F, kEpsilon);
    EXPECT_EQ(mobileInput.GetPreviousSnapshot().frameNumber, 0U);

    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{110.0F, 190.0F}, -0.5F);
    mobileInput.SetGyroscopeRotationRate(Math::Vector3{-1.0F, 0.0F, 2.0F});
    mobileInput.Update();

    const MobileInputSnapshot second = mobileInput.GetSnapshot();
    EXPECT_EQ(second.frameNumber, 2U);
    EXPECT_NEAR(second.touches[0U].delta.x, 10.0F, kEpsilon);
    EXPECT_NEAR(second.touches[0U].delta.y, -10.0F, kEpsilon);
    EXPECT_NEAR(second.touches[0U].pressure, 0.0F, kEpsilon);
    EXPECT_NEAR(second.gyroscopeRotationRate.x, -1.0F, kEpsilon);
    EXPECT_EQ(mobileInput.GetPreviousSnapshot().touches[0U].identifier, 42U);
}

TEST(MobileInputSystemTest, FiniteEndpointDeltaOverflowFailsClosedToZero) {
    MobileInputSystem mobileInput;
    const float maximumFloat = std::numeric_limits<float>::max();

    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{-maximumFloat, 0.0F}, 1.0F);
    mobileInput.Update();
    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{maximumFloat, 0.0F}, 1.0F);
    mobileInput.Update();

    const TouchPointState touch = mobileInput.GetSnapshot().touches[0U];
    EXPECT_TRUE(std::isfinite(touch.delta.x));
    EXPECT_TRUE(std::isfinite(touch.delta.y));
    EXPECT_EQ(touch.delta, (Math::Vector2{}));
}

TEST(MobileInputSystemTest, ActiveZeroIdentifierIsRejectedWithoutPublishingOrReplacingTouch) {
    MobileInputSystem mobileInput;
    mobileInput.SetTouchState(0U, true, 0U, Math::Vector2{10.0F, 20.0F}, 1.0F);
    mobileInput.Update();
    EXPECT_FALSE(mobileInput.GetSnapshot().touches[0U].active);

    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{10.0F, 20.0F}, 1.0F);
    mobileInput.Update();
    mobileInput.SetTouchState(0U, true, 0U, Math::Vector2{30.0F, 40.0F}, 1.0F);
    mobileInput.Update();
    const TouchPointState touch = mobileInput.GetSnapshot().touches[0U];
    EXPECT_TRUE(touch.active);
    EXPECT_EQ(touch.identifier, 42U);
    EXPECT_EQ(touch.position, (Math::Vector2{10.0F, 20.0F}));
}

TEST(MobileInputSystemTest, DuplicateActiveIdentifierIsRejectedWithoutPublishingOrReplacingTouch) {
    MobileInputSystem mobileInput;
    mobileInput.SetTouchState(0U, true, 42U, Math::Vector2{10.0F, 20.0F}, 0.5F);
    mobileInput.Update();

    mobileInput.SetTouchState(1U, true, 42U, Math::Vector2{30.0F, 40.0F}, 0.75F);
    mobileInput.Update();
    const MobileInputSnapshot duplicateRejected = mobileInput.GetSnapshot();
    EXPECT_TRUE(duplicateRejected.touches[0U].active);
    EXPECT_EQ(duplicateRejected.touches[0U].identifier, 42U);
    EXPECT_EQ(duplicateRejected.touches[0U].position, (Math::Vector2{10.0F, 20.0F}));
    EXPECT_FALSE(duplicateRejected.touches[1U].active);

    mobileInput.SetTouchState(1U, true, 43U, Math::Vector2{30.0F, 40.0F}, 0.75F);
    mobileInput.Update();
    const MobileInputSnapshot uniqueAccepted = mobileInput.GetSnapshot();
    EXPECT_TRUE(uniqueAccepted.touches[1U].active);
    EXPECT_EQ(uniqueAccepted.touches[1U].identifier, 43U);
}

TEST(MobileInputSystemTest, InvalidAndNonFiniteInputFailsClosedAndReleaseClearsTouch) {
    MobileInputSystem mobileInput;
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    mobileInput.SetTouchState(kMaximumTouchCount, true, 99U, Math::Vector2{1.0F, 2.0F}, 1.0F);
    mobileInput.SetTouchState(1U, true, 7U, Math::Vector2{infinity, nan}, nan);
    mobileInput.SetGyroscopeRotationRate(Math::Vector3{nan, infinity, -infinity});
    mobileInput.Update();

    MobileInputSnapshot active = mobileInput.GetSnapshot();
    EXPECT_TRUE(active.touches[1U].active);
    EXPECT_EQ(active.touches[1U].identifier, 7U);
    EXPECT_NEAR(active.touches[1U].position.x, 0.0F, kEpsilon);
    EXPECT_NEAR(active.touches[1U].position.y, 0.0F, kEpsilon);
    EXPECT_NEAR(active.touches[1U].pressure, 0.0F, kEpsilon);
    EXPECT_NEAR(active.gyroscopeRotationRate.x, 0.0F, kEpsilon);
    EXPECT_NEAR(active.gyroscopeRotationRate.y, 0.0F, kEpsilon);
    EXPECT_NEAR(active.gyroscopeRotationRate.z, 0.0F, kEpsilon);
    EXPECT_FALSE(active.touches[0U].active);

    mobileInput.SetTouchState(1U, false, 0U, {}, 0.0F);
    mobileInput.Update();
    const MobileInputSnapshot released = mobileInput.GetSnapshot();
    EXPECT_FALSE(released.touches[1U].active);
    EXPECT_EQ(released.touches[1U].identifier, 0U);
    EXPECT_NEAR(released.touches[1U].delta.x, 0.0F, kEpsilon);
    EXPECT_NEAR(released.touches[1U].delta.y, 0.0F, kEpsilon);
    EXPECT_TRUE(mobileInput.GetPreviousSnapshot().touches[1U].active);
}

} // namespace
} // namespace UVE::Input::Tests
