// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// Supported authored collider descriptors. Box remains the legacy default; sphere and capsule
/// are represented here for validation and conservative world-AABB broad-phase bounds. Exact
/// shape narrow phases remain a separate future contract.
enum class ColliderShapeTypeUVE : std::uint8_t {
    Box = 0,
    Sphere = 1,
    Capsule = 2,
};

/// One of the master spec's named built-in components. The original halfExtents/layer/material
/// fields remain in their established order so existing aggregate initializers stay source
/// compatible; shape fields are trailing defaults. Capsule height is the total end-to-end height
/// along the local Y axis, including both hemispherical caps.
struct ColliderComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    float friction = 0.0F;
    float restitution = 0.0F;
    float density = 1.0F;
    ColliderShapeTypeUVE shapeType = ColliderShapeTypeUVE::Box;
    float radius = 0.5F;
    float height = 1.0F;
};

/// Returns conservative local half-extents for the supported descriptors. Collision and raycast
/// systems intentionally consume these as broad-phase AABBs in v1; callers that need exact sphere
/// or capsule contacts must not infer them from this helper.
[[nodiscard]] Math::Vector3UVE GetColliderLocalHalfExtentsUVE(
    const ColliderComponentUVE& collider) noexcept;

/// Validates the value-only collider contract before scene persistence and explicit validation
/// consumers. Collision masks remain opaque bit fields; a zero layer is rejected because it would
/// make the collider unreachable by the layer-mask raycast contract. Runtime material extraction
/// retains its established defensive friction/restitution clamp for legacy hand-authored values.
[[nodiscard]] bool IsColliderComponentValidUVE(const ColliderComponentUVE& collider) noexcept;

} // namespace UVE::Scene
