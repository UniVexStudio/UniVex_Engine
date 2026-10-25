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
    Cylinder = 3,
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
    bool disabled = false;
};

/// Returns conservative local half-extents for the supported descriptors. Collision and raycast
/// systems intentionally consume these as broad-phase AABBs in v1; callers that need exact sphere
/// or capsule contacts must not infer them from this helper.
[[nodiscard]] Math::Vector3UVE GetColliderLocalHalfExtentsUVE(
    const ColliderComponentUVE& collider) noexcept;

/// Smallest magnitude any one axis of a collider's world scale is allowed to reach.
///
/// A zero-scaled object still has to be a shape: a degenerate extent fails
/// `IsColliderComponentValidUVE` and the body would leave the world entirely, which reads as an
/// object that ignores its own collider rather than as one the author shrank to nothing.
inline constexpr float kMinimumColliderWorldScaleUVE = 1.0e-4F;

/// The collider a body actually occupies once its world scale is taken into account.
///
/// The mesh is drawn at `worldScale`, so the shape has to be too - a scaled cube whose collider
/// stayed unit-sized is a floor you fall through, which is the exact complaint this exists to
/// answer. Rather than teach every reader of a collider about scale (mixing it up in any one of
/// them is a silent, shape-specific bug), the scaled shape is produced once, here, and every
/// consumer keeps reading a plain collider.
///
/// Shapes with a single major axis cannot follow a non-uniform scale exactly, so a round shape
/// takes its widest cross-section axis and its own axis for length: a capsule squashed on X by 4
/// and left alone on Y becomes a thinner-but-not-shorter capsule, never an ellipse nothing else in
/// the engine could collide against. `Height` is floored at `2 * radius` for the same reason the
/// authored value is validated that way - a capsule cannot be shorter than the two caps it is
/// made of.
[[nodiscard]] ColliderComponentUVE ScaleColliderUVE(const ColliderComponentUVE& collider,
                                                    const Math::Vector3UVE& worldScale) noexcept;

/// Validates the value-only collider contract before scene persistence and explicit validation
/// consumers. Collision masks remain opaque bit fields; a zero layer is rejected because it would
/// make the collider unreachable by the layer-mask raycast contract. Runtime material extraction
/// retains its established defensive friction/restitution clamp for legacy hand-authored values.
[[nodiscard]] bool IsColliderComponentValidUVE(const ColliderComponentUVE& collider) noexcept;

/// The height below which a capsule has no barrel at all: its two hemispherical caps meet exactly
/// and the shape is a sphere wearing a capsule's shape type.
[[nodiscard]] float MinimumCapsuleHeightUVE(float radius) noexcept;

/// Gives a collider the proportions the shape it now names actually implies.
///
/// Called when the author changes `shapeType`, not on every edit, because it only rescues a shape
/// that cannot express itself with the numbers it inherited. Switching a Box to a Capsule is the
/// case that matters: the field default is height 1.0 and radius 0.5, so the cap height equals the
/// half height, the barrel is zero, and the author is shown a sphere while the Inspector says
/// Capsule. A capsule with a barrel is the shape they asked for, so the height is raised until the
/// barrel is as long as the capsule is wide - the proportion a capsule is drawn with everywhere -
/// and an authored height that already has a barrel is left untouched.
///
/// Nothing about the other shapes changes: a Box, Sphere or Cylinder is already exactly what its
/// numbers say.
[[nodiscard]] ColliderComponentUVE FitColliderShapeUVE(const ColliderComponentUVE& collider) noexcept;

} // namespace UVE::Scene
