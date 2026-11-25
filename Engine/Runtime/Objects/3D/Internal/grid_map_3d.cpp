// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "grid_map_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsGridMap3DComponentValid(const GridMap3DComponent& value) noexcept {
    if (!IsFinite3DObjectVector(value.cellSize) || value.cellSize.x <= 0.0F ||
        value.cellSize.y <= 0.0F || value.cellSize.z <= 0.0F ||
        value.cells.size() > kMaximumGridMapCells3D) {
        return false;
    }
    for (const GridMapCellEntry3D& c : value.cells) {
        if (c.itemIndex < 0 || c.orientationIndex >= 24U) {
            return false;
        }
    }
    return true;
}

GridMap3DComponent SanitizeGridMap3DComponent(const GridMap3DComponent& value) {
    GridMap3DComponent out = value;
    if (!IsFinite3DObjectVector(out.cellSize) || out.cellSize.x <= 0.0F ||
        out.cellSize.y <= 0.0F || out.cellSize.z <= 0.0F) {
        out.cellSize = Math::Vector3{2.0F, 2.0F, 2.0F};
    }
    if (out.cells.size() > kMaximumGridMapCells3D) {
        out.cells.resize(kMaximumGridMapCells3D);
    }
    std::erase_if(out.cells, [](const GridMapCellEntry3D& c) { return c.itemIndex < 0; });
    for (GridMapCellEntry3D& c : out.cells) {
        if (c.orientationIndex >= 24U) {
            c.orientationIndex = 0U;
        }
    }
    return out;
}

bool IsGridMap3DObjectDefinitionValid(const GridMap3DObjectDefinition& value) noexcept {
    return IsGridMap3DComponentValid(value.gridMap);
}

void ApplyGridMap3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const GridMap3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, GridMap3DObjectDefinition::defaultName);
    entityManager.AddComponent<GridMap3DComponent>(
        entity, SanitizeGridMap3DComponent(value.gridMap));
}

Math::Vector3 GridMap3D::MapToLocalPosition(const GridMap3DComponent& gridMap,
                                                     const Math::Vector3i& coord) noexcept {
    const Math::Vector3 safeSize =
        (IsFinite3DObjectVector(gridMap.cellSize) && gridMap.cellSize.x > 0.0F &&
         gridMap.cellSize.y > 0.0F && gridMap.cellSize.z > 0.0F)
            ? gridMap.cellSize
            : Math::Vector3{2.0F, 2.0F, 2.0F};
    return Math::Vector3{
        (static_cast<float>(coord.x) + (gridMap.centerX ? 0.5F : 0.0F)) * safeSize.x,
        (static_cast<float>(coord.y) + (gridMap.centerY ? 0.5F : 0.0F)) * safeSize.y,
        (static_cast<float>(coord.z) + (gridMap.centerZ ? 0.5F : 0.0F)) * safeSize.z};
}

Math::Vector3i GridMap3D::LocalPositionToMap(
    const GridMap3DComponent& gridMap, const Math::Vector3& localPosition) noexcept {
    if (!IsFinite3DObjectVector(localPosition)) {
        return Math::Vector3i{0, 0, 0};
    }
    const Math::Vector3 safeSize =
        (IsFinite3DObjectVector(gridMap.cellSize) && gridMap.cellSize.x > 0.0F &&
         gridMap.cellSize.y > 0.0F && gridMap.cellSize.z > 0.0F)
            ? gridMap.cellSize
            : Math::Vector3{2.0F, 2.0F, 2.0F};
    return Math::Vector3i{static_cast<std::int32_t>(std::floor(localPosition.x / safeSize.x)),
                             static_cast<std::int32_t>(std::floor(localPosition.y / safeSize.y)),
                             static_cast<std::int32_t>(std::floor(localPosition.z / safeSize.z))};
}

void GridMap3D::SetCellItem(GridMap3DComponent& gridMap, const Math::Vector3i& coord,
                                  const std::int32_t itemIndex,
                                  const std::uint8_t orientationIndex) {
    const auto it = std::find_if(
        gridMap.cells.begin(), gridMap.cells.end(),
        [&](const GridMapCellEntry3D& entry) { return entry.coord == coord; });
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
    } else if (gridMap.cells.size() < kMaximumGridMapCells3D) {
        gridMap.cells.push_back(GridMapCellEntry3D{coord, itemIndex, safeOrient});
    }
}

std::int32_t GridMap3D::GetCellItem(const GridMap3DComponent& gridMap,
                                          const Math::Vector3i& coord) noexcept {
    for (const GridMapCellEntry3D& entry : gridMap.cells) {
        if (entry.coord == coord) {
            return entry.itemIndex;
        }
    }
    return kGridMapInvalidItem;
}

} // namespace UVE::Scene
