// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsNavMeshVolume3DObjectComponentValidUVE(const NavMeshVolume3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DObjectStringUVE(value.navigationMeshAssetPath) &&
           std::isfinite(value.cellSize) && value.cellSize > 0.0F && std::isfinite(value.agentRadius) &&
           value.agentRadius > 0.0F && std::isfinite(value.agentHeight) &&
           value.agentHeight >= value.agentRadius * 2.0F && std::isfinite(value.maximumSlopeDegrees) &&
           value.maximumSlopeDegrees > 0.0F && value.maximumSlopeDegrees < 90.0F &&
           std::isfinite(value.maximumStepHeight) && value.maximumStepHeight >= 0.0F;
}

} // namespace UVE::Scene
