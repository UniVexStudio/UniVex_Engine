// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "uve/math/aabb_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// A solid sphere: center plus radius. The shared shape the physics narrow phase, the collider
/// authoring (`ColliderComponentUVE::MakeSphereUVE`) and any future bounding-sphere path build
/// on — before this type, sphere geometry travelled as loose `center + radius` parameters at
/// every call site (see `Physics::Detail::IntersectRaySphereUVE`'s old signature).
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct SphereUVE {
    Vector3UVE center{0.0F, 0.0F, 0.0F};
    float radius = 0.0F;

    /// True when the center is finite and the radius is finite and non-negative. A zero radius
    /// is valid (a point sphere); a negative or non-finite radius is a caller bug, matching
    /// `AabbUVE`'s "min must not exceed max" convention of validating shape coherence, not use.
    [[nodiscard]] constexpr bool IsValidUVE() const noexcept {
        // Hand-rolled finite check (not IsFiniteUVE, which is not constexpr) so this stays
        // usable in constant expressions, per the module's convention. NaN fails the
        // self-compare; infinity fails the magnitude bound.
        constexpr float kMaxUVE = 3.402823466e+38F;
        const bool finiteCenter = center.x == center.x && center.y == center.y && center.z == center.z &&
                                  center.x <= kMaxUVE && center.x >= -kMaxUVE && center.y <= kMaxUVE &&
                                  center.y >= -kMaxUVE && center.z <= kMaxUVE && center.z >= -kMaxUVE;
        return finiteCenter && radius == radius && radius >= 0.0F && radius <= kMaxUVE;
    }

    /// True when `point` lies inside or on the surface. Caller ensures validity.
    [[nodiscard]] constexpr bool ContainsUVE(const Vector3UVE& point) const noexcept {
        const Vector3UVE offset{point.x - center.x, point.y - center.y, point.z - center.z};
        return LengthSquaredUVE(offset) <= radius * radius;
    }

    /// True when the two solids overlap or touch. Caller ensures both are valid.
    [[nodiscard]] constexpr bool IntersectsUVE(const SphereUVE& other) const noexcept {
        const Vector3UVE offset{other.center.x - center.x, other.center.y - center.y,
                                other.center.z - center.z};
        const float combined = radius + other.radius;
        return LengthSquaredUVE(offset) <= combined * combined;
    }

    /// The smallest `AabbUVE` containing the sphere. Caller ensures validity.
    [[nodiscard]] constexpr AabbUVE ToAabbUVE() const noexcept {
        return AabbUVE{Vector3UVE{center.x - radius, center.y - radius, center.z - radius},
                       Vector3UVE{center.x + radius, center.y + radius, center.z + radius}};
    }
};

[[nodiscard]] constexpr bool operator==(const SphereUVE& lhs, const SphereUVE& rhs) noexcept {
    return lhs.center == rhs.center && lhs.radius == rhs.radius;
}

[[nodiscard]] std::string ToStringUVE(const SphereUVE& sphere);

} // namespace UVE::Math
