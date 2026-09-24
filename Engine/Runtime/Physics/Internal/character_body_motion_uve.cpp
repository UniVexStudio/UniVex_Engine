// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/character_body_motion_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {
namespace {

// The coyote clock stops here: past any sensible window, and still finite for the validator.
constexpr float kMaximumTimeSinceOnFloorUVE = 3600.0F;

} // namespace

bool StepCharacterIntentUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMotionInputUVE& input,
                            const float gravityY, const float deltaTimeSeconds) noexcept {
    const bool floating = c.motionMode == Scene::CharacterMotionModeUVE::Floating;
    const float gravity = floating ? 0.0F : gravityY * c.gravityScale;
    bool jumped = false;
    if (c.builtInMovement) {
        const float steering = floating || c.isOnFloor ? 1.0F : c.airControl;
        c.velocity.x += (input.move.x * c.moveSpeed - c.velocity.x) * steering;
        c.velocity.z += (input.move.z * c.moveSpeed - c.velocity.z) * steering;
        if (floating) {
            c.velocity.y = std::clamp(input.rise, -1.0F, 1.0F) * c.moveSpeed;
        } else {
            // A press is remembered for Jump Buffer seconds, and a jump is still allowed for Coyote
            // Time seconds after leaving the floor, so it happens when the player meant it rather
            // than only on the exact step the rules allow.
            if (input.jumpPressed) {
                c.jumpBufferRemaining = std::max(c.jumpBufferSeconds, deltaTimeSeconds);
            }
            const bool canJump = c.isOnFloor || c.timeSinceOnFloor <= c.coyoteTimeSeconds;
            if (c.jumpBufferRemaining > 0.0F && canJump && gravity < 0.0F) {
                c.velocity.y = std::sqrt(2.0F * -gravity * c.jumpHeight);
                jumped = true;
                c.jumpBufferRemaining = 0.0F;
                c.isOnFloor = false;
                // Spent: walking off a ledge gives one coyote jump, not one per step of the window.
                c.timeSinceOnFloor = c.coyoteTimeSeconds + deltaTimeSeconds;
            } else {
                c.jumpBufferRemaining = std::max(0.0F, c.jumpBufferRemaining - deltaTimeSeconds);
            }
        }
    }
    // Standing on the floor cancels gravity rather than pressing into the floor every step.
    if (!floating && !jumped) {
        if (c.isOnFloor && c.velocity.y <= 0.0F) {
            c.velocity.y = 0.0F;
        } else {
            c.velocity.y += gravity * deltaTimeSeconds;
        }
    }
    return jumped;
}

void FinishCharacterStepUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMoveOutcomeUVE& outcome,
                            const bool jumped, const float deltaTimeSeconds) noexcept {
    c.isOnCeiling = outcome.hitCeiling;
    if (outcome.hitCeiling) {
        c.velocity.y = std::min(c.velocity.y, 0.0F);
        if (!c.slideOnCeiling) {
            c.velocity.x = 0.0F;
            c.velocity.z = 0.0F;
        }
    }
    c.isOnFloor = outcome.onFloor;
    c.floorNormal = outcome.onFloor ? outcome.floorNormal : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    if (outcome.onFloor) {
        c.timeSinceOnFloor = 0.0F;
        c.velocity.y = std::max(c.velocity.y, 0.0F);
    } else if (!jumped) {
        c.timeSinceOnFloor = std::min(c.timeSinceOnFloor + deltaTimeSeconds, kMaximumTimeSinceOnFloorUVE);
    }
}

} // namespace UVE::Physics
