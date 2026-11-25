// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <optional>
#include <string>

#include "matrix4x4.h"
#include "ray.h"
#include "vector3.h"

namespace UVE::Math {

/// An axis-aligned bounding box, used for mesh bounds and frustum culling (Part 7.2,
/// Increments 11-14) and AABB broad/narrow-phase collision (Part 7.5, Increment 15).
/// Deliberately minimal, matching Vector3/Quaternion's precedent: it provides AABB/ray/TOI
/// math only; exact sphere bounds and oriented bounding boxes remain outside this value contract.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Aabb {
    Vector3 min{0.0F, 0.0F, 0.0F};
    Vector3 max{0.0F, 0.0F, 0.0F};

    /// Builds an Aabb from a center point and per-axis half-extents.
    [[nodiscard]] static constexpr Aabb FromCenterExtents(Vector3 center, Vector3 extents) noexcept {
        return Aabb{
            Vector3{center.x - extents.x, center.y - extents.y, center.z - extents.z},
            Vector3{center.x + extents.x, center.y + extents.y, center.z + extents.z},
        };
    }

    [[nodiscard]] constexpr Vector3 GetCenter() const noexcept {
        const float floatCenterX = (min.x + max.x) * 0.5F;
        const float floatCenterY = (min.y + max.y) * 0.5F;
        const float floatCenterZ = (min.z + max.z) * 0.5F;
        const auto IsFiniteFloat = [](const float value) constexpr {
            return value == value && value <= std::numeric_limits<float>::max() &&
                   value >= -std::numeric_limits<float>::max();
        };
        if (IsFiniteFloat(floatCenterX) && IsFiniteFloat(floatCenterY) && IsFiniteFloat(floatCenterZ)) {
            return Vector3{floatCenterX, floatCenterY, floatCenterZ};
        }
        return Vector3{
            static_cast<float>((static_cast<double>(min.x) + static_cast<double>(max.x)) * 0.5),
            static_cast<float>((static_cast<double>(min.y) + static_cast<double>(max.y)) * 0.5),
            static_cast<float>((static_cast<double>(min.z) + static_cast<double>(max.z)) * 0.5),
        };
    }

    [[nodiscard]] constexpr Vector3 GetExtents() const noexcept {
        return Vector3{(max.x - min.x) * 0.5F, (max.y - min.y) * 0.5F, (max.z - min.z) * 0.5F};
    }

    /// Returns the smallest Aabb containing both `*this` and `other`.
    [[nodiscard]] constexpr Aabb Union(const Aabb& other) const noexcept {
        return Aabb{
            Vector3{min.x < other.min.x ? min.x : other.min.x, min.y < other.min.y ? min.y : other.min.y,
                       min.z < other.min.z ? min.z : other.min.z},
            Vector3{max.x > other.max.x ? max.x : other.max.x, max.y > other.max.y ? max.y : other.max.y,
                       max.z > other.max.z ? max.z : other.max.z},
        };
    }

    /// Returns the smallest axis-aligned box containing `*this` transformed by `matrix`.
    /// Implemented by transforming all 8 corners and taking the componentwise min/max —
    /// deliberately the simplest obviously-correct approach (not Arvo's faster analytical
    /// method); revisit only if profiling ever shows it matters.
    [[nodiscard]] Aabb Transform(const Matrix4x4& matrix) const noexcept;

    /// True iff `*this` and `other` overlap on all three axes (touching-but-not-overlapping
    /// edges count as not intersecting). CollisionSystem's broad-phase and current AABB
    /// narrow-phase are built on this overlap test; expanded sphere/capsule descriptors use
    /// conservative bounds until exact narrow phases exist.
    [[nodiscard]] constexpr bool Intersects(const Aabb& other) const noexcept {
        return min.x < other.max.x && max.x > other.min.x && min.y < other.max.y && max.y > other.min.y &&
               min.z < other.max.z && max.z > other.min.z;
    }
};

/// The minimum-translation-vector (MTV) between two overlapping AABBs: the shortest
/// single-axis push that separates them. `axis` is a unit vector pointing from `a` toward `b`;
/// `depth` is the overlap distance along that axis. Pure AABB geometry (same category as
/// Frustum's plane math), so it lives here rather than being duplicated inside
/// CollisionSystem (Part 7.5).
struct Penetration {
    Vector3 axis;
    float depth = 0.0F;
};

/// Returns the MTV separating `a` and `b`, or std::nullopt if they don't overlap
/// (`a.Intersects(b)` is false). When multiple axes tie for the smallest overlap, the first
/// in x/y/z order is returned — deterministic, not an arbitrary implementation detail.
[[nodiscard]] std::optional<Penetration> ComputePenetration(const Aabb& a, const Aabb& b) noexcept;

/// The result of a ray hitting an Aabb: `distance` along `ray.direction` (i.e. the hit point
/// is `ray.origin + ray.direction * distance`), and the unit-length face `normal` that was hit —
/// except when the ray's origin is already inside the box, in which case `distance = 0` and
/// `normal` is the zero vector (no face was actually crossed to reach it).
struct RayHit {
    float distance = 0.0F;
    Vector3 normal;
};

/// A normalized swept-AABB impact report. `normal` points from the moving AABB toward the target
/// along the first entered face, which is the direction to remove from remaining motion when
/// producing a sliding response.
struct SweptAabbHit {
    float time = 0.0F;
    Vector3 normal;
};

/// Returns the first normalized time in [0, 1] at which `moving` translated by `displacement`
/// reaches `target`, or nullopt when the boxes miss or already overlap. This is conservative for
/// expanded collider shapes because callers pass their broad-phase AABBs; it is not an exact
/// sphere/capsule or oriented-shape narrow phase.
[[nodiscard]] std::optional<SweptAabbHit> SweepAabb(
    const Aabb& moving, const Vector3& displacement, const Aabb& target) noexcept;

/// Returns the closest hit of `ray` against `aabb` within `[0, maxDistance]`, or std::nullopt if
/// the ray misses or the box is beyond `maxDistance`. Standard slab method — pure AABB geometry,
/// so it lives here next to ComputePenetration rather than inside RaycastSystem (Part 7.5,
/// Increment 16). A ray origin already inside the box reports `distance = 0` (hits immediately,
/// rather than "behind" the ray), matching common engine convention.
[[nodiscard]] std::optional<RayHit> IntersectRay(const Ray& ray, const Aabb& aabb,
                                                         float maxDistance) noexcept;

[[nodiscard]] constexpr bool operator==(const Aabb& lhs, const Aabb& rhs) noexcept {
    return lhs.min == rhs.min && lhs.max == rhs.max;
}

[[nodiscard]] constexpr bool operator!=(const Aabb& lhs, const Aabb& rhs) noexcept {
    return !(lhs == rhs);
}

/// Formats `box` as `"[min .. max]"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Aabb& box);

} // namespace UVE::Math
