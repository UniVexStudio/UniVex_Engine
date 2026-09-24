// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/character_body_motion_uve.h"

#include <cmath>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

using Scene::CharacterControllerComponentUVE;
using Scene::CharacterMotionModeUVE;

constexpr float kGravityUVE = -9.8F;
constexpr float kStepUVE = 1.0F / 60.0F;

[[nodiscard]] CharacterControllerComponentUVE OnFloorUVE() {
    CharacterControllerComponentUVE c{};
    c.isOnFloor = true;
    return c;
}

[[nodiscard]] CharacterMoveOutcomeUVE AirUVE() {
    return CharacterMoveOutcomeUVE{false, {0.0F, 1.0F, 0.0F}, false};
}

TEST(CharacterBodyMotionUVETest, AJumpReachesItsHeightWhateverTheGravity) {
    for (const float gravityScale : {0.5F, 1.0F, 3.0F}) {
        CharacterControllerComponentUVE c = OnFloorUVE();
        c.gravityScale = gravityScale;
        c.jumpHeight = 2.0F;
        ASSERT_TRUE(StepCharacterIntentUVE(c, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
        // v^2 = 2 g h: the apex of v under g is exactly jumpHeight.
        const float g = -kGravityUVE * gravityScale;
        EXPECT_NEAR((c.velocity.y * c.velocity.y) / (2.0F * g), 2.0F, 1.0e-4F);
        EXPECT_FALSE(c.isOnFloor);
    }
}

TEST(CharacterBodyMotionUVETest, CoyoteTimeAllowsOneLateJumpAfterWalkingOffALedge) {
    CharacterControllerComponentUVE c = OnFloorUVE();
    c.coyoteTimeSeconds = 0.1F;
    // Walks off: two steps in the air, still inside the window.
    for (int step = 0; step < 2; ++step) {
        EXPECT_FALSE(StepCharacterIntentUVE(c, {}, kGravityUVE, kStepUVE));
        FinishCharacterStepUVE(c, AirUVE(), false, kStepUVE);
    }
    EXPECT_TRUE(StepCharacterIntentUVE(c, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
    FinishCharacterStepUVE(c, AirUVE(), true, kStepUVE);
    // Spent: pressing again in the same fall does nothing.
    EXPECT_FALSE(StepCharacterIntentUVE(c, {{}, 0.0F, true}, kGravityUVE, kStepUVE));

    // Past the window, a press is only remembered, not acted on.
    CharacterControllerComponentUVE late = OnFloorUVE();
    late.coyoteTimeSeconds = 0.05F;
    late.jumpBufferSeconds = 0.0F;
    for (int step = 0; step < 6; ++step) {
        static_cast<void>(StepCharacterIntentUVE(late, {}, kGravityUVE, kStepUVE));
        FinishCharacterStepUVE(late, AirUVE(), false, kStepUVE);
    }
    EXPECT_FALSE(StepCharacterIntentUVE(late, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
}

TEST(CharacterBodyMotionUVETest, AJumpPressedJustBeforeLandingHappensOnLanding) {
    CharacterControllerComponentUVE c{};
    c.coyoteTimeSeconds = 0.0F;
    c.timeSinceOnFloor = 1.0F; // Well into a fall.
    c.jumpBufferSeconds = 0.1F;
    EXPECT_FALSE(StepCharacterIntentUVE(c, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
    FinishCharacterStepUVE(c, AirUVE(), false, kStepUVE);
    // Lands two steps later, with no new press.
    FinishCharacterStepUVE(c, CharacterMoveOutcomeUVE{true, {0.0F, 1.0F, 0.0F}, false}, false, kStepUVE);
    EXPECT_TRUE(StepCharacterIntentUVE(c, {}, kGravityUVE, kStepUVE));

    // Without a buffer the early press is simply lost.
    CharacterControllerComponentUVE unbuffered{};
    unbuffered.coyoteTimeSeconds = 0.0F;
    unbuffered.timeSinceOnFloor = 1.0F;
    unbuffered.jumpBufferSeconds = 0.0F;
    static_cast<void>(StepCharacterIntentUVE(unbuffered, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
    FinishCharacterStepUVE(unbuffered, AirUVE(), false, kStepUVE);
    FinishCharacterStepUVE(unbuffered, CharacterMoveOutcomeUVE{true, {0.0F, 1.0F, 0.0F}, false}, false, kStepUVE);
    EXPECT_FALSE(StepCharacterIntentUVE(unbuffered, {}, kGravityUVE, kStepUVE));
}

TEST(CharacterBodyMotionUVETest, AirControlScalesSteeringOnlyOffTheFloor) {
    CharacterControllerComponentUVE ground = OnFloorUVE();
    ground.airControl = 0.0F;
    static_cast<void>(StepCharacterIntentUVE(ground, {{1.0F, 0.0F, 0.0F}}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(ground.velocity.x, ground.moveSpeed);

    CharacterControllerComponentUVE air{};
    air.timeSinceOnFloor = 1.0F;
    air.airControl = 0.25F;
    air.velocity.x = 0.0F;
    static_cast<void>(StepCharacterIntentUVE(air, {{1.0F, 0.0F, 0.0F}}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(air.velocity.x, 0.25F * air.moveSpeed);

    CharacterControllerComponentUVE committed{};
    committed.timeSinceOnFloor = 1.0F;
    committed.airControl = 0.0F;
    committed.velocity.x = 3.0F;
    static_cast<void>(StepCharacterIntentUVE(committed, {{-1.0F, 0.0F, 0.0F}}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(committed.velocity.x, 3.0F); // Keeps the jump's direction.
}

TEST(CharacterBodyMotionUVETest, FloatingIgnoresGravityAndRisesOnRequest) {
    CharacterControllerComponentUVE c{};
    c.motionMode = CharacterMotionModeUVE::Floating;
    static_cast<void>(StepCharacterIntentUVE(c, {}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(c.velocity.y, 0.0F);
    static_cast<void>(StepCharacterIntentUVE(c, {{}, 1.0F, false}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(c.velocity.y, c.moveSpeed);
    // A jump press means nothing while floating.
    EXPECT_FALSE(StepCharacterIntentUVE(c, {{}, 0.0F, true}, kGravityUVE, kStepUVE));
}

TEST(CharacterBodyMotionUVETest, WithoutBuiltInMovementOnlyGravityTouchesTheVelocity) {
    CharacterControllerComponentUVE c{};
    c.builtInMovement = false;
    c.timeSinceOnFloor = 1.0F;
    c.velocity = Math::Vector3UVE{2.0F, 0.0F, -1.0F};
    EXPECT_FALSE(StepCharacterIntentUVE(c, {{1.0F, 0.0F, 1.0F}, 0.0F, true}, kGravityUVE, kStepUVE));
    EXPECT_FLOAT_EQ(c.velocity.x, 2.0F);
    EXPECT_FLOAT_EQ(c.velocity.z, -1.0F);
    EXPECT_FLOAT_EQ(c.velocity.y, kGravityUVE * kStepUVE);
}

TEST(CharacterBodyMotionUVETest, StandingOnTheFloorDoesNotBuildUpFallSpeed) {
    CharacterControllerComponentUVE c = OnFloorUVE();
    for (int step = 0; step < 120; ++step) {
        static_cast<void>(StepCharacterIntentUVE(c, {}, kGravityUVE, kStepUVE));
        FinishCharacterStepUVE(c, CharacterMoveOutcomeUVE{true, {0.0F, 1.0F, 0.0F}, false}, false, kStepUVE);
    }
    EXPECT_FLOAT_EQ(c.velocity.y, 0.0F);
    EXPECT_FLOAT_EQ(c.timeSinceOnFloor, 0.0F);
}

TEST(CharacterBodyMotionUVETest, ACeilingStopsTheRiseAndOptionallyTheWholeMove) {
    CharacterControllerComponentUVE sliding{};
    sliding.velocity = Math::Vector3UVE{3.0F, 4.0F, 0.0F};
    FinishCharacterStepUVE(sliding, CharacterMoveOutcomeUVE{false, {0.0F, 1.0F, 0.0F}, true}, false, kStepUVE);
    EXPECT_TRUE(sliding.isOnCeiling);
    EXPECT_FLOAT_EQ(sliding.velocity.y, 0.0F);
    EXPECT_FLOAT_EQ(sliding.velocity.x, 3.0F);

    CharacterControllerComponentUVE stopping{};
    stopping.slideOnCeiling = false;
    stopping.velocity = Math::Vector3UVE{3.0F, 4.0F, 1.0F};
    FinishCharacterStepUVE(stopping, CharacterMoveOutcomeUVE{false, {0.0F, 1.0F, 0.0F}, true}, false, kStepUVE);
    EXPECT_EQ(stopping.velocity, (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
}

} // namespace
} // namespace UVE::Physics::Tests
