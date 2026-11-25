// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "kinematic_body.h"

#include <cmath>

#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "vector3.h"
#include "abstract_physics_objects_3d.h"
#include "collider_3d.h"
#include "kinematic_3d.h"
#include "character_controller.h"

namespace UVE::Physics {
namespace {

/// A step shorter than this is not a duration at all: it would divide a velocity by nothing and
/// report a body moving at the speed of light. Refused, the way every other mover in the engine
/// refuses a delta time it cannot honour.
constexpr float kMinimumStepDeltaSeconds = 1.0e-6F;

[[nodiscard]] bool IsUsable(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Leaves the body's own velocity at zero. Called for every body that is not moving this step -
/// switched off, stopped, or refused before the move - so nothing can ride a platform that the
/// mover is not driving, off the strength of a velocity the body is no longer using.
void ClearBodyVelocity(Scene::IEntityManager& entityManager, const Scene::Entity entity) {
    if (!entityManager.HasComponent<Scene::Rigid3DComponent>(entity)) {
        return;
    }
    Scene::Rigid3DComponent& body = entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
    if (!Scene::IsRigid3DComponentValid(body)) {
        return;
    }
    body.velocity = Math::Vector3{};
}

/// The mover's codes, translated from the kinematic move's own. The move validates the same
/// preconditions this step already checked, so a failure this late means the world changed under
/// the body - a component removed mid-step, a transform made non-finite - and the honest answer is
/// that the world could not describe the body.
[[nodiscard]] KinematicBodyStepCode TranslateMoveCode(const CharacterControllerMoveCode code) noexcept {
    switch (code) {
        case CharacterControllerMoveCode::InvalidEntity:
            return KinematicBodyStepCode::UnknownEntity;
        case CharacterControllerMoveCode::MissingTransform:
            return KinematicBodyStepCode::MissingTransform;
        case CharacterControllerMoveCode::MissingCollider:
            return KinematicBodyStepCode::MissingCollider;
        case CharacterControllerMoveCode::NonKinematicBody:
            return KinematicBodyStepCode::NonKinematicBody;
        case CharacterControllerMoveCode::InvalidInput:
        case CharacterControllerMoveCode::Moved:
        default:
            return KinematicBodyStepCode::InvalidWorld;
    }
}

} // namespace

float KinematicEaseBlend(const float interpolation, const float deltaTimeSeconds) noexcept {
    return Scene::Kinematic3D::EaseBlend(interpolation, deltaTimeSeconds);
}

KinematicBodyPushPolicy DefaultKinematicBodyPushPolicy() noexcept {
    return KinematicBodyPushPolicy{};
}

KinematicBodyStepResult StepKinematicBody(Scene::IEntityManager& entityManager,
                                                Scene::ISceneGraph& sceneGraph,
                                                ICollisionSystem& collisionSystem,
                                                const Scene::Entity entity,
                                                const float deltaTimeSeconds,
                                                const KinematicBodyPushPolicy& pushPolicy) {
    KinematicBodyStepResult result;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= kMinimumStepDeltaSeconds) {
        result.code = KinematicBodyStepCode::InvalidDeltaTime;
        return result;
    }
    if (!entityManager.IsAlive(entity)) {
        result.code = KinematicBodyStepCode::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponent<Scene::Kinematic3DComponent>(entity)) {
        result.code = KinematicBodyStepCode::NotAKinematicBody;
        return result;
    }
    const Scene::Kinematic3DComponent& kinematic =
        entityManager.GetComponent<Scene::Kinematic3DComponent>(entity);
    if (!entityManager.HasComponent<Scene::TransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        result.code = KinematicBodyStepCode::MissingTransform;
        return result;
    }
    if (!entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
        result.code = KinematicBodyStepCode::MissingCollider;
        return result;
    }
    // A kinematic body is one the simulation never integrates and never deflects: it goes where it
    // is driven. A dynamic body here would fight the mover for the transform, which is why the
    // recipe authors it kinematic and this refuses the combination rather than picking a winner.
    if (entityManager.HasComponent<Scene::Rigid3DComponent>(entity)) {
        const Scene::Rigid3DComponent& body =
            entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
        if (!Scene::IsRigid3DComponentValid(body) || !body.isKinematic) {
            result.code = KinematicBodyStepCode::NonKinematicBody;
            return result;
        }
    } else {
        result.code = KinematicBodyStepCode::NonKinematicBody;
        return result;
    }
    {
        const Scene::ColliderComponent& collider =
            entityManager.GetComponent<Scene::ColliderComponent>(entity);
        const Math::Vector3 halfExtents = Scene::GetColliderLocalHalfExtents(collider);
        if (!Scene::Collider3D::IsParticipating(collider) || !IsUsable(halfExtents) ||
            halfExtents.x <= 0.0F || halfExtents.y <= 0.0F || halfExtents.z <= 0.0F) {
            result.code = KinematicBodyStepCode::InvalidWorld;
            return result;
        }
    }

    result.targetVelocity = IsUsable(kinematic.targetVelocity) ? kinematic.targetVelocity
                                                                  : Math::Vector3{};

    if (!Scene::Kinematic3D::IsDriving(kinematic) ||
        !Scene::IsPhysicsObjectSimulated(entityManager, entity)) {
        ClearBodyVelocity(entityManager, entity);
        result.code = KinematicBodyStepCode::Idle;
        return result;
    }

    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    const Math::Vector3 worldTarget =
        Scene::Kinematic3D::ResolveWorldTarget(result.targetVelocity, worldTransform.worldRotation);

    Scene::Rigid3DComponent& body = entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
    Math::Vector3 velocity = Scene::Kinematic3D::EaseVelocity(
        body.velocity, worldTarget, kinematic.interpolation, deltaTimeSeconds);
    result.velocity = velocity;

    // ---- Move, through the world ---------------------------------------------------------------
    CharacterControllerInput input;
    input.entity = entity;
    input.desiredDisplacement = velocity * deltaTimeSeconds;
    input.pushDynamicBodies = pushPolicy.enabled;
    input.dynamicBodyPushStrength = pushPolicy.strength;
    input.maximumDynamicBodyPushSpeed = pushPolicy.maximumSpeed;
    input.dynamicBodyPushDeltaTimeSeconds = deltaTimeSeconds;
    const CharacterControllerMoveResult move =
        CharacterController::MoveWithToI(entityManager, sceneGraph, collisionSystem, input);
    if (!move.IsAccepted()) {
        const KinematicBodyStepCode code = TranslateMoveCode(move.code);
        ClearBodyVelocity(entityManager, entity);
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
    const Math::Vector3 actualVelocity = result.appliedMotion * (1.0F / deltaTimeSeconds);
    body.velocity = IsUsable(actualVelocity) ? actualVelocity : Math::Vector3{};
    result.velocity = body.velocity;
    result.code = KinematicBodyStepCode::Moved;
    return result;
}

} // namespace UVE::Physics
