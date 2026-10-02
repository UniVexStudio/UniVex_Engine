// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

struct Projectile3DComponentUVE final {
    Math::Vector3UVE velocity{};
    Math::Vector3UVE acceleration{};
    float radius = 0.1F;
    float maxLifetime = 10.0F;
    float remainingLifetime = 10.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool active = true;
};

[[nodiscard]] bool IsProjectile3DObjectComponentValidUVE(const Projectile3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
