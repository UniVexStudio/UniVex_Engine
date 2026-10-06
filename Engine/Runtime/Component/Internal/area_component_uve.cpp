// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/area_component_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsAreaSpaceOverrideModeValidUVE(const AreaSpaceOverrideModeUVE mode) noexcept {
    return static_cast<std::uint8_t>(mode) <=
           static_cast<std::uint8_t>(AreaSpaceOverrideModeUVE::ReplaceCombine);
}

[[nodiscard]] bool IsAreaComponentValidUVE(const AreaComponentUVE& area) noexcept {
    return std::isfinite(area.halfExtents.x) && std::isfinite(area.halfExtents.y) &&
           std::isfinite(area.halfExtents.z) && area.halfExtents.x > 0.0F &&
           area.halfExtents.y > 0.0F && area.halfExtents.z > 0.0F && area.collisionLayer != 0U &&
           IsAreaSpaceOverrideModeValidUVE(area.gravityOverride) &&
           IsAreaSpaceOverrideModeValidUVE(area.linearDampOverride) &&
           IsAreaSpaceOverrideModeValidUVE(area.angularDampOverride) &&
           std::isfinite(area.gravityDirection.x) && std::isfinite(area.gravityDirection.y) &&
           std::isfinite(area.gravityDirection.z) && std::isfinite(area.gravityMagnitude) &&
           std::isfinite(area.gravityPointOffset.x) && std::isfinite(area.gravityPointOffset.y) &&
           std::isfinite(area.gravityPointOffset.z) && std::isfinite(area.gravityPointUnitDistance) &&
           area.gravityPointUnitDistance >= 0.0F && std::isfinite(area.linearDamp) &&
           area.linearDamp >= 0.0F && std::isfinite(area.angularDamp) && area.angularDamp >= 0.0F;
}

} // namespace UVE::Scene
