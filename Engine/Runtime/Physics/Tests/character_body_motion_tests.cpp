// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_body_motion.h"

#include <cmath>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

using Scene::CharacterControllerComponent;
using Scene::CharacterMotionMode;

constexpr float kGravity = -9.8F;
constexpr float kStep = 1.0F / 60.0F;

[[nodiscard]] CharacterControllerComponent OnFloor() {
    CharacterControllerComponent c{};
    c.grounded = true;
    return c;
}

[[nodiscard]] CharacterMoveOutcome Air() {
    return CharacterMoveOutcome{false, {0.0F, 1.0F, 0.0F}, false};
}

TEST(CharacterBodyMotionTest, AJumpReachesItsHeightWhateverTheGravity) {
    for (const float gravityScale : {0.5F, 1.0F, 3.0F}) {
        CharacterControllerComponent c = OnFloor();
        c.gravityScale = gravityScale;
        c.jumpHeight = 2.0F;
        ASSERT_TRUE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));
        // v^2 = 2 g h: the apex of v under g is exactly jumpHeight.
        const float g = -kGravity * gravityScale;
        EXPECT_NEAR((c.velocity.y * c.velocity.y) / (2.0F * g), 2.0F, 1.0e-4F);
        EXPECT_FALSE(c.grounded);
    }
}

TEST(CharacterBodyMotionTest, CoyoteTimeAllowsOneLateJumpAfterWalkingOffALedge) {
    CharacterControllerComponent c = OnFloor();
    c.coyoteTimeSeconds = 0.1F;
    // Walks off: two steps in the air, still inside the window.
    for (int step = 0; step < 2; ++step) {
        EXPECT_FALSE(StepCharacterIntent(c, {}, kGravity, kStep));
        FinishCharacterStep(c, Air(), false, kStep);
    }
    EXPECT_TRUE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));
    FinishCharacterStep(c, Air(), true, kStep);
    // Spent: pressing again in the same fall does nothing.
    EXPECT_FALSE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));

    // Past the window, a press is only remembered, not acted on.
    CharacterControllerComponent late = OnFloor();
    late.coyoteTimeSeconds = 0.05F;
    late.jumpBufferSeconds = 0.0F;
    for (int step = 0; step < 6; ++step) {
        static_cast<void>(StepCharacterIntent(late, {}, kGravity, kStep));
        FinishCharacterStep(late, Air(), false, kStep);
    }
    EXPECT_FALSE(StepCharacterIntent(late, {{}, 0.0F, true}, kGravity, kStep));
}

TEST(CharacterBodyMotionTest, AJumpPressedJustBeforeLandingHappensOnLanding) {
    CharacterControllerComponent c{};
    c.coyoteTimeSeconds = 0.0F;
    c.timeSinceOnFloor = 1.0F; // Well into a fall.
    c.jumpBufferSeconds = 0.1F;
    EXPECT_FALSE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));
    FinishCharacterStep(c, Air(), false, kStep);
    // Lands two steps later, with no new press.
    FinishCharacterStep(c, CharacterMoveOutcome{true, {0.0F, 1.0F, 0.0F}, false}, false, kStep);
    EXPECT_TRUE(StepCharacterIntent(c, {}, kGravity, kStep));

    // Without a buffer the early press is simply lost.
    CharacterControllerComponent unbuffered{};
    unbuffered.coyoteTimeSeconds = 0.0F;
    unbuffered.timeSinceOnFloor = 1.0F;
    unbuffered.jumpBufferSeconds = 0.0F;
    static_cast<void>(StepCharacterIntent(unbuffered, {{}, 0.0F, true}, kGravity, kStep));
    FinishCharacterStep(unbuffered, Air(), false, kStep);
    FinishCharacterStep(unbuffered, CharacterMoveOutcome{true, {0.0F, 1.0F, 0.0F}, false}, false, kStep);
    EXPECT_FALSE(StepCharacterIntent(unbuffered, {}, kGravity, kStep));
}

TEST(CharacterBodyMotionTest, AirControlScalesSteeringOnlyOffTheFloor) {
    // Built-in movement ramps velocity toward the input target instead of snapping to it: one
    // frame of the ground acceleration rate (14/s) moves a resting body to moveSpeed * 14 * step.
    constexpr float kGroundAccelerationRate = 14.0F;

    CharacterControllerComponent ground = OnFloor();
    ground.airControl = 0.0F;
    static_cast<void>(StepCharacterIntent(ground, {{1.0F, 0.0F, 0.0F}}, kGravity, kStep));
    EXPECT_FLOAT_EQ(ground.velocity.x, ground.moveSpeed * kGroundAccelerationRate * kStep);

    CharacterControllerComponent air{};
    air.timeSinceOnFloor = 1.0F;
    air.airControl = 0.25F;
    air.velocity.x = 0.0F;
    static_cast<void>(StepCharacterIntent(air, {{1.0F, 0.0F, 0.0F}}, kGravity, kStep));
    EXPECT_FLOAT_EQ(air.velocity.x,
                    air.moveSpeed * air.airControl * kGroundAccelerationRate * kStep);

    CharacterControllerComponent committed{};
    committed.timeSinceOnFloor = 1.0F;
    committed.airControl = 0.0F;
    committed.velocity.x = 3.0F;
    static_cast<void>(StepCharacterIntent(committed, {{-1.0F, 0.0F, 0.0F}}, kGravity, kStep));
    EXPECT_FLOAT_EQ(committed.velocity.x, 3.0F); // Keeps the jump's direction.
}

TEST(CharacterBodyMotionTest, FloatingIgnoresGravityAndRisesOnRequest) {
    CharacterControllerComponent c{};
    c.motionMode = CharacterMotionMode::Floating;
    static_cast<void>(StepCharacterIntent(c, {}, kGravity, kStep));
    EXPECT_FLOAT_EQ(c.velocity.y, 0.0F);
    static_cast<void>(StepCharacterIntent(c, {{}, 1.0F, false}, kGravity, kStep));
    EXPECT_FLOAT_EQ(c.velocity.y, c.moveSpeed);
    // A jump press means nothing while floating.
    EXPECT_FALSE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));
}

TEST(CharacterBodyMotionTest, WithoutBuiltInMovementOnlyGravityTouchesTheVelocity) {
    CharacterControllerComponent c{};
    c.builtInMovement = false;
    c.timeSinceOnFloor = 1.0F;
    c.velocity = Math::Vector3{2.0F, 0.0F, -1.0F};
    EXPECT_FALSE(StepCharacterIntent(c, {{1.0F, 0.0F, 1.0F}, 0.0F, true}, kGravity, kStep));
    EXPECT_FLOAT_EQ(c.velocity.x, 2.0F);
    EXPECT_FLOAT_EQ(c.velocity.z, -1.0F);
    EXPECT_FLOAT_EQ(c.velocity.y, kGravity * kStep);
}

TEST(CharacterBodyMotionTest, StandingOnTheFloorDoesNotBuildUpFallSpeed) {
    CharacterControllerComponent c = OnFloor();
    for (int step = 0; step < 120; ++step) {
        static_cast<void>(StepCharacterIntent(c, {}, kGravity, kStep));
        FinishCharacterStep(c, CharacterMoveOutcome{true, {0.0F, 1.0F, 0.0F}, false}, false, kStep);
    }
    EXPECT_FLOAT_EQ(c.velocity.y, 0.0F);
    EXPECT_FLOAT_EQ(c.timeSinceOnFloor, 0.0F);
}

TEST(CharacterBodyMotionTest, AJumpAndGravityFollowTheBodyUpNotWorldY) {
    CharacterControllerComponent c = OnFloor();
    c.upDirection = {0.0F, 0.0F, 1.0F};
    c.jumpHeight = 2.0F;
    ASSERT_TRUE(StepCharacterIntent(c, {{}, 0.0F, true}, kGravity, kStep));
    const float g = -kGravity;
    EXPECT_NEAR((c.velocity.z * c.velocity.z) / (2.0F * g), 2.0F, 1.0e-4F);
    EXPECT_NEAR(c.velocity.y, 0.0F, 1.0e-6F);
    EXPECT_FALSE(c.grounded);

    CharacterControllerComponent falling{};
    falling.upDirection = {0.0F, 0.0F, 1.0F};
    falling.timeSinceOnFloor = 1.0F;
    falling.builtInMovement = false;
    EXPECT_FALSE(StepCharacterIntent(falling, {}, kGravity, kStep));
    EXPECT_NEAR(falling.velocity.z, kGravity * kStep, 1.0e-6F);
    EXPECT_NEAR(falling.velocity.y, 0.0F, 1.0e-6F);
}

TEST(CharacterBodyMotionTest, ACeilingStopsTheRiseAndOptionallyTheWholeMove) {
    CharacterControllerComponent sliding{};
    sliding.velocity = Math::Vector3{3.0F, 4.0F, 0.0F};
    FinishCharacterStep(sliding, CharacterMoveOutcome{false, {0.0F, 1.0F, 0.0F}, true}, false, kStep);
    EXPECT_TRUE(sliding.isOnCeiling);
    EXPECT_FLOAT_EQ(sliding.velocity.y, 0.0F);
    EXPECT_FLOAT_EQ(sliding.velocity.x, 3.0F);

    CharacterControllerComponent stopping{};
    stopping.slideOnCeiling = false;
    stopping.velocity = Math::Vector3{3.0F, 4.0F, 1.0F};
    FinishCharacterStep(stopping, CharacterMoveOutcome{false, {0.0F, 1.0F, 0.0F}, true}, false, kStep);
    EXPECT_EQ(stopping.velocity, (Math::Vector3{0.0F, 0.0F, 0.0F}));
}

} // namespace
} // namespace UVE::Physics::Tests
