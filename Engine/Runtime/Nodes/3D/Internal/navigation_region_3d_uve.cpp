// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/navigation_region_3d_uve.h"

namespace UVE::Scene {

bool IsNavigationRegion3DNodeComponentValidUVE(const NavigationRegion3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DNodeStringUVE(value.navigationMeshAssetPath);
}

} // namespace UVE::Scene
