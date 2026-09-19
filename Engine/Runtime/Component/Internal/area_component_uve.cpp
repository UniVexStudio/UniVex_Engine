// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/area_component_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsAreaComponentValidUVE(const AreaComponentUVE& area) noexcept {
    return std::isfinite(area.halfExtents.x) && std::isfinite(area.halfExtents.y) &&
           std::isfinite(area.halfExtents.z) && area.halfExtents.x > 0.0F &&
           area.halfExtents.y > 0.0F && area.halfExtents.z > 0.0F && area.collisionLayer != 0U;
}

} // namespace UVE::Scene
