// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct LevelStreamer3DNodeComponentUVE final {
    std::string levelPath;
    float loadDistance = 250.0F;
    float unloadDistance = 300.0F;
    bool enabled = false;
    bool loaded = false;
    bool loadRequested = false;
};

[[nodiscard]] bool IsLevelStreamer3DNodeComponentValidUVE(const LevelStreamer3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
