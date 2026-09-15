// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/world_environment_3d_uve.h"

namespace UVE::Scene {

bool IsWorldEnvironment3DNodeComponentValidUVE(const WorldEnvironment3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.skyAssetPath) && IsFinite3DNodeVectorUVE(value.ambientColor) &&
           IsFinite3DNodeVectorUVE(value.fogColor) && value.ambientColor.x >= 0.0F && value.ambientColor.y >= 0.0F &&
           value.ambientColor.z >= 0.0F && std::isfinite(value.ambientEnergy) && value.ambientEnergy >= 0.0F &&
           std::isfinite(value.exposure) && value.exposure > 0.0F && std::isfinite(value.fogDensity) &&
           value.fogDensity >= 0.0F;
}

} // namespace UVE::Scene
