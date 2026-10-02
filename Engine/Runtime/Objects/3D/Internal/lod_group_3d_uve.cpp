// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/lod_group_3d_uve.h"

#include <cmath>

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

void ResolveLodGroup3DLevelUVE(LodGroup3DNodeComponentUVE& value, const float distanceToCamera) noexcept {
    // Every degenerate case resolves to "draw at full detail". A configuration mistake should be
    // visible so it gets fixed, not silently hide geometry and look like a missing asset.
    if (!value.enabled || value.levelCount == 0U || value.levelCount > kMaximumLodLevelsUVE ||
        !std::isfinite(distanceToCamera)) {
        value.currentLevel = 0U;
        value.culledByDistance = false;
        return;
    }

    // Thresholds are ascending - the validator enforces it - so the first one the distance fits
    // under is the level. A linear scan over at most eight entries beats anything cleverer: it is
    // branch-predictable and the array is a single cache line.
    for (std::uint8_t level = 0U; level < value.levelCount; ++level) {
        if (distanceToCamera <= value.distanceThresholds[level]) {
            value.currentLevel = level;
            value.culledByDistance = false;
            return;
        }
    }

    // Past the end of the chain. currentLevel stays at the last real level rather than running one
    // past it, so a consumer that indexes by level cannot walk off the end just because an object
    // moved too far away.
    value.currentLevel = static_cast<std::uint8_t>(value.levelCount - 1U);
    value.culledByDistance = true;
}

} // namespace UVE::Scene
