// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>

#include "collider_component.h"
#include "quaternion.h"
#include "ragdoll_solver.h"
#include "vector3.h"

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
[[nodiscard]] ColliderDebugBuildResultUVE BuildColliderDebugLinesUVE(
    const Scene::ColliderComponentUVE& collider, const Math::Vector3UVE& worldCenter,
    const Math::QuaternionUVE& worldRotation, std::span<ColliderDebugLineUVE> outLines,
    const ColliderDebugStyleUVE& style = {},
    const Math::Vector3UVE& worldScale = Math::Vector3UVE{1.0F, 1.0F, 1.0F}) noexcept;

/// The 12 edges of an oriented box, as its own entry point.
void BuildOrientedBoxDebugLinesUVE(const Math::Vector3UVE& center, const Math::Vector3UVE& halfExtents,
                                   const Math::QuaternionUVE& rotation,
                                   std::span<ColliderDebugLineUVE> outLines,
                                   ColliderDebugBuildResultUVE& outResult) noexcept;

/// Options controlling which parts of a ragdoll solver are emitted as viewport debug wireframes.
struct RagdollDebugDrawOptionsUVE final {
    ColliderDebugStyleUVE style{16U, false};
    bool drawBodies = true;
    bool drawJointCones = true;
    float jointConeLength = 0.14F;

    [[nodiscard]] bool operator==(const RagdollDebugDrawOptionsUVE&) const noexcept = default;
};

/// Emits world-space wireframe lines for every body capsule and (when enabled) every joint's
/// elliptical swing limit cone and hinge twist arc fan. Zero heap allocation, deterministic.
[[nodiscard]] ColliderDebugBuildResultUVE BuildRagdollDebugLinesUVE(
    const RagdollSolverUVE& solver, std::span<ColliderDebugLineUVE> outLines,
    const RagdollDebugDrawOptionsUVE& options = {}) noexcept;

} // namespace UVE::Physics
