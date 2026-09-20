// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/world_partition_3d_uve.h"

#include <cmath>

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

std::optional<WorldPartition3DCellIdUVE> ResolveWorldPartition3DCellIdForPositionUVE(
    const WorldPartition3DNodeComponentUVE& config,
    const Math::Vector3UVE& gridOriginWorld,
    const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsWorldPartition3DNodeComponentValidUVE(config)) {
        return std::nullopt;
    }
    const float localX = pointWorld.x - gridOriginWorld.x;
    const float localY = pointWorld.y - gridOriginWorld.y;
    const float localZ = pointWorld.z - gridOriginWorld.z;
    if (!std::isfinite(localX) || !std::isfinite(localY) || !std::isfinite(localZ)) {
        return std::nullopt;
    }
    // Each axis maps independently: depth-limited worlds (cellCounts[1] == 1, the common flat
    // case) still behave EXACTLY like the 3D rule, not a special case.
    const auto cellCoordinate = [&config](const float local, const std::uint32_t count,
                                         std::int32_t& outCoordinate) noexcept {
        if (local < 0.0F || local >= config.cellSize * static_cast<float>(count)) {
            return false; // strictly outside this axis' volume
        }
        // The floor rule: a point exactly on a cell start belongs to the cell it starts. The
        // < count*cellSize guard above already parked every edge coordinate inside [0, count)
        // exactly once, so floor() here can never produce an out-of-range id.
        const float scaled = local / config.cellSize;
        outCoordinate = static_cast<std::int32_t>(std::floor(scaled));
        return true;
    };
    WorldPartition3DCellIdUVE cell;
    if (!cellCoordinate(localX, config.cellCounts[0U], cell.x) ||
        !cellCoordinate(localY, config.cellCounts[1U], cell.y) ||
        !cellCoordinate(localZ, config.cellCounts[2U], cell.z)) {
        return std::nullopt;
    }
    return cell;
}

std::size_t ResolveWorldPartition3DCellLinearIndexUVE(
    const WorldPartition3DCellIdUVE& cell,
    const std::array<std::uint32_t, 3U>& cellCounts) noexcept {
    const std::uint32_t countX = cellCounts[0U];
    const std::uint32_t countY = cellCounts[1U];
    return static_cast<std::size_t>(cell.x) +
           static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(countX) +
           static_cast<std::size_t>(cell.z) * static_cast<std::size_t>(countX) *
               static_cast<std::size_t>(countY);
}

} // namespace UVE::Scene
