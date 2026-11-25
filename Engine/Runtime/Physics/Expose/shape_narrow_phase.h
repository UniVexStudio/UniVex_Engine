// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include "aabb.h"
#include "oriented_box.h"
#include "quaternion.h"
#include "sphere.h"

namespace UVE::Physics::Detail {

/// Computes the first bounded hit of a moving sphere against a stationary sphere along `ray`.
/// The combined radius is used for exact quadratic time-of-impact; an origin already inside or
/// touching the combined sphere reports distance zero with a zero normal. Invalid, non-finite,
/// overflowed, or out-of-range results fail closed. This value-only helper owns no ECS or backend state.
[[nodiscard]] std::optional<Math::RayHit> IntersectMovingSphereSphere(
    const Math::Ray& ray, float movingRadius, const Math::Sphere& target,
    float maxDistance) noexcept;

/// Intersects a point ray against a stationary sphere using the same bounded quadratic policy as
/// `IntersectMovingSphereSphere`; initial containment reports distance zero and a zero normal.
[[nodiscard]] std::optional<Math::RayHit> IntersectRaySphere(
    const Math::Ray& ray, const Math::Sphere& target, float maxDistance) noexcept;

/// Intersects a point ray against a finite capsule represented by its world-space centerline segment.
/// Cylinder and hemispherical-cap candidates are evaluated in double precision; initial containment
/// reports distance zero and a zero normal. Invalid or tangent-only inputs fail closed.
[[nodiscard]] std::optional<Math::RayHit> IntersectRayCapsule(
    const Math::Ray& ray, Math::Vector3 segmentStart, Math::Vector3 segmentEnd,
    float capsuleRadius, float maxDistance) noexcept;

/// Intersects a point ray against a stationary oriented box by transforming the ray to box-local
/// space and reusing bounded slab math. The returned normal is restored to world space.
[[nodiscard]] std::optional<Math::RayHit> IntersectRayOrientedBox(
    const Math::Ray& ray, const Math::OrientedBox& box, float maxDistance) noexcept;

/// Computes exact sphere-vs-axis-aligned-box penetration for the current Physics v1 geometry
/// contract. The returned axis points from the sphere center toward the box, matching
/// CollisionPair's first-to-second separation convention when the sphere is the first shape.
/// Touching boundaries are not intersections. This helper is value-only and deliberately does not
/// model oriented boxes, capsule pairs, sphere pairs, transforms, ECS state, or backend resources.
[[nodiscard]] std::optional<Math::Penetration> ComputeSphereAabbPenetration(
    const Math::Aabb& box, const Math::Sphere& sphere) noexcept;

/// Computes exact overlap distance between a capsule's axis segment and an axis-aligned box, then
/// returns a deterministic generic penetration axis/depth. The segment is supplied explicitly and
/// the capsule radius is applied as a Minkowski expansion. When the center segment is already
/// inside the box, the returned depth is the radius floor needed by the generic resolver. This
/// value-only helper does not model rotated capsules, capsule pairs, sphere pairs, ECS state, or
/// backend resources.
[[nodiscard]] std::optional<Math::Penetration> ComputeCapsuleAabbPenetration(
    const Math::Aabb& box, Math::Vector3 segmentStart, Math::Vector3 segmentEnd,
    float capsuleRadius) noexcept;

/// Computes exact sphere-vs-sphere penetration with a deterministic first-to-second axis. Touching
/// spheres are not intersections; coincident centers use +X as the stable fallback axis. This is a
/// copied value-only helper with no ECS, transform, or backend ownership.
[[nodiscard]] std::optional<Math::Penetration> ComputeSphereSpherePenetration(
    Math::Vector3 firstCenter, float firstRadius, Math::Vector3 secondCenter,
    float secondRadius) noexcept;

/// Computes exact capsule-vs-sphere penetration from the capsule centerline segment and sphere
/// center. The returned axis points from the capsule toward the sphere. Touching pairs are not
/// intersections; coincident closest points use +X as the stable fallback axis. This copied
/// value-only helper does not model rotated capsules, capsule pairs, transforms, ECS state, or
/// backend resources.
[[nodiscard]] std::optional<Math::Penetration> ComputeCapsuleSpherePenetration(
    Math::Vector3 capsuleSegmentStart, Math::Vector3 capsuleSegmentEnd, float capsuleRadius,
    Math::Vector3 sphereCenter, float sphereRadius) noexcept;

/// Computes exact penetration between two capsules represented by explicit centerline segments.
/// The returned axis points from the first capsule toward the second. Touching pairs are not
/// intersections; coincident closest points use +X as the stable fallback axis. This copied
/// value-only helper does not model rotated collider ownership, transforms, ECS state, or backend
/// resources.
[[nodiscard]] std::optional<Math::Penetration> ComputeCapsuleCapsulePenetration(
    Math::Vector3 firstSegmentStart, Math::Vector3 firstSegmentEnd, float firstRadius,
    Math::Vector3 secondSegmentStart, Math::Vector3 secondSegmentEnd, float secondRadius) noexcept;

/// Computes exact sphere-vs-oriented-box penetration by transforming the sphere center into box
/// local space. The returned axis is in world space and points from the sphere toward the box.
/// Touching boundaries are not intersections; invalid rotations fail closed.
[[nodiscard]] std::optional<Math::Penetration> ComputeSphereOrientedBoxPenetration(
    Math::Vector3 boxCenter, Math::Vector3 boxHalfExtents, Math::Quaternion boxRotation,
    Math::Vector3 sphereCenter, float sphereRadius) noexcept;

/// Computes exact capsule-vs-oriented-box penetration by transforming the capsule centerline into
/// box local space. The returned axis is in world space and points from the capsule toward the box.
/// Touching boundaries are not intersections; invalid rotations fail closed.
[[nodiscard]] std::optional<Math::Penetration> ComputeCapsuleOrientedBoxPenetration(
    Math::Vector3 boxCenter, Math::Vector3 boxHalfExtents, Math::Quaternion boxRotation,
    Math::Vector3 capsuleSegmentStart, Math::Vector3 capsuleSegmentEnd, float capsuleRadius) noexcept;

/// Computes exact oriented-box-vs-oriented-box penetration using the 15-axis separating-axis
/// theorem. The returned axis points from the first box toward the second; touching and invalid
/// inputs are rejected, and coincident centers use deterministic axis ordering.
[[nodiscard]] std::optional<Math::Penetration> ComputeOrientedBoxOrientedBoxPenetration(
    Math::Vector3 firstCenter, Math::Vector3 firstHalfExtents, Math::Quaternion firstRotation,
    Math::Vector3 secondCenter, Math::Vector3 secondHalfExtents,
    Math::Quaternion secondRotation) noexcept;

} // namespace UVE::Physics::Detail

// EOF
