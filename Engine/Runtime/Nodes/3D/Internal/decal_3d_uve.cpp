// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/decal_3d_uve.h"

namespace UVE::Scene {

bool IsDecal3DNodeComponentValidUVE(const Decal3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.materialAssetPath) && IsFinite3DNodeVectorUVE(value.size) &&
           value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F && std::isfinite(value.lifetime) &&
           value.lifetime >= 0.0F && value.projection <= DecalProjectionModeUVE::Cylinder;
}

} // namespace UVE::Scene
