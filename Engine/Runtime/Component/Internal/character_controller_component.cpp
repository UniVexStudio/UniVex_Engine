// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_controller_component.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool IsFiniteVector(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool IsCharacterControllerComponentValid(const CharacterControllerComponent& characterController) noexcept {
    const CharacterControllerComponent& c = characterController;
    return c.motionMode <= CharacterMotionMode::Floating && IsFiniteVector(c.upDirection) &&
           (c.upDirection.x * c.upDirection.x + c.upDirection.y * c.upDirection.y +
                c.upDirection.z * c.upDirection.z >
            0.0F) &&
           IsNonNegative(c.gravityScale) &&
           IsNonNegative(c.moveSpeed) && IsNonNegative(c.jumpHeight) && IsNonNegative(c.airControl) &&
           c.airControl <= 1.0F && IsNonNegative(c.coyoteTimeSeconds) && IsNonNegative(c.jumpBufferSeconds) &&
           IsNonNegative(c.floorSnapLength) && IsNonNegative(c.maxStepHeight) &&
           IsNonNegative(c.minStepWidth) && c.minStepWidth <= 1.0F &&
           IsNonNegative(c.floorMaxAngleDegrees) && c.floorMaxAngleDegrees <= 90.0F &&
           IsNonNegative(c.wallMinSlideAngleDegrees) && c.wallMinSlideAngleDegrees <= 90.0F &&
           IsNonNegative(c.safeMargin) && c.safeMargin <= 0.25F &&
           IsNonNegative(c.maximumPlatformSpeed) &&
           c.platformOnLeave <= CharacterPlatformLeaveMode::AddUpwardVelocity &&
           c.maximumContacts >= 1U && c.maximumContacts <= 64U &&
           IsNonNegative(c.pushStrength) && IsNonNegative(c.maxPushSpeed) && c.maxSlides >= 1U &&
           c.maxSlides <= 32U && IsFiniteVector(c.velocity) && IsFiniteVector(c.floorNormal) &&
           IsNonNegative(c.timeSinceOnFloor) && IsNonNegative(c.jumpBufferRemaining);
}

} // namespace UVE::Scene
