// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "entity.h"
#include "vector3.h"
#include "vector3i.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::int32_t kGridMapInvalidItem = -1;
inline constexpr std::size_t kMaximumGridMapCells3D = 16384U;

struct GridMapCellEntry3D final {
    Math::Vector3i coord{0, 0, 0};
    std::int32_t itemIndex = 0;
    std::uint8_t orientationIndex = 0U;

    [[nodiscard]] bool operator==(const GridMapCellEntry3D&) const = default;
};

/// 3D block/tile grid level authoring component (`cellSize`, `cells`, `centerX`, `centerY`,
/// `centerZ`).
struct GridMap3DComponent final {
    Math::Vector3 cellSize{2.0F, 2.0F, 2.0F};
    bool centerX = true;
    bool centerY = true;
    bool centerZ = true;
    std::vector<GridMapCellEntry3D> cells{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const GridMap3DComponent&) const = default;
};

[[nodiscard]] bool IsGridMap3DComponentValid(const GridMap3DComponent& value) noexcept;

[[nodiscard]] GridMap3DComponent SanitizeGridMap3DComponent(
    const GridMap3DComponent& value);

struct GridMap3DObjectDefinition final {
    static constexpr std::string_view defaultName = "GridMap";
    GridMap3DComponent gridMap{};
};

[[nodiscard]] bool IsGridMap3DObjectDefinitionValid(
    const GridMap3DObjectDefinition& value) noexcept;

void ApplyGridMap3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const GridMap3DObjectDefinition& value);

class GridMap3D final {
public:
    GridMap3D() = delete;

    /// Converts integer grid cell coordinates to local-space position (including cell centering).
    [[nodiscard]] static Math::Vector3 MapToLocalPosition(
        const GridMap3DComponent& gridMap, const Math::Vector3i& coord) noexcept;

    /// Converts a local-space position to integer grid cell coordinates.
    [[nodiscard]] static Math::Vector3i LocalPositionToMap(
        const GridMap3DComponent& gridMap, const Math::Vector3& localPosition) noexcept;

    /// Sets or clears (`itemIndex == kGridMapInvalidItem`) the item at `coord`.
    static void SetCellItem(GridMap3DComponent& gridMap, const Math::Vector3i& coord,
                               std::int32_t itemIndex, std::uint8_t orientationIndex = 0U);

    /// Returns the item index at `coord`, or `kGridMapInvalidItem` when empty.
    [[nodiscard]] static std::int32_t GetCellItem(const GridMap3DComponent& gridMap,
                                                     const Math::Vector3i& coord) noexcept;
};

} // namespace UVE::Scene
