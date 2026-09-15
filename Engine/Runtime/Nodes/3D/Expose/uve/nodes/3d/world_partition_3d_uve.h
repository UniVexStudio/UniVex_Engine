// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumStreamedCellsUVE = 4096U;

struct WorldPartition3DNodeComponentUVE final {
    float cellSize = 128.0F;
    std::array<std::uint32_t, 3U> cellCounts{16U, 1U, 16U};
    std::uint32_t maximumLoadedCells = 64U;
    std::uint32_t loadedCellCount = 0U;
    bool enabled = true;
};

[[nodiscard]] bool IsWorldPartition3DNodeComponentValidUVE(const WorldPartition3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
