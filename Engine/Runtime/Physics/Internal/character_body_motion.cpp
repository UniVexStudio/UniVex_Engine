// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_body_motion.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {
namespace {

// The coyote clock stops here: past any sensible window, and still finite for the validator.
constexpr float kMaximumTimeSinceOnFloor = 3600.0F;
constexpr float kMinimumNormalizableLength = 1.0e-12F;

[[nodiscard]] Math::Vector3 CharacterUp(const Scene::CharacterControllerComponent& c) noexcept {
    const float lengthSquared = Math::LengthSquared(c.upDirection);
    if (!Math::IsFinite(c.upDirection) || !std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumNormalizableLength) {
        return Math::Vector3{0.0F, 1.0F, 0.0F};
    }
    return c.upDirection * (1.0F / std::sqrt(lengthSquared));
}

[[nodiscard]] float AlongUp(const Math::Vector3& value, const Math::Vector3& up) noexcept {
    const float along = Math::Dot(value, up);
    return std::isfinite(along) ? along : 0.0F;
}

void SetAlongUp(Math::Vector3& velocity, const Math::Vector3& up, const float along) noexcept {
    velocity = velocity - up * AlongUp(velocity, up) + up * along;
}

} // namespace

bool StepCharacterIntent(Scene::CharacterControllerComponent& c, const CharacterMotionInput& input,
                            const float gravityY, const float deltaTimeSeconds) noexcept {
    const bool floating = c.motionMode == Scene::CharacterMotionMode::Floating;
    const Math::Vector3 up = CharacterUp(c);
    const float gravity = floating ? 0.0F : gravityY * c.gravityScale;
    bool jumped = false;
    if (c.builtInMovement) {
        constexpr float kGroundAccelerationRate = 14.0F;
        constexpr float kGroundDecelerationRate = 18.0F;
        const float targetX = input.move.x * c.moveSpeed;
        const float targetZ = input.move.z * c.moveSpeed;
        const bool moving = std::abs(targetX) > 1.0e-4F || std::abs(targetZ) > 1.0e-4F;
        const float rate = moving ? kGroundAccelerationRate : kGroundDecelerationRate;
        // airControl scales steering only off the floor, and 0.0F means none at all: the body
        // keeps the momentum its jump gave it instead of drifting toward the input target.
        // Floating bodies steer instantly - creative-flight response, not ground inertia.
        const float airScale = c.airControl > 0.0F ? std::clamp(c.airControl, 0.0F, 1.0F) : 0.0F;
        const float alpha = floating ? 1.0F : std::clamp((c.grounded ? rate : rate * airScale) * deltaTimeSeconds, 0.0F, 1.0F);
        c.velocity.x += (targetX - c.velocity.x) * alpha;
        c.velocity.z += (targetZ - c.velocity.z) * alpha;
        if (floating) {
            SetAlongUp(c.velocity, up, std::clamp(input.rise, -1.0F, 1.0F) * c.moveSpeed);
        } else {
            // A press is remembered for Jump Buffer seconds, and a jump is still allowed for Coyote
            // Time seconds after leaving the floor, so it happens when the player meant it rather
            // than only on the exact step the rules allow.
            if (input.jumpPressed) {
                c.jumpBufferRemaining = std::max(c.jumpBufferSeconds, deltaTimeSeconds);
            }
            const bool canJump = c.grounded || c.timeSinceOnFloor <= c.coyoteTimeSeconds;
            if (c.jumpBufferRemaining > 0.0F && canJump && gravity < 0.0F) {
                SetAlongUp(c.velocity, up, std::sqrt(2.0F * -gravity * c.jumpHeight));
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
        if (c.grounded && AlongUp(c.velocity, up) <= 0.0F) {
            SetAlongUp(c.velocity, up, 0.0F);
        } else {
            c.velocity += up * (gravity * deltaTimeSeconds);
        }
    }
    return jumped;
}

void FinishCharacterStep(Scene::CharacterControllerComponent& c, const CharacterMoveOutcome& outcome,
                            const bool jumped, const float deltaTimeSeconds) noexcept {
    const Math::Vector3 up = CharacterUp(c);
    c.isOnCeiling = outcome.hitCeiling;
    if (outcome.hitCeiling) {
        if (AlongUp(c.velocity, up) > 0.0F) {
            SetAlongUp(c.velocity, up, 0.0F);
        }
        if (!c.slideOnCeiling) {
            c.velocity = up * std::min(AlongUp(c.velocity, up), 0.0F);
        }
    }
    c.grounded = outcome.onFloor;
    c.floorNormal = outcome.onFloor ? outcome.floorNormal : up;
    if (outcome.onFloor) {
        c.timeSinceOnFloor = 0.0F;
        if (AlongUp(c.velocity, up) < 0.0F) {
            SetAlongUp(c.velocity, up, 0.0F);
        }
    } else if (!jumped) {
        c.timeSinceOnFloor = std::min(c.timeSinceOnFloor + deltaTimeSeconds, kMaximumTimeSinceOnFloor);
    }
}

} // namespace UVE::Physics
