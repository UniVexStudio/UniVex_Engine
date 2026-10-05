// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/kinematic_body_uve.h"

#include <cmath>

#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/kinematic_3d_uve.h"
#include "uve/physics/character_controller_uve.h"

namespace UVE::Physics {
namespace {

/// A step shorter than this is not a duration at all: it would divide a velocity by nothing and
/// report a body moving at the speed of light. Refused, the way every other mover in the engine
/// refuses a delta time it cannot honour.
constexpr float kMinimumStepDeltaSecondsUVE = 1.0e-6F;

[[nodiscard]] bool IsUsableUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Leaves the body's own velocity at zero. Called for every body that is not moving this step -
/// switched off, stopped, or refused before the move - so nothing can ride a platform that the
/// mover is not driving, off the strength of a velocity the body is no longer using.
void ClearBodyVelocityUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity) {
    if (!entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
        return;
    }
    Scene::Rigid3DComponentUVE& body = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
    if (!Scene::IsRigid3DComponentValidUVE(body)) {
        return;
    }
    body.velocity = Math::Vector3UVE{};
}

/// The mover's codes, translated from the kinematic move's own. The move validates the same
/// preconditions this step already checked, so a failure this late means the world changed under
/// the body - a component removed mid-step, a transform made non-finite - and the honest answer is
/// that the world could not describe the body.
[[nodiscard]] KinematicBodyStepCodeUVE TranslateMoveCodeUVE(const CharacterControllerMoveCodeUVE code) noexcept {
    switch (code) {
        case CharacterControllerMoveCodeUVE::InvalidEntity:
            return KinematicBodyStepCodeUVE::UnknownEntity;
        case CharacterControllerMoveCodeUVE::MissingTransform:
            return KinematicBodyStepCodeUVE::MissingTransform;
        case CharacterControllerMoveCodeUVE::MissingCollider:
            return KinematicBodyStepCodeUVE::MissingCollider;
        case CharacterControllerMoveCodeUVE::NonKinematicBody:
            return KinematicBodyStepCodeUVE::NonKinematicBody;
        case CharacterControllerMoveCodeUVE::InvalidInput:
        case CharacterControllerMoveCodeUVE::Moved:
        default:
            return KinematicBodyStepCodeUVE::InvalidWorld;
    }
}

} // namespace

float KinematicEaseBlendUVE(const float interpolation, const float deltaTimeSeconds) noexcept {
    // Zero means the gap never closes - the body keeps whatever velocity it already has, which for
    // every body that has not been given one is standing still. Useful as an explicit "do not
    // ease, do not start" without deleting the body, and the only value below 1 that does not have
    // to be a curve.
    if (!std::isfinite(interpolation) || interpolation <= 0.0F) {
        return 0.0F;
    }
    // 1 is the default and the crisp case: the body is at its target speed this step, no curve at
    // all. Checked before the power so the common path never calls into the maths library.
    if (interpolation >= 1.0F) {
        return 1.0F;
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0.0F;
    }
    // A share of the *remaining gap per second*, so the curve is a property of the author's value
    // and not of the frame rate the game happens to run at.
    return 1.0F - std::pow(1.0F - interpolation, deltaTimeSeconds);
}

KinematicBodyPushPolicyUVE DefaultKinematicBodyPushPolicyUVE() noexcept {
    return KinematicBodyPushPolicyUVE{};
}

KinematicBodyStepResultUVE StepKinematicBodyUVE(Scene::IEntityManagerUVE& entityManager,
                                                Scene::ISceneGraphUVE& sceneGraph,
                                                ICollisionSystemUVE& collisionSystem,
                                                const Scene::EntityUVE entity,
                                                const float deltaTimeSeconds,
                                                const KinematicBodyPushPolicyUVE& pushPolicy) {
    KinematicBodyStepResultUVE result;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= kMinimumStepDeltaSecondsUVE) {
        result.code = KinematicBodyStepCodeUVE::InvalidDeltaTime;
        return result;
    }
    if (!entityManager.IsAliveUVE(entity)) {
        result.code = KinematicBodyStepCodeUVE::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponentUVE<Scene::Kinematic3DComponentUVE>(entity)) {
        result.code = KinematicBodyStepCodeUVE::NotAKinematicBody;
        return result;
    }
    const Scene::Kinematic3DComponentUVE& kinematic =
        entityManager.GetComponentUVE<Scene::Kinematic3DComponentUVE>(entity);
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        result.code = KinematicBodyStepCodeUVE::MissingTransform;
        return result;
    }
    if (!entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
        result.code = KinematicBodyStepCodeUVE::MissingCollider;
        return result;
    }
    // A kinematic body is one the simulation never integrates and never deflects: it goes where it
    // is driven. A dynamic body here would fight the mover for the transform, which is why the
    // recipe authors it kinematic and this refuses the combination rather than picking a winner.
    if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity)) {
        const Scene::Rigid3DComponentUVE& body =
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
        if (!Scene::IsRigid3DComponentValidUVE(body) || !body.isKinematic) {
            result.code = KinematicBodyStepCodeUVE::NonKinematicBody;
            return result;
        }
    } else {
        result.code = KinematicBodyStepCodeUVE::NonKinematicBody;
        return result;
    }
    {
        const Scene::ColliderComponentUVE& collider =
            entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity);
        const Math::Vector3UVE halfExtents = Scene::GetColliderLocalHalfExtentsUVE(collider);
        if (!Scene::IsColliderComponentValidUVE(collider) || !IsUsableUVE(halfExtents) ||
            halfExtents.x <= 0.0F || halfExtents.y <= 0.0F || halfExtents.z <= 0.0F) {
            result.code = KinematicBodyStepCodeUVE::InvalidWorld;
            return result;
        }
    }

    // An authored target the mover cannot honour is a body standing still, not a body moving at
    // whatever the nan was in. The component's own validator rejects it on save and on the
    // Inspector; a script can still write one at runtime, and this is where that stops.
    result.targetVelocity = IsUsableUVE(kinematic.targetVelocity) ? kinematic.targetVelocity
                                                                  : Math::Vector3UVE{};

    // ---- Is this body moving at all? ------------------------------------------------------------
    // Two separate switches, read in one place because they mean the same thing to the world: the
    // component's own `active` flag, and the object's participation - a body whose object is
    // stopped and whose disable mode keeps it out of the simulation, or keeps it as an immovable
    // obstacle, does not move. Both leave it exactly where it is.
    if (!kinematic.active || !Scene::IsPhysicsObjectSimulatedUVE(entityManager, entity)) {
        ClearBodyVelocityUVE(entityManager, entity);
        result.code = KinematicBodyStepCodeUVE::Idle;
        return result;
    }

    // ---- Ease toward the target ----------------------------------------------------------------
    Scene::Rigid3DComponentUVE& body = entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity);
    Math::Vector3UVE velocity = IsUsableUVE(body.velocity) ? body.velocity : Math::Vector3UVE{};
    const float blend = KinematicEaseBlendUVE(kinematic.interpolation, deltaTimeSeconds);
    if (blend >= 1.0F) {
        velocity = result.targetVelocity;
    } else if (blend > 0.0F) {
        velocity += (result.targetVelocity - velocity) * blend;
    }
    if (!IsUsableUVE(velocity)) {
        velocity = Math::Vector3UVE{};
    }
    result.velocity = velocity;

    // ---- Move, through the world ---------------------------------------------------------------
    CharacterControllerInputUVE input;
    input.entity = entity;
    input.desiredDisplacement = velocity * deltaTimeSeconds;
    input.pushDynamicBodies = pushPolicy.enabled;
    input.dynamicBodyPushStrength = pushPolicy.strength;
    input.maximumDynamicBodyPushSpeed = pushPolicy.maximumSpeed;
    input.dynamicBodyPushDeltaTimeSeconds = deltaTimeSeconds;
    const CharacterControllerMoveResultUVE move =
        CharacterControllerUVE::MoveWithToIUVE(entityManager, sceneGraph, collisionSystem, input);
    if (!move.IsAcceptedUVE()) {
        const KinematicBodyStepCodeUVE code = TranslateMoveCodeUVE(move.code);
        ClearBodyVelocityUVE(entityManager, entity);
        result.code = code;
        return result;
    }

    result.appliedMotion = move.appliedDisplacement;
    // What the world took away, not what the move still had left to slide: for a body stopped by a
    // wall this is the step's whole request, and for a glancing contact it is the part that was
    // projected off. The move's own remaining displacement is its business (it is zero once the
    // displacement has been projected away, even when the body did not get anywhere).
    result.remainingMotion = move.requestedDisplacement - move.appliedDisplacement;
    result.blocked = move.blocked;
    result.grounded = move.grounded;
    result.groundNormal = move.groundNormal;
    result.contactCount = move.contactCount;
    result.pushedBodyCount = move.pushedBodyCount;

    // ---- Say what actually happened --------------------------------------------------------------
    // The body's velocity is what it did, not what it was told: a platform held up by a wall has no
    // velocity to hand a rider stepping off it, and a script reading the body reads the truth. On
    // the unblocked path this is the eased velocity again, to within a rounding error.
    const Math::Vector3UVE actualVelocity = result.appliedMotion * (1.0F / deltaTimeSeconds);
    body.velocity = IsUsableUVE(actualVelocity) ? actualVelocity : Math::Vector3UVE{};
    result.velocity = body.velocity;
    result.code = KinematicBodyStepCodeUVE::Moved;
    return result;
}

} // namespace UVE::Physics
