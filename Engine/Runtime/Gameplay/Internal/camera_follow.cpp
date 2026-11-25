// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_follow.h"

#include <vector>

#include "camera_follow_component.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "pawn.h"

namespace UVE::Scene {

namespace {

template <typename TComponent>
[[nodiscard]] bool HasLiveFollowComponent(const IEntityManager& entityManager, const Entity entity) {
    return entityManager.IsAlive(entity) && entityManager.HasComponent<TComponent>(entity);
}

[[nodiscard]] bool IsPlayerController(const IEntityManager& entityManager, const Entity controller) {
    return HasLiveFollowComponent<ControllerComponent>(entityManager, controller) &&
           entityManager.GetComponent<ControllerComponent>(controller).kind == ControllerKind::Player;
}

} // namespace

void UpdateCameraFollow(IEntityManager& entityManager) {
    std::vector<Entity> followers;
    entityManager.ForEach<CameraFollowComponent>(
        [&followers](const Entity entity, const CameraFollowComponent&) { followers.push_back(entity); });
    // Collected first, written after: the pass mutates transforms, never the follow set.
    for (const Entity camera : followers) {
        if (!HasLiveFollowComponent<TransformComponent>(entityManager, camera) ||
            !HasLiveFollowComponent<WorldTransformComponent>(entityManager, camera) ||
            !HasLiveFollowComponent<HierarchyComponent>(entityManager, camera)) {
            continue;
        }
        if (entityManager.GetComponent<HierarchyComponent>(camera).parent != kInvalidEntity) {
            continue; // rig-driven: a spring arm, vehicle, or bone owns this camera's pose
        }
        const CameraFollowComponent& follow =
            entityManager.GetComponent<CameraFollowComponent>(camera);
        if (!HasCameraFollowTarget(follow, camera) ||
            !HasLiveFollowComponent<WorldTransformComponent>(entityManager, follow.target) ||
            !IsCameraFollowComponentValid(follow)) {
            continue; // parked, destroyed, or never given a world pose: hold the last one
        }
        const Math::Vector3 targetPosition =
            entityManager.GetComponent<WorldTransformComponent>(follow.target).worldPosition;
        if (!Math::IsFinite(targetPosition)) {
            continue;
        }
        TransformComponent pose = entityManager.GetComponent<TransformComponent>(camera);
        pose.localPosition = ResolveCameraFollowDesiredPosition(follow, targetPosition);
        if (!IsTransformComponentValid(pose)) {
            continue; // NaN in, nothing out: a poisoned target must not teleport the camera
        }
        // SetLocalTransform() by hand (uve_gameplay must not depend on uve_scene): overwrite
        // the authored local transform, mark the world transform dirty for this frame's
        // propagation. Rotation and scale pass through untouched - the follow only translates.
        entityManager.GetComponent<TransformComponent>(camera) = pose;
        entityManager.GetComponent<WorldTransformComponent>(camera).dirty = true;
    }
}

void RetargetPossessionFollowers(IEntityManager& entityManager, const Entity controller,
                                     const Entity pawn) {
    if (!IsPlayerController(entityManager, controller)) {
        return;
    }
    std::vector<Entity> followers;
    entityManager.ForEach<CameraFollowComponent>(
        [&followers](const Entity entity, const CameraFollowComponent&) { followers.push_back(entity); });
    for (const Entity camera : followers) {
        CameraFollowComponent& follow = entityManager.GetComponent<CameraFollowComponent>(camera);
        ApplyPlayerPossessionToCameraFollow(follow, pawn);
    }
}

void ReleasePossessionFollowers(IEntityManager& entityManager, const Entity controller,
                                    const Entity pawn) {
    if (!IsPlayerController(entityManager, controller)) {
        return;
    }
    std::vector<Entity> followers;
    entityManager.ForEach<CameraFollowComponent>(
        [&followers](const Entity entity, const CameraFollowComponent&) { followers.push_back(entity); });
    for (const Entity camera : followers) {
        CameraFollowComponent& follow = entityManager.GetComponent<CameraFollowComponent>(camera);
        if (follow.followPossessedPawn && follow.target == pawn) {
            follow.target = kInvalidEntity;
        }
    }
}

} // namespace UVE::Scene
