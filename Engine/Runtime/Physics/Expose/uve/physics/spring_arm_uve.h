// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Physics {

class IRaycastSystemUVE;

/// Why one fixed-step evaluation of a SpringArm3D did what it did, or why it did nothing.
enum class SpringArm3DStepCodeUVE : std::uint8_t {
    /// The arm was cast and its length committed.
    Stepped = 0,
    /// The arm is switched off: no cast ran, and the arm handed its length back to its child.
    Disabled,
    /// The step's duration was zero, negative or not finite.
    InvalidDeltaTime,
    /// No such entity.
    UnknownEntity,
    /// The entity is not a spring arm: no SpringArm3DComponentUVE.
    NotASpringArm,
    /// The arm has no world transform, so there is no origin to cast from.
    MissingTransform,
    /// The component is past its authored contract (a length outside `0..armLength`, a non-finite
    /// field). Refused rather than latched, so a malformed arm does not freeze a camera somewhere.
    InvalidComponent,
};

/// One step's outcome, with the numbers that produced it.
struct SpringArm3DStepResultUVE final {
    SpringArm3DStepCodeUVE code = SpringArm3DStepCodeUVE::UnknownEntity;
    /// Whether a ray was cast this step (`Stepped` only).
    bool castRan = false;
    bool hasHit = false;
    Scene::EntityUVE hitEntity{};
    Math::Vector3UVE hitPoint{};
    /// Distance along the arm to the hit, before the margin was taken off.
    float hitDistance = 0.0F;
    /// The authored reach.
    float armLength = 0.0F;
    /// The length the arm had before this step.
    float previousLength = 0.0F;
    /// Where the motion law aimed this step: the full reach when nothing is in the way, hit minus
    /// margin otherwise, and the full reach again while the arm is switched off.
    float targetLength = 0.0F;
    /// The runtime truth after this step.
    float currentLength = 0.0F;
    /// How far the arm moved this step, which is what every direct child rode.
    float lengthDelta = 0.0F;
    /// How many direct children actually moved (children without a transform are skipped).
    std::size_t movedChildCount = 0U;

    [[nodiscard]] bool IsSteppedUVE() const noexcept {
        return code == SpringArm3DStepCodeUVE::Stepped;
    }
    [[nodiscard]] bool IsDisabledUVE() const noexcept {
        return code == SpringArm3DStepCodeUVE::Disabled;
    }
    /// True when the arm gave ground this step - the direction that must never lag behind a wall.
    [[nodiscard]] bool ShortenedUVE() const noexcept {
        return lengthDelta < 0.0F;
    }
    /// True when the arm handed length back.
    [[nodiscard]] bool LengthenedUVE() const noexcept {
        return lengthDelta > 0.0F;
    }
    [[nodiscard]] bool MovedChildrenUVE() const noexcept {
        return movedChildCount > 0U;
    }
};

/// Runs one fixed-step evaluation of `entity`'s spring arm:
///
///   1. refuses an arm it cannot drive (see the codes above) rather than half-moving one;
///   2. casts a ray from the pivot's world position along its own local +Z - the axis the arm
///      extends along, behind the pivot, since this engine's cameras look down -Z;
///   3. resolves the target length through `Scene::SpringArm3DUVE` (full reach when clear, hit
///      distance minus `margin` when not, clamped to the authored envelope);
///   4. commits it through the node's motion law - retraction snaps so a camera never clips into
///      a wall for one smooth frame's sake, extension springs back at `smoothing`/s, and an arm
///      switched off hands its whole length back;
///   5. shifts every direct child by the change in length, along the arm's own local Z.
///
/// The child shift is a *delta*, never an absolute rewrite, so authored child offsets survive and
/// an arm that retracts and clears again restores the authored pose exactly instead of drifting a
/// fraction of a millimetre per doorway.
///
/// The world pose read here is the one the scene graph last resolved; the engine updates it every
/// frame after its fixed steps, and a caller driving this by hand should do the same.
///
/// The cast ignores the arm's own entity. What else it may not collide with is the authored
/// `collisionMask` - the same layer contract every other physics object uses, which is how a
/// character rig keeps the camera off its own body.
[[nodiscard]] SpringArm3DStepResultUVE StepSpringArm3DUVE(
    Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
    const IRaycastSystemUVE& raycastSystem, Scene::EntityUVE entity, float deltaTimeSeconds);

} // namespace UVE::Physics
