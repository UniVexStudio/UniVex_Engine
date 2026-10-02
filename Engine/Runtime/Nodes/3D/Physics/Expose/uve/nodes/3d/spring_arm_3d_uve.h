// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authored state for a SpringArm3D: the third-person-camera arm that keeps a child camera out
/// of walls. The arm extends along its entity's local +Z axis (behind the pivot; the engine's
/// camera convention looks down -Z, matching LightSystemUVE's own local-to-world direction
/// rule), casts a single ray of `armLength` each fixed simulation step through the engine's
/// raycast system, and shortens toward the first thing in `collisionMask` that would block the
/// view.
///
/// `currentLength` is pure runtime truth (hit length): how far along the arm the child may sit
/// RIGHT NOW. It is deliberately not serialized - the deserializer and the definition's Apply
/// both seed it to `armLength`, and each simulation step re-derives it.
///
/// The engine core moves every direct child by the CHANGE in currentLength each step (children
/// keep their authored pose plus the retraction delta, and an unobstructed arm hands back every
/// millimetre it borrowed, so the pose round-trips drift-free). Rotation of children is the
/// developer's business, matching the arm's design since the original Godot proposal.
///
/// Smoothing is where this deliberately differs from Godot's SpringArm3D, which has no smoothing
/// member and snaps both ways (docs list spring_length/margin/mask/shape only), so cameras pop
/// when geometry clears. Here retraction SNAPS - a camera may never clip into a wall for one
/// smooth frame's sake - while extension blends out at `smoothing` per second, so leaving a
/// doorway springs the camera back instead of teleporting it. `smoothing = 0` restores exact
/// Godot snap-both-ways behaviour.
struct SpringArm3DNodeComponentUVE final {

    float armLength = 4.0F;
    float margin = 0.1F;
    float smoothing = 8.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    float currentLength = 4.0F;
    bool enabled = true;
};

[[nodiscard]] bool IsSpringArm3DNodeComponentValidUVE(const SpringArm3DNodeComponentUVE& value) noexcept;

/// Authoring definition for the SpringArm3D node: the Node3D baseline plus the arm component,
/// seeded at full length the same way the deserializer seeds it. `defaultName` is what a
/// freshly created arm is called.
struct SpringArm3DNodeDefinitionUVE final {
    SpringArm3DNodeComponentUVE springArm;

    static constexpr std::string_view defaultName = "SpringArm3D";
};

[[nodiscard]] bool IsSpringArm3DNodeDefinitionValidUVE(const SpringArm3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity`. Every application first guarantees the Node3D
/// baseline (Transform/WorldTransform/Hierarchy/Name) through EnsureNode3DBaselineUVE - this
/// kind is Node3D plus its recipe. The entity must be alive and must not already have a
/// SpringArm3DNodeComponentUVE.
void ApplySpringArm3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const SpringArm3DNodeDefinitionUVE& value);

/// Pure target resolution: where the arm would sit this step before motion law. An unobstructed
/// arm targets its full length; a hit targets hit-distance minus `margin`, never below zero and
/// never beyond `armLength` (a hit report inconsistent with the arm's own authored envelope
/// degrades to "unobstructed" rather than exploding the arm).
[[nodiscard]] float ResolveSpringArm3DTargetUVE(std::optional<float> hitDistance, float margin,
                                                float armLength) noexcept;

/// Settling threshold for the extension blend: below this remaining distance the arm lands on
/// its target exactly rather than approaching asymptotically forever. That one rule is what
/// makes the obstruct-then-clear cycle restore the child's authored pose instead of permanently
/// borrowing a diminishing fraction of a millimetre.
inline constexpr float kSpringArm3DCompletionToleranceUVE = 1.0e-3F;

/// Pure motion law, authored data in and out so the tick wiring in the engine core stays one
/// scan: retraction snaps (never clip), extension blends at `smoothing`/s via
/// `1 - exp(-smoothing * dt)` (monotone, never overshoots), `smoothing <= 0` snaps both ways,
/// `dt <= 0` freezes, and a remaining distance under kSpringArm3DCompletionToleranceUVE settles
/// exactly. Non-finite or clamped-invalid inputs refuse to move rather than smear a garbage
/// pose - the validator above is what a scene should be checking first.
[[nodiscard]] float ResolveSpringArm3DLengthUVE(float currentLength, float targetLength,
                                                float smoothing, float dtSeconds) noexcept;

} // namespace UVE::Scene
