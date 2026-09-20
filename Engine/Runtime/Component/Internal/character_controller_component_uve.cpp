// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/character_controller_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsCharacterControllerComponentValidUVE(
    const CharacterControllerComponentUVE& characterController) noexcept {
    return std::isfinite(characterController.moveSpeed) && characterController.moveSpeed >= 0.0F &&
           std::isfinite(characterController.jumpHeight) && characterController.jumpHeight >= 0.0F &&
           std::isfinite(characterController.gravityScale) && characterController.gravityScale >= 0.0F &&
           std::isfinite(characterController.verticalVelocity);
}

} // namespace UVE::Scene
