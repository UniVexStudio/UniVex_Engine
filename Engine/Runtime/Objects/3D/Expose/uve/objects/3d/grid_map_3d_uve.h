// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector3i_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::int32_t kGridMapInvalidItemUVE = -1;
inline constexpr std::size_t kMaximumGridMapCells3DUVE = 16384U;

struct GridMapCellEntry3DUVE final {
    Math::Vector3iUVE coord{0, 0, 0};
    std::int32_t itemIndex = 0;
    std::uint8_t orientationIndex = 0U;

    [[nodiscard]] bool operator==(const GridMapCellEntry3DUVE&) const = default;
};

/// 3D block/tile grid level authoring component (`cellSize`, `cells`, `centerX`, `centerY`,
/// `centerZ`).
struct GridMap3DComponentUVE final {
    Math::Vector3UVE cellSize{2.0F, 2.0F, 2.0F};
    bool centerX = true;
    bool centerY = true;
    bool centerZ = true;
    std::vector<GridMapCellEntry3DUVE> cells{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const GridMap3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsGridMap3DComponentValidUVE(const GridMap3DComponentUVE& value) noexcept;

[[nodiscard]] GridMap3DComponentUVE SanitizeGridMap3DComponentUVE(
    const GridMap3DComponentUVE& value);

struct GridMap3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "GridMap";
    GridMap3DComponentUVE gridMap{};
};

[[nodiscard]] bool IsGridMap3DObjectDefinitionValidUVE(
    const GridMap3DObjectDefinitionUVE& value) noexcept;

void ApplyGridMap3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const GridMap3DObjectDefinitionUVE& value);

class GridMap3DUVE final {
public:
    GridMap3DUVE() = delete;

    /// Converts integer grid cell coordinates to local-space position (including cell centering).
    [[nodiscard]] static Math::Vector3UVE MapToLocalPositionUVE(
        const GridMap3DComponentUVE& gridMap, const Math::Vector3iUVE& coord) noexcept;

    /// Converts a local-space position to integer grid cell coordinates.
    [[nodiscard]] static Math::Vector3iUVE LocalPositionToMapUVE(
        const GridMap3DComponentUVE& gridMap, const Math::Vector3UVE& localPosition) noexcept;

    /// Sets or clears (`itemIndex == kGridMapInvalidItemUVE`) the item at `coord`.
    static void SetCellItemUVE(GridMap3DComponentUVE& gridMap, const Math::Vector3iUVE& coord,
                               std::int32_t itemIndex, std::uint8_t orientationIndex = 0U);

    /// Returns the item index at `coord`, or `kGridMapInvalidItemUVE` when empty.
    [[nodiscard]] static std::int32_t GetCellItemUVE(const GridMap3DComponentUVE& gridMap,
                                                     const Math::Vector3iUVE& coord) noexcept;
};

} // namespace UVE::Scene
