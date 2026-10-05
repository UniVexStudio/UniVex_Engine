// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/visibility_region_3d_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsVisibilityRegion3DObjectComponentValidUVE(const VisibilityRegion3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
}

bool ResolveVisibilityRegion3DContainsPointUVE(const VisibilityRegion3DComponentUVE& config,
                                               const Math::Vector3UVE& regionWorldPosition,
                                               const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsVisibilityRegion3DObjectComponentValidUVE(config)) {
        return false;
    }
    const float localX = pointWorld.x - regionWorldPosition.x;
    const float localY = pointWorld.y - regionWorldPosition.y;
    const float localZ = pointWorld.z - regionWorldPosition.z;
    if (!std::isfinite(localX) || !std::isfinite(localY) || !std::isfinite(localZ)) {
        return false;
    }
    // Per-axis containment, boundary INCLUDED on both sides: the box is exactly what the author
    // sees (unlike the world partition's cells, which partition space and therefore need a
    // half-open boundary, one box's skin belongs to itself exactly once).
    return std::abs(localX) <= config.halfExtents.x && std::abs(localY) <= config.halfExtents.y &&
           std::abs(localZ) <= config.halfExtents.z;
}

bool ResolveVisibilityRegion3DAnyViewerInsideUVE(
    const VisibilityRegion3DComponentUVE& config, const Math::Vector3UVE& regionWorldPosition,
    const std::vector<Math::Vector3UVE>& viewerPositions) noexcept {
    for (const Math::Vector3UVE& viewer : viewerPositions) {
        if (ResolveVisibilityRegion3DContainsPointUVE(config, regionWorldPosition, viewer)) {
            return true;
        }
    }
    return false;
}

} // namespace UVE::Scene
