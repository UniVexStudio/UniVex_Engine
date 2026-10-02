// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct Projectile3DNodeComponentUVE final {
    Math::Vector3UVE velocity{};
    Math::Vector3UVE acceleration{};
    float radius = 0.1F;
    float maxLifetime = 10.0F;
    float remainingLifetime = 10.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool active = true;
};

[[nodiscard]] bool IsProjectile3DNodeComponentValidUVE(const Projectile3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
