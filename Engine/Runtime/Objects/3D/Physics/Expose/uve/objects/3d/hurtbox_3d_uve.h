// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

struct Hurtbox3DComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
};

[[nodiscard]] bool IsHurtbox3DObjectComponentValidUVE(const Hurtbox3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
