// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "aabb.h"
#include "vector3.h"

namespace UVE::Math {

/// A solid sphere: center plus radius. The shared shape the physics narrow phase, the collider
/// authoring (`ColliderComponent::MakeSphere`) and any future bounding-sphere path build
/// on — before this type, sphere geometry travelled as loose `center + radius` parameters at
/// every call site (see `Physics::Detail::IntersectRaySphere`'s old signature).
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Sphere {
    Vector3 center{0.0F, 0.0F, 0.0F};
    float radius = 0.0F;

    /// True when the center is finite and the radius is finite and non-negative. A zero radius
    /// is valid (a point sphere); a negative or non-finite radius is a caller bug, matching
    /// `Aabb`'s "min must not exceed max" convention of validating shape coherence, not use.
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        // Hand-rolled finite check (not IsFinite, which is not constexpr) so this stays
        // usable in constant expressions, per the module's convention. NaN fails the
        // self-compare; infinity fails the magnitude bound.
        constexpr float kMax = 3.402823466e+38F;
        const bool finiteCenter = center.x == center.x && center.y == center.y && center.z == center.z &&
                                  center.x <= kMax && center.x >= -kMax && center.y <= kMax &&
                                  center.y >= -kMax && center.z <= kMax && center.z >= -kMax;
        return finiteCenter && radius == radius && radius >= 0.0F && radius <= kMax;
    }

    /// True when `point` lies inside or on the surface. Caller ensures validity.
    [[nodiscard]] constexpr bool Contains(const Vector3& point) const noexcept {
        const Vector3 offset{point.x - center.x, point.y - center.y, point.z - center.z};
        return LengthSquared(offset) <= radius * radius;
    }

    /// True when the two solids overlap or touch. Caller ensures both are valid.
    [[nodiscard]] constexpr bool Intersects(const Sphere& other) const noexcept {
        const Vector3 offset{other.center.x - center.x, other.center.y - center.y,
                                other.center.z - center.z};
        const float combined = radius + other.radius;
        return LengthSquared(offset) <= combined * combined;
    }

    /// The smallest `Aabb` containing the sphere. Caller ensures validity.
    [[nodiscard]] constexpr Aabb ToAabb() const noexcept {
        return Aabb{Vector3{center.x - radius, center.y - radius, center.z - radius},
                       Vector3{center.x + radius, center.y + radius, center.z + radius}};
    }
};

[[nodiscard]] constexpr bool operator==(const Sphere& lhs, const Sphere& rhs) noexcept {
    return lhs.center == rhs.center && lhs.radius == rhs.radius;
}

[[nodiscard]] std::string ToString(const Sphere& sphere);

} // namespace UVE::Math
