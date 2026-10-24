// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// An oriented bounding box: center, per-axis half-extents, and rotation. The shared shape for
/// rotated-box geometry — the narrow phase's oriented-box raycast took `boxCenter +
/// boxHalfExtents + boxRotation` as loose parameters before this type existed.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct OrientedBoxUVE {
    Vector3UVE center{0.0F, 0.0F, 0.0F};
    Vector3UVE halfExtents{0.0F, 0.0F, 0.0F};
    QuaternionUVE rotation{};

    /// True when the center and extents are finite, every extent is non-negative, and the
    /// rotation is finite. Normalization is NOT checked: like `RayUVE`'s unit direction, a unit
    /// rotation is expected but not enforced, and callers transforming points must ensure it.
    [[nodiscard]] constexpr bool IsValidUVE() const noexcept {
        // Hand-rolled finite checks (IsFiniteUVE is not constexpr) so this stays usable in
        // constant expressions, per the module's convention. NaN fails the self-compare;
        // infinity fails the magnitude bound.
        constexpr float kMaxUVE = 3.402823466e+38F;
        const auto finite = [](const Vector3UVE& v) constexpr {
            return v.x == v.x && v.y == v.y && v.z == v.z && v.x <= kMaxUVE && v.x >= -kMaxUVE &&
                   v.y <= kMaxUVE && v.y >= -kMaxUVE && v.z <= kMaxUVE && v.z >= -kMaxUVE;
        };
        const bool finiteRotation = rotation.x == rotation.x && rotation.y == rotation.y &&
                                    rotation.z == rotation.z && rotation.w == rotation.w &&
                                    rotation.x <= kMaxUVE && rotation.x >= -kMaxUVE &&
                                    rotation.y <= kMaxUVE && rotation.y >= -kMaxUVE &&
                                    rotation.z <= kMaxUVE && rotation.z >= -kMaxUVE &&
                                    rotation.w <= kMaxUVE && rotation.w >= -kMaxUVE;
        return finite(center) && finite(halfExtents) && halfExtents.x >= 0.0F && halfExtents.y >= 0.0F &&
               halfExtents.z >= 0.0F && finiteRotation;
    }

    /// Transforms a world point into box-local space (caller ensures a unit rotation).
    [[nodiscard]] Vector3UVE ToLocalUVE(const Vector3UVE& worldPoint) const noexcept {
        const Vector3UVE offset{worldPoint.x - center.x, worldPoint.y - center.y,
                                worldPoint.z - center.z};
        return RotateVectorUVE(ConjugateUVE(rotation), offset);
    }

    /// True when `point` lies inside or on the box (caller ensures a unit rotation).
    [[nodiscard]] bool ContainsUVE(const Vector3UVE& point) const noexcept {
        const Vector3UVE local = ToLocalUVE(point);
        const float ax = local.x >= 0.0F ? local.x : -local.x;
        const float ay = local.y >= 0.0F ? local.y : -local.y;
        const float az = local.z >= 0.0F ? local.z : -local.z;
        return ax <= halfExtents.x && ay <= halfExtents.y && az <= halfExtents.z;
    }

    /// The smallest world-axis `AabbUVE` containing the rotated box, via the tested
    /// `ComposeTrsUVE` + `AabbUVE::TransformUVE` path rather than a second matrix derivation.
    /// Caller ensures a unit rotation.
    [[nodiscard]] AabbUVE ToAabbUVE() const noexcept;
};

[[nodiscard]] constexpr bool operator==(const OrientedBoxUVE& lhs, const OrientedBoxUVE& rhs) noexcept {
    return lhs.center == rhs.center && lhs.halfExtents == rhs.halfExtents &&
           lhs.rotation == rhs.rotation;
}

[[nodiscard]] std::string ToStringUVE(const OrientedBoxUVE& box);

} // namespace UVE::Math
