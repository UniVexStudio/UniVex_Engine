// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/level_streamer_3d_uve.h"

namespace UVE::Scene {

bool IsLevelStreamer3DNodeComponentValidUVE(const LevelStreamer3DNodeComponentUVE& value) noexcept {
    if (!IsBounded3DNodeStringUVE(value.levelPath) || !std::isfinite(value.loadDistance) ||
        value.loadDistance <= 0.0F || !std::isfinite(value.unloadDistance) ||
        value.unloadDistance <= value.loadDistance) {
        return false;
    }
    if (value.levelPath.empty()) {
        return !value.enabled && !value.loaded && !value.loadRequested;
    }
    return true;
}

} // namespace UVE::Scene
