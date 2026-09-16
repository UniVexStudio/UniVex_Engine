// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumLodLevelsUVE = 8U;

struct LodGroup3DNodeComponentUVE final {
    std::array<float, kMaximumLodLevelsUVE> distanceThresholds{10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    std::uint8_t levelCount = 4U;
    std::uint8_t currentLevel = 0U;
    bool enabled = true;
};

[[nodiscard]] bool IsLodGroup3DNodeComponentValidUVE(const LodGroup3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
