// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/visibility_region_3d_uve.h"

namespace UVE::Scene {

bool IsVisibilityRegion3DNodeComponentValidUVE(const VisibilityRegion3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
}

} // namespace UVE::Scene
