// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumRayCastExclusionsUVE = 8U;

struct RayCast3DNodeComponentUVE final {
    Math::Vector3UVE direction{0.0F, -1.0F, 0.0F};
    float length = 100.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool enabled = true;
    std::array<std::uint32_t, kMaximumRayCastExclusionsUVE> exclusions{};
    std::uint8_t exclusionCount = 0U;
    bool hit = false;
    Math::Vector3UVE hitPosition{};
    Math::Vector3UVE hitNormal{};
    std::uint32_t colliderLocalId = std::numeric_limits<std::uint32_t>::max();
};

[[nodiscard]] bool IsRayCast3DNodeComponentValidUVE(const RayCast3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
