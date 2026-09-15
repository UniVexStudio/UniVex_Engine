// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/world_partition_3d_uve.h"

namespace UVE::Scene {

bool IsWorldPartition3DNodeComponentValidUVE(const WorldPartition3DNodeComponentUVE& value) noexcept {
    if (!std::isfinite(value.cellSize) || value.cellSize <= 0.0F || value.maximumLoadedCells == 0U ||
        value.maximumLoadedCells > kMaximumStreamedCellsUVE || value.loadedCellCount > value.maximumLoadedCells) {
        return false;
    }
    for (const std::uint32_t count : value.cellCounts) {
        if (count == 0U || count > kMaximumStreamedCellsUVE) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Scene
