// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/grid_map_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsGridMap3DComponentValidUVE(const GridMap3DComponentUVE& value) noexcept {
    if (!IsFinite3DObjectVectorUVE(value.cellSize) || value.cellSize.x <= 0.0F ||
        value.cellSize.y <= 0.0F || value.cellSize.z <= 0.0F ||
        value.cells.size() > kMaximumGridMapCells3DUVE) {
        return false;
    }
    for (const GridMapCellEntry3DUVE& c : value.cells) {
        if (c.itemIndex < 0 || c.orientationIndex >= 24U) {
            return false;
        }
    }
    return true;
}

GridMap3DComponentUVE SanitizeGridMap3DComponentUVE(const GridMap3DComponentUVE& value) {
    GridMap3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.cellSize) || out.cellSize.x <= 0.0F ||
        out.cellSize.y <= 0.0F || out.cellSize.z <= 0.0F) {
        out.cellSize = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    }
    if (out.cells.size() > kMaximumGridMapCells3DUVE) {
        out.cells.resize(kMaximumGridMapCells3DUVE);
    }
    std::erase_if(out.cells, [](const GridMapCellEntry3DUVE& c) { return c.itemIndex < 0; });
    for (GridMapCellEntry3DUVE& c : out.cells) {
        if (c.orientationIndex >= 24U) {
            c.orientationIndex = 0U;
        }
    }
    return out;
}

bool IsGridMap3DObjectDefinitionValidUVE(const GridMap3DObjectDefinitionUVE& value) noexcept {
    return IsGridMap3DComponentValidUVE(value.gridMap);
}

void ApplyGridMap3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const GridMap3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, GridMap3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<GridMap3DComponentUVE>(
        entity, SanitizeGridMap3DComponentUVE(value.gridMap));
}

Math::Vector3UVE GridMap3DUVE::MapToLocalPositionUVE(const GridMap3DComponentUVE& gridMap,
                                                     const Math::Vector3iUVE& coord) noexcept {
    const Math::Vector3UVE safeSize =
        (IsFinite3DObjectVectorUVE(gridMap.cellSize) && gridMap.cellSize.x > 0.0F &&
         gridMap.cellSize.y > 0.0F && gridMap.cellSize.z > 0.0F)
            ? gridMap.cellSize
            : Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    return Math::Vector3UVE{
        (static_cast<float>(coord.x) + (gridMap.centerX ? 0.5F : 0.0F)) * safeSize.x,
        (static_cast<float>(coord.y) + (gridMap.centerY ? 0.5F : 0.0F)) * safeSize.y,
        (static_cast<float>(coord.z) + (gridMap.centerZ ? 0.5F : 0.0F)) * safeSize.z};
}

Math::Vector3iUVE GridMap3DUVE::LocalPositionToMapUVE(
    const GridMap3DComponentUVE& gridMap, const Math::Vector3UVE& localPosition) noexcept {
    if (!IsFinite3DObjectVectorUVE(localPosition)) {
        return Math::Vector3iUVE{0, 0, 0};
    }
    const Math::Vector3UVE safeSize =
        (IsFinite3DObjectVectorUVE(gridMap.cellSize) && gridMap.cellSize.x > 0.0F &&
         gridMap.cellSize.y > 0.0F && gridMap.cellSize.z > 0.0F)
            ? gridMap.cellSize
            : Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    return Math::Vector3iUVE{static_cast<std::int32_t>(std::floor(localPosition.x / safeSize.x)),
                             static_cast<std::int32_t>(std::floor(localPosition.y / safeSize.y)),
                             static_cast<std::int32_t>(std::floor(localPosition.z / safeSize.z))};
}

void GridMap3DUVE::SetCellItemUVE(GridMap3DComponentUVE& gridMap, const Math::Vector3iUVE& coord,
                                  const std::int32_t itemIndex,
                                  const std::uint8_t orientationIndex) {
    const auto it = std::find_if(
        gridMap.cells.begin(), gridMap.cells.end(),
        [&](const GridMapCellEntry3DUVE& entry) { return entry.coord == coord; });
    if (itemIndex < 0) {
        if (it != gridMap.cells.end()) {
            gridMap.cells.erase(it);
        }
        return;
    }
    const std::uint8_t safeOrient = (orientationIndex < 24U) ? orientationIndex : 0U;
    if (it != gridMap.cells.end()) {
        it->itemIndex = itemIndex;
        it->orientationIndex = safeOrient;
    } else if (gridMap.cells.size() < kMaximumGridMapCells3DUVE) {
        gridMap.cells.push_back(GridMapCellEntry3DUVE{coord, itemIndex, safeOrient});
    }
}

std::int32_t GridMap3DUVE::GetCellItemUVE(const GridMap3DComponentUVE& gridMap,
                                          const Math::Vector3iUVE& coord) noexcept {
    for (const GridMapCellEntry3DUVE& entry : gridMap.cells) {
        if (entry.coord == coord) {
            return entry.itemIndex;
        }
    }
    return kGridMapInvalidItemUVE;
}

} // namespace UVE::Scene
