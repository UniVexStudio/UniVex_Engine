// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/character_controller_component_uve.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool IsCharacterControllerComponentValidUVE(const CharacterControllerComponentUVE& characterController) noexcept {
    const CharacterControllerComponentUVE& c = characterController;
    return c.motionMode <= CharacterMotionModeUVE::Floating && IsNonNegativeUVE(c.gravityScale) &&
           IsNonNegativeUVE(c.moveSpeed) && IsNonNegativeUVE(c.jumpHeight) && IsNonNegativeUVE(c.airControl) &&
           c.airControl <= 1.0F && IsNonNegativeUVE(c.coyoteTimeSeconds) && IsNonNegativeUVE(c.jumpBufferSeconds) &&
           IsNonNegativeUVE(c.floorSnapLength) && IsNonNegativeUVE(c.maxStepHeight) &&
           IsNonNegativeUVE(c.pushStrength) && IsNonNegativeUVE(c.maxPushSpeed) && c.maxSlides >= 1U &&
           c.maxSlides <= 32U && IsFiniteVectorUVE(c.velocity) && IsFiniteVectorUVE(c.floorNormal) &&
           IsNonNegativeUVE(c.timeSinceOnFloor) && IsNonNegativeUVE(c.jumpBufferRemaining);
}

} // namespace UVE::Scene
