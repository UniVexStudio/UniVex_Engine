// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "aabb.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Math {

/// An oriented bounding box: center, per-axis half-extents, and rotation. The shared shape for
/// rotated-box geometry — the narrow phase's oriented-box raycast took `boxCenter +
/// boxHalfExtents + boxRotation` as loose parameters before this type existed.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct OrientedBox {
    Vector3 center{0.0F, 0.0F, 0.0F};
    Vector3 halfExtents{0.0F, 0.0F, 0.0F};
    Quaternion rotation{};

    /// True when the center and extents are finite, every extent is non-negative, and the
    /// rotation is finite. Normalization is NOT checked: like `Ray`'s unit direction, a unit
    /// rotation is expected but not enforced, and callers transforming points must ensure it.
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        // Hand-rolled finite checks (IsFinite is not constexpr) so this stays usable in
        // constant expressions, per the module's convention. NaN fails the self-compare;
        // infinity fails the magnitude bound.
        constexpr float kMax = 3.402823466e+38F;
        const auto finite = [](const Vector3& v) constexpr {
            return v.x == v.x && v.y == v.y && v.z == v.z && v.x <= kMax && v.x >= -kMax &&
                   v.y <= kMax && v.y >= -kMax && v.z <= kMax && v.z >= -kMax;
        };
        const bool finiteRotation = rotation.x == rotation.x && rotation.y == rotation.y &&
                                    rotation.z == rotation.z && rotation.w == rotation.w &&
                                    rotation.x <= kMax && rotation.x >= -kMax &&
                                    rotation.y <= kMax && rotation.y >= -kMax &&
                                    rotation.z <= kMax && rotation.z >= -kMax &&
                                    rotation.w <= kMax && rotation.w >= -kMax;
        return finite(center) && finite(halfExtents) && halfExtents.x >= 0.0F && halfExtents.y >= 0.0F &&
               halfExtents.z >= 0.0F && finiteRotation;
    }

    /// Transforms a world point into box-local space (caller ensures a unit rotation).
    [[nodiscard]] Vector3 ToLocal(const Vector3& worldPoint) const noexcept {
        const Vector3 offset{worldPoint.x - center.x, worldPoint.y - center.y,
                                worldPoint.z - center.z};
        return RotateVector(Conjugate(rotation), offset);
    }

    /// True when `point` lies inside or on the box (caller ensures a unit rotation).
    [[nodiscard]] bool Contains(const Vector3& point) const noexcept {
        const Vector3 local = ToLocal(point);
        const float ax = local.x >= 0.0F ? local.x : -local.x;
        const float ay = local.y >= 0.0F ? local.y : -local.y;
        const float az = local.z >= 0.0F ? local.z : -local.z;
        return ax <= halfExtents.x && ay <= halfExtents.y && az <= halfExtents.z;
    }

    /// The smallest world-axis `Aabb` containing the rotated box, via the tested
    /// `ComposeTrs` + `Aabb::Transform` path rather than a second matrix derivation.
    /// Caller ensures a unit rotation.
    [[nodiscard]] Aabb ToAabb() const noexcept;
};

[[nodiscard]] constexpr bool operator==(const OrientedBox& lhs, const OrientedBox& rhs) noexcept {
    return lhs.center == rhs.center && lhs.halfExtents == rhs.halfExtents &&
           lhs.rotation == rhs.rotation;
}

[[nodiscard]] std::string ToString(const OrientedBox& box);

} // namespace UVE::Math
