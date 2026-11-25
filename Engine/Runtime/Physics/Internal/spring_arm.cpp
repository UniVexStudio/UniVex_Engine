// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spring_arm.h"

#include <cmath>
#include <optional>

#include "collider_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "quaternion.h"
#include "spring_arm_3d.h"
#include "i_raycast_system.h"

namespace UVE::Physics {
namespace {

/// The arm's own axis in its local space: +Z, behind the pivot. Named because it is the one
/// convention everything here and the component's own documentation share.
[[nodiscard]] bool IsUsable(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Shifts every direct child by `lengthDelta` along the arm's local Z.
///
/// A child's own transform is skipped rather than refused: an arm is allowed to have children that
/// are not scene-graph nodes with a pose of their own (a marker, a sound), and those simply do not
/// ride the arm.
[[nodiscard]] std::size_t RideChildren(Scene::IEntityManager& entityManager,
                                          Scene::ISceneGraph& sceneGraph,
                                          const Scene::Entity entity, const float lengthDelta) {
    std::size_t moved = 0U;
    for (const Scene::Entity child : sceneGraph.GetChildren(entityManager, entity)) {
        if (!entityManager.HasComponent<Scene::TransformComponent>(child)) {
            continue;
        }
        Scene::TransformComponent childTransform =
            entityManager.GetComponent<Scene::TransformComponent>(child);
        childTransform.localPosition.z += lengthDelta;
        sceneGraph.SetLocalTransform(entityManager, child, childTransform);
        ++moved;
    }
    return moved;
}

} // namespace

SpringArm3DStepResult StepSpringArm3D(Scene::IEntityManager& entityManager,
                                           Scene::ISceneGraph& sceneGraph,
                                           const IRaycastSystem& raycastSystem,
                                           const Scene::Entity entity,
                                           const float deltaTimeSeconds) {
    SpringArm3DStepResult result;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        result.code = SpringArm3DStepCode::InvalidDeltaTime;
        return result;
    }
    if (!entityManager.IsAlive(entity)) {
        result.code = SpringArm3DStepCode::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponent<Scene::SpringArm3DComponent>(entity)) {
        result.code = SpringArm3DStepCode::NotASpringArm;
        return result;
    }
    if (!entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        result.code = SpringArm3DStepCode::MissingTransform;
        return result;
    }

    const Scene::SpringArm3DComponent& springArm =
        entityManager.GetComponent<Scene::SpringArm3DComponent>(entity);
    result.armLength = springArm.armLength;
    result.previousLength = springArm.currentLength;
    result.currentLength = springArm.currentLength;
    // A length outside the arm's own envelope is a malformed component, and the engine refuses to
    // act on one rather than latching a camera to a number nobody could have meant - the Inspector
    // and the scene loader both stop it before it gets this far.
    if (!Scene::IsSpringArm3DObjectComponentValid(springArm)) {
        result.code = SpringArm3DStepCode::InvalidComponent;
        return result;
    }

    // ---- Where is the arm pointing, and what is in the way? --------------------------------------
    // Switched off, the arm casts nothing and aims home: the child it carries has to be able to
    // come back, or disabling an arm would strand a camera in a wall forever.
    if (Scene::SpringArm3D::IsCasting(springArm)) {
        const Scene::WorldTransformComponent& worldTransform =
            entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
        const Math::Vector3 axis = Scene::SpringArm3D::ResolveWorldAxis(worldTransform.worldRotation);
        if (!IsUsable(worldTransform.worldPosition) || !IsUsable(axis)) {
            result.code = SpringArm3DStepCode::InvalidComponent;
            return result;
        }

        RaycastQuery query{};
        query.ray.origin = worldTransform.worldPosition;
        query.ray.direction = axis;
        query.maxDistance = springArm.armLength;
        query.layerMask = springArm.collisionMask;
        query.ignoreEntity = entity;

        const std::optional<RaycastHit> hit = raycastSystem.Raycast(entityManager, query);
        result.castRan = true;
        result.hasHit = hit.has_value();
        if (hit.has_value()) {
            result.hitEntity = hit->entity;
            result.hitPoint = hit->point;
            result.hitDistance = hit->distance;
        }
        result.targetLength = Scene::SpringArm3D::ResolveTarget(
            hit.has_value() ? std::optional<float>{hit->distance} : std::nullopt, springArm.margin,
            springArm.armLength);
        result.code = SpringArm3DStepCode::Stepped;
    } else {
        result.targetLength = springArm.armLength;
        result.code = SpringArm3DStepCode::Disabled;
    }

    const float resolvedLength = Scene::SpringArm3D::ResolveLength(
        springArm.currentLength, result.targetLength, springArm.smoothing, deltaTimeSeconds);
    result.currentLength = resolvedLength;
    result.lengthDelta = resolvedLength - result.previousLength;
    entityManager.GetComponent<Scene::SpringArm3DComponent>(entity).currentLength = resolvedLength;
    if (result.lengthDelta != 0.0F) {
        result.movedChildCount = RideChildren(entityManager, sceneGraph, entity, result.lengthDelta);
    }
    return result;
}

} // namespace UVE::Physics
