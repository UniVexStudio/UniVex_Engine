// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/math/vector3_uve.h"
#include "uve/physics/i_collision_system_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Physics {

/// One fixed-step move of a Kinematic3D body: the body an author drives by target velocity - a lift,
/// a moving platform, a door, a conveyor - as opposed to a character (driven by intent) or a rigid
/// body (driven by forces).
///
/// WHERE THE MOTION COMES FROM. The authored `targetVelocity` says where the body is going, and
/// `interpolation` says how quickly it gets up to that speed: 1 is the body moving at its target
/// the very first step, lower values ease into it. The eased result is the body's actual velocity,
/// and it is written to the body's Rigid3DComponentUVE::velocity - so a character standing on the
/// platform reads how fast it is really moving when it steps off, and a script reading the body's
/// velocity sees what happened rather than what was asked for.
///
/// WHAT IT MOVES THROUGH. The move itself is the engine's own kinematic move - the same swept,
/// sub-stepped, collision-aware path the character controller exercises - so a fast platform cannot
/// tunnel through a thin wall, a body stopped by geometry stops short of it instead of overlapping,
/// and the rigid bodies it walks into are pushed the way a moving platform pushes them. A body that
/// must follow its authored path exactly, through anything, is a *teleport*, and an author does that
/// by writing the transform; this mover's job is to be honest about the world it moves in.
enum class KinematicBodyStepCodeUVE : std::uint8_t {
    /// The body was moved this step.
    Moved = 0,
    /// The step's duration was zero, negative or not finite.
    InvalidDeltaTime,
    /// No such entity.
    UnknownEntity,
    /// The entity is not a kinematic body: no Kinematic3DComponentUVE.
    NotAKinematicBody,
    /// A kinematic body needs a collider to collide with, and a transform to be somewhere.
    MissingCollider,
    MissingTransform,
    /// A rigid body that is not kinematic is driven by forces. This mover owns its body's motion.
    NonKinematicBody,
    /// The body could not be described in the world (non-finite transform or collider extents).
    InvalidWorld,
    /// The body is not switched on - `active` is false, or its object is stopped and its disable
    /// mode keeps it out of the simulation. It stayed where it was and its velocity was zeroed, so
    /// nothing rides a body that is not moving.
    Idle,
};

struct KinematicBodyStepResultUVE final {
    KinematicBodyStepCodeUVE code = KinematicBodyStepCodeUVE::UnknownEntity;
    /// What the author asked the body to do this step.
    Math::Vector3UVE targetVelocity{};
    /// The velocity the body ended the step with: the eased target on a clear path, and what the
    /// body actually managed when the world got in the way - a body stopped by a wall has no
    /// velocity to hand a rider stepping off it.
    Math::Vector3UVE velocity{};
    /// Where the body actually went - the swept, collision-aware result, which is shorter than
    /// `velocity * deltaTimeSeconds` when something was in the way.
    Math::Vector3UVE appliedMotion{};
    /// The part of this step's requested motion the world did not let the body make - the request
    /// minus what was applied. The whole request for a body stopped by a wall, nothing for a body
    /// that had a clear path, and zero for a body refused before the move (its code says why).
    Math::Vector3UVE remainingMotion{};
    /// True when geometry stopped the move short.
    bool blocked = false;
    /// True when the body was standing on something at the end of the move.
    bool grounded = false;
    Math::Vector3UVE groundNormal{};
    /// How many contacts the move resolved.
    std::size_t contactCount = 0U;
    /// How many rigid bodies the body pushed on its way.
    std::size_t pushedBodyCount = 0U;

    [[nodiscard]] bool IsSteppedUVE() const noexcept {
        return code == KinematicBodyStepCodeUVE::Moved;
    }
    /// True when the body did not move because it was switched off, as opposed to refused.
    [[nodiscard]] bool IsIdleUVE() const noexcept {
        return code == KinematicBodyStepCodeUVE::Idle;
    }
    [[nodiscard]] bool PushedAnythingUVE() const noexcept {
        return pushedBodyCount > 0U;
    }
};

/// The push policy a kinematic body applies to the rigid bodies it walks into: the same shape the
/// character's own push uses, with the same defaults, so a platform and a character shoving the
/// same crate agree about what "shoving" means.
struct KinematicBodyPushPolicyUVE final {
    bool enabled = true;
    /// How hard the body pushes, relative to how fast it was going into the surface.
    float strength = 1.0F;
    /// The fastest a push may send a body, in metres per second.
    float maximumSpeed = 5.0F;
};

/// The engine's push rule, named so a caller can start from it and change the one number it has an
/// opinion about instead of spelling all three out - and so this stays the single place the rule is
/// written.
[[nodiscard]] KinematicBodyPushPolicyUVE DefaultKinematicBodyPushPolicyUVE() noexcept;

/// How much of the remaining velocity gap the body closes in one second, from the authored
/// `interpolation`. 1 is the whole gap - the body is at its target speed immediately, which is
/// every body that does not ask for anything else - and 0.5 closes half of what is left over a
/// second of easing. Kept as a share per second rather than per step so a lift eases the same way
/// at 30 Hz and at 240 Hz.
///
/// A value the mover cannot honour - negative, or not a number at all - blends nothing, so the body
/// does not move on the strength of an authored value nobody meant.
[[nodiscard]] float KinematicEaseBlendUVE(float interpolation, float deltaTimeSeconds) noexcept;

/// Runs one fixed-step move for `entity`:
///
///   1. refuses a body it cannot drive (see the step codes above) rather than half-moving one;
///   2. eases the body's velocity toward the authored `targetVelocity`;
///   3. moves it by that velocity through the world with the engine's kinematic move, pushing the
///      rigid bodies it walks into;
///   4. writes the velocity that actually happened back to the body.
///
/// The body's own component is never modified beyond that velocity; the authored target, the
/// interpolation and the active flag are the author's, not the mover's.
///
/// `pushPolicy` is how hard the body shoves what it walks into. It defaults to the engine's own
/// policy - the same one a character's push uses - so a caller that does not care gets the house
/// rule, and a caller with a reason gets the last word.
[[nodiscard]] KinematicBodyStepResultUVE StepKinematicBodyUVE(
    Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
    ICollisionSystemUVE& collisionSystem, Scene::EntityUVE entity, float deltaTimeSeconds,
    const KinematicBodyPushPolicyUVE& pushPolicy = {});

} // namespace UVE::Physics
