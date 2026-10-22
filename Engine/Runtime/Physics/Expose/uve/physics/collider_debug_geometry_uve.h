// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>

#include "uve/component/collider_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Physics {

/// One world-space segment of a collider's debug wireframe.
///
/// Deliberately colourless. What a shape looks like is a rendering decision that belongs to the
/// viewport (a selected collider, an overlapping pair and a sleeping ragdoll body are the same
/// geometry in three colours), so this module answers only the question the viewport cannot: where
/// is the shape, in the world, in the tessellation the renderer wants.
struct ColliderDebugLineUVE final {
    Math::Vector3UVE from{};
    Math::Vector3UVE to{};

    [[nodiscard]] bool operator==(const ColliderDebugLineUVE&) const noexcept = default;
};

/// How finely a curved shape is tessellated. Eight segments is the floor - below it a sphere reads
/// as an octagon rather than as a sphere - and ninety-six is the ceiling, because a wireframe is
/// read at a glance and never at a silhouette.
struct ColliderDebugStyleUVE final {
    std::size_t circleSegments = 24U;

    /// Draw the conservative world AABB instead of the shape itself.
    ///
    /// This is what a broadphase actually sees, and it is the only honest way to show why a
    /// cylinder appears to collide a little early, or why a rotated box's bounds are bigger than the
    /// box: those are AABB artefacts the author cannot otherwise see.
    bool drawBoundsOnly = false;

    [[nodiscard]] bool operator==(const ColliderDebugStyleUVE&) const noexcept = default;
};

/// The tessellation actually used, after clamping `circleSegments` into [8, 96] and rounding it to
/// an even count so a capsule's half-circle arcs land exactly on the ring points.
[[nodiscard]] std::size_t ResolveColliderDebugSegmentsUVE(const ColliderDebugStyleUVE& style) noexcept;

/// Upper bound on the lines one collider can produce with `style`, so a caller can size its buffer
/// in one call instead of guessing: 12 for a box, 3 circles for a sphere, four circles plus two
/// half-fans per cap for a capsule, two rings for a cylinder.
[[nodiscard]] std::size_t ComputeColliderDebugLineCapacityUVE(const Scene::ColliderComponentUVE& collider,
                                                              const ColliderDebugStyleUVE& style = {}) noexcept;

struct ColliderDebugBuildResultUVE final {
    /// Lines written into the caller's buffer.
    std::size_t lineCount = 0U;
    /// True when the buffer was too small for the whole shape. The lines that DID fit are a
    /// complete prefix - a partial box is 9 of its 12 edges, never a truncated coordinate - so a
    /// short buffer degrades to a less detailed wireframe rather than to garbage.
    bool truncated = false;
};

/// World-space wireframe of one collider.
///
/// `worldCenter` and `worldRotation` are the entity's world transform. World SCALE is deliberately
/// not a parameter: this engine's colliders are scale-independent by convention -
/// `GetColliderLocalHalfExtentsUVE` and every narrow phase read the authored extents and ignore the
/// transform's scale - so a debug view that applied it would draw a shape the simulation does not
/// use, which is worse than no debug view at all. Scaling is visible the other way round: the mesh
/// scales and the wireframe does not, which is exactly the mismatch the author needs to see.
///
/// Shape by shape, all in the collider's own local frame before `worldRotation` is applied:
///  * Box       - 12 edges of the authored half extents.
///  * Sphere    - 3 great circles, XY / XZ / YZ.
///  * Capsule   - a ring at each end of the barrel, 4 barrel lines, and a semicircle fan over each
///                cap in two orthogonal planes. The barrel's half length is
///                `height * 0.5 - radius`, clamped at zero, so a "capsule" shorter than its own
///                diameter is drawn as the sphere it actually is rather than as an inside-out ring.
///  * Cylinder  - a ring at each end and 4 barrel lines. Its ends are drawn flat, which is a
///                cylinder's only difference from a capsule at this level of detail.
///
/// Refused (zero lines, `truncated` false) for a non-finite centre or rotation, a collider that
/// fails `IsColliderComponentValidUVE`, or a buffer that is empty. Nothing here allocates, and the
/// same input always produces the same lines in the same order.
[[nodiscard]] ColliderDebugBuildResultUVE BuildColliderDebugLinesUVE(
    const Scene::ColliderComponentUVE& collider, const Math::Vector3UVE& worldCenter,
    const Math::QuaternionUVE& worldRotation, std::span<ColliderDebugLineUVE> outLines,
    const ColliderDebugStyleUVE& style = {}) noexcept;

/// The 12 edges of an oriented box, as its own entry point.
///
/// Exposed because it is the one shape with no tessellation and no ambiguity, which makes it the
/// useful thing to test the transform path against - and because an AABB drawn from a broadphase
/// entry is the same 12 edges.
void BuildOrientedBoxDebugLinesUVE(const Math::Vector3UVE& center, const Math::Vector3UVE& halfExtents,
                                   const Math::QuaternionUVE& rotation,
                                   std::span<ColliderDebugLineUVE> outLines,
                                   ColliderDebugBuildResultUVE& outResult) noexcept;

} // namespace UVE::Physics
