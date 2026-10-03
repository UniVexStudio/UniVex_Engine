// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"

namespace UVE::Scene {

bool IsNavMeshVolume3DObjectComponentValidUVE(const NavMeshVolume3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DObjectStringUVE(value.navigationMeshAssetPath);
}

} // namespace UVE::Scene
