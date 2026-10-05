// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/spring_arm_uve.h"

#include <cmath>
#include <optional>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"
#include "uve/physics/i_raycast_system_uve.h"

namespace UVE::Physics {
namespace {

/// The arm's own axis in its local space: +Z, behind the pivot. Named because it is the one
/// convention everything here and the component's own documentation share.
constexpr Math::Vector3UVE kArmAxisUVE{0.0F, 0.0F, 1.0F};

[[nodiscard]] bool IsUsableUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// Shifts every direct child by `lengthDelta` along the arm's local Z.
///
/// A child's own transform is skipped rather than refused: an arm is allowed to have children that
/// are not scene-graph nodes with a pose of their own (a marker, a sound), and those simply do not
/// ride the arm.
[[nodiscard]] std::size_t RideChildrenUVE(Scene::IEntityManagerUVE& entityManager,
                                          Scene::ISceneGraphUVE& sceneGraph,
                                          const Scene::EntityUVE entity, const float lengthDelta) {
    std::size_t moved = 0U;
    for (const Scene::EntityUVE child : sceneGraph.GetChildrenUVE(entityManager, entity)) {
        if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(child)) {
            continue;
        }
        Scene::TransformComponentUVE childTransform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child);
        childTransform.localPosition.z += lengthDelta;
        sceneGraph.SetLocalTransformUVE(entityManager, child, childTransform);
        ++moved;
    }
    return moved;
}

} // namespace

SpringArm3DStepResultUVE StepSpringArm3DUVE(Scene::IEntityManagerUVE& entityManager,
                                           Scene::ISceneGraphUVE& sceneGraph,
                                           const IRaycastSystemUVE& raycastSystem,
                                           const Scene::EntityUVE entity,
                                           const float deltaTimeSeconds) {
    SpringArm3DStepResultUVE result;
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        result.code = SpringArm3DStepCodeUVE::InvalidDeltaTime;
        return result;
    }
    if (!entityManager.IsAliveUVE(entity)) {
        result.code = SpringArm3DStepCodeUVE::UnknownEntity;
        return result;
    }
    if (!entityManager.HasComponentUVE<Scene::SpringArm3DComponentUVE>(entity)) {
        result.code = SpringArm3DStepCodeUVE::NotASpringArm;
        return result;
    }
    if (!entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        result.code = SpringArm3DStepCodeUVE::MissingTransform;
        return result;
    }

    const Scene::SpringArm3DComponentUVE& springArm =
        entityManager.GetComponentUVE<Scene::SpringArm3DComponentUVE>(entity);
    result.armLength = springArm.armLength;
    result.previousLength = springArm.currentLength;
    result.currentLength = springArm.currentLength;
    // A length outside the arm's own envelope is a malformed component, and the engine refuses to
    // act on one rather than latching a camera to a number nobody could have meant - the Inspector
    // and the scene loader both stop it before it gets this far.
    if (!Scene::IsSpringArm3DObjectComponentValidUVE(springArm)) {
        result.code = SpringArm3DStepCodeUVE::InvalidComponent;
        return result;
    }

    // ---- Where is the arm pointing, and what is in the way? --------------------------------------
    // Switched off, the arm casts nothing and aims home: the child it carries has to be able to
    // come back, or disabling an arm would strand a camera in a wall forever.
    if (springArm.enabled) {
        const Scene::WorldTransformComponentUVE& worldTransform =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        // A degenerate rotation would send the arm along a garbage axis; the hitbox sync reads the
        // same situation the same way, falling back to the identity rather than refusing a body
        // whose pose the scene graph could not resolve.
        Math::QuaternionUVE rotation{};
        if (!Math::TryNormalizeUVE(worldTransform.worldRotation, rotation)) {
            rotation = {};
        }
        const Math::Vector3UVE axis = Math::RotateVectorUVE(rotation, kArmAxisUVE);
        if (!IsUsableUVE(worldTransform.worldPosition) || !IsUsableUVE(axis)) {
            result.code = SpringArm3DStepCodeUVE::InvalidComponent;
            return result;
        }

        RaycastQueryUVE query{};
        query.ray.origin = worldTransform.worldPosition;
        query.ray.direction = axis;
        query.maxDistance = springArm.armLength;
        query.layerMask = springArm.collisionMask;
        query.ignoreEntity = entity;

        const std::optional<RaycastHitUVE> hit = raycastSystem.RaycastUVE(entityManager, query);
        result.castRan = true;
        result.hasHit = hit.has_value();
        if (hit.has_value()) {
            result.hitEntity = hit->entity;
            result.hitPoint = hit->point;
            result.hitDistance = hit->distance;
        }
        result.targetLength = Scene::ResolveSpringArm3DTargetUVE(
            hit.has_value() ? std::optional<float>{hit->distance} : std::nullopt, springArm.margin,
            springArm.armLength);
        result.code = SpringArm3DStepCodeUVE::Stepped;
    } else {
        result.targetLength = springArm.armLength;
        result.code = SpringArm3DStepCodeUVE::Disabled;
    }

    // ---- The motion law, then the children that ride it ------------------------------------------
    const float resolvedLength = Scene::ResolveSpringArm3DLengthUVE(
        springArm.currentLength, result.targetLength, springArm.smoothing, deltaTimeSeconds);
    result.currentLength = resolvedLength;
    result.lengthDelta = resolvedLength - result.previousLength;
    entityManager.GetComponentUVE<Scene::SpringArm3DComponentUVE>(entity).currentLength = resolvedLength;
    if (result.lengthDelta != 0.0F) {
        result.movedChildCount = RideChildrenUVE(entityManager, sceneGraph, entity, result.lengthDelta);
    }
    return result;
}

} // namespace UVE::Physics
