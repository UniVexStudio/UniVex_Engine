// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct Hitbox3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
};

[[nodiscard]] bool IsHitbox3DNodeComponentValidUVE(const Hitbox3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
