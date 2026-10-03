// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/navigation_region_3d_uve.h"

namespace UVE::Scene {

bool IsNavigationRegion3DObjectComponentValidUVE(const NavigationRegion3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DObjectStringUVE(value.navigationMeshAssetPath);
}

} // namespace UVE::Scene
