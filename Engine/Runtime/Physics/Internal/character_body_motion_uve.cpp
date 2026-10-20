// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/character_body_motion_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {
namespace {

// The coyote clock stops here: past any sensible window, and still finite for the validator.
constexpr float kMaximumTimeSinceOnFloorUVE = 3600.0F;
constexpr float kMinimumNormalizableLengthUVE = 1.0e-12F;

[[nodiscard]] Math::Vector3UVE CharacterUpUVE(const Scene::CharacterControllerComponentUVE& c) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(c.upDirection);
    if (!Math::IsFiniteUVE(c.upDirection) || !std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumNormalizableLengthUVE) {
        return Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    }
    return c.upDirection * (1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] float AlongUpUVE(const Math::Vector3UVE& value, const Math::Vector3UVE& up) noexcept {
    const float along = Math::DotUVE(value, up);
    return std::isfinite(along) ? along : 0.0F;
}

void SetAlongUpUVE(Math::Vector3UVE& velocity, const Math::Vector3UVE& up, const float along) noexcept {
    velocity = velocity - up * AlongUpUVE(velocity, up) + up * along;
}

} // namespace

bool StepCharacterIntentUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMotionInputUVE& input,
                            const float gravityY, const float deltaTimeSeconds) noexcept {
    const bool floating = c.motionMode == Scene::CharacterMotionModeUVE::Floating;
    const Math::Vector3UVE up = CharacterUpUVE(c);
    const float gravity = floating ? 0.0F : gravityY * c.gravityScale;
    bool jumped = false;
    if (c.builtInMovement) {
        const float steering = floating || c.grounded ? 1.0F : c.airControl;
        c.velocity.x += (input.move.x * c.moveSpeed - c.velocity.x) * steering;
        c.velocity.z += (input.move.z * c.moveSpeed - c.velocity.z) * steering;
        if (floating) {
            SetAlongUpUVE(c.velocity, up, std::clamp(input.rise, -1.0F, 1.0F) * c.moveSpeed);
        } else {
            // A press is remembered for Jump Buffer seconds, and a jump is still allowed for Coyote
            // Time seconds after leaving the floor, so it happens when the player meant it rather
            // than only on the exact step the rules allow.
            if (input.jumpPressed) {
                c.jumpBufferRemaining = std::max(c.jumpBufferSeconds, deltaTimeSeconds);
            }
            const bool canJump = c.grounded || c.timeSinceOnFloor <= c.coyoteTimeSeconds;
            if (c.jumpBufferRemaining > 0.0F && canJump && gravity < 0.0F) {
                SetAlongUpUVE(c.velocity, up, std::sqrt(2.0F * -gravity * c.jumpHeight));
                jumped = true;
                c.jumpBufferRemaining = 0.0F;
                c.grounded = false;
                // Spent: walking off a ledge gives one coyote jump, not one per step of the window.
                c.timeSinceOnFloor = c.coyoteTimeSeconds + deltaTimeSeconds;
            } else {
                c.jumpBufferRemaining = std::max(0.0F, c.jumpBufferRemaining - deltaTimeSeconds);
            }
        }
    }
    // Standing on the floor cancels gravity rather than pressing into the floor every step.
    if (!floating && !jumped) {
        if (c.grounded && AlongUpUVE(c.velocity, up) <= 0.0F) {
            SetAlongUpUVE(c.velocity, up, 0.0F);
        } else {
            c.velocity += up * (gravity * deltaTimeSeconds);
        }
    }
    return jumped;
}

void FinishCharacterStepUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMoveOutcomeUVE& outcome,
                            const bool jumped, const float deltaTimeSeconds) noexcept {
    const Math::Vector3UVE up = CharacterUpUVE(c);
    c.isOnCeiling = outcome.hitCeiling;
    if (outcome.hitCeiling) {
        if (AlongUpUVE(c.velocity, up) > 0.0F) {
            SetAlongUpUVE(c.velocity, up, 0.0F);
        }
        if (!c.slideOnCeiling) {
            c.velocity = up * std::min(AlongUpUVE(c.velocity, up), 0.0F);
        }
    }
    c.grounded = outcome.onFloor;
    c.floorNormal = outcome.onFloor ? outcome.floorNormal : up;
    if (outcome.onFloor) {
        c.timeSinceOnFloor = 0.0F;
        if (AlongUpUVE(c.velocity, up) < 0.0F) {
            SetAlongUpUVE(c.velocity, up, 0.0F);
        }
    } else if (!jumped) {
        c.timeSinceOnFloor = std::min(c.timeSinceOnFloor + deltaTimeSeconds, kMaximumTimeSinceOnFloorUVE);
    }
}

} // namespace UVE::Physics
