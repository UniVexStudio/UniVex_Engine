// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct WorldEnvironment3DNodeComponentUVE final {
    std::string skyAssetPath;
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    float ambientEnergy = 1.0F;
    float exposure = 1.0F;
    float fogDensity = 0.0F;
    bool fogEnabled = false;
    bool postProcessingEnabled = false;
};

[[nodiscard]] bool IsWorldEnvironment3DNodeComponentValidUVE(const WorldEnvironment3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
