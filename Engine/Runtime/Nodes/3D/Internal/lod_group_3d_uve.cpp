// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/lod_group_3d_uve.h"

namespace UVE::Scene {

bool IsLodGroup3DNodeComponentValidUVE(const LodGroup3DNodeComponentUVE& value) noexcept {
    if (value.levelCount == 0U || value.levelCount > kMaximumLodLevelsUVE || value.currentLevel >= value.levelCount) {
        return false;
    }
    for (std::size_t index = 0U; index < value.levelCount; ++index) {
        if (!std::isfinite(value.distanceThresholds[index]) || value.distanceThresholds[index] < 0.0F ||
            (index > 0U && value.distanceThresholds[index] <= value.distanceThresholds[index - 1U])) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Scene
