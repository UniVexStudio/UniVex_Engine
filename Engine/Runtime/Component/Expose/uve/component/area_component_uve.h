// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// A bounded, non-physical overlap volume. AreaComponentUVE participates in read-only
/// AreaOverlapSystemUVE queries but never enters rigid-body collision resolution. The shape is
/// intentionally an axis-aligned box for this increment; shape breadth is a separate contract.
struct AreaComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool monitoring = true;
    bool monitorable = true;
};

[[nodiscard]] bool IsAreaComponentValidUVE(const AreaComponentUVE& area) noexcept;

} // namespace UVE::Scene
