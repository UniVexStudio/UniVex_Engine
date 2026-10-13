// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/camera_follow_uve.h"

#include <vector>

#include "uve/component/camera_follow_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/gameplay/pawn_uve.h"

namespace UVE::Scene {

namespace {

template <typename TComponent>
[[nodiscard]] bool HasLiveFollowComponentUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    return entityManager.IsAliveUVE(entity) && entityManager.HasComponentUVE<TComponent>(entity);
}

[[nodiscard]] bool IsPlayerControllerUVE(const IEntityManagerUVE& entityManager, const EntityUVE controller) {
    return HasLiveFollowComponentUVE<ControllerComponentUVE>(entityManager, controller) &&
           entityManager.GetComponentUVE<ControllerComponentUVE>(controller).kind == ControllerKindUVE::Player;
}

} // namespace

void UpdateCameraFollowUVE(IEntityManagerUVE& entityManager) {
    std::vector<EntityUVE> followers;
    entityManager.ForEachUVE<CameraFollowComponentUVE>(
        [&followers](const EntityUVE entity, const CameraFollowComponentUVE&) { followers.push_back(entity); });
    // Collected first, written after: the pass mutates transforms, never the follow set.
    for (const EntityUVE camera : followers) {
        if (!HasLiveFollowComponentUVE<TransformComponentUVE>(entityManager, camera) ||
            !HasLiveFollowComponentUVE<WorldTransformComponentUVE>(entityManager, camera) ||
            !HasLiveFollowComponentUVE<HierarchyComponentUVE>(entityManager, camera)) {
            continue;
        }
        if (entityManager.GetComponentUVE<HierarchyComponentUVE>(camera).parent != kInvalidEntityUVE) {
            continue; // rig-driven: a spring arm, vehicle, or bone owns this camera's pose
        }
        const EntityUVE target =
            entityManager.GetComponentUVE<CameraFollowComponentUVE>(camera).target;
        if (!HasLiveFollowComponentUVE<WorldTransformComponentUVE>(entityManager, target)) {
            continue; // parked, destroyed, or never given a world pose: hold the last one
        }
        const Math::Vector3UVE targetPosition =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(target).worldPosition;
        const Math::Vector3UVE offset =
            entityManager.GetComponentUVE<CameraFollowComponentUVE>(camera).offset;
        TransformComponentUVE pose = entityManager.GetComponentUVE<TransformComponentUVE>(camera);
        pose.localPosition = targetPosition + offset;
        if (!IsTransformComponentValidUVE(pose)) {
            continue; // NaN in, nothing out: a poisoned target must not teleport the camera
        }
        // SetLocalTransformUVE() by hand (uve_gameplay must not depend on uve_scene): overwrite
        // the authored local transform, mark the world transform dirty for this frame's
        // propagation. Rotation and scale pass through untouched - the follow only translates.
        entityManager.GetComponentUVE<TransformComponentUVE>(camera) = pose;
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty = true;
    }
}

void RetargetPossessionFollowersUVE(IEntityManagerUVE& entityManager, const EntityUVE controller,
                                     const EntityUVE pawn) {
    if (!IsPlayerControllerUVE(entityManager, controller)) {
        return;
    }
    std::vector<EntityUVE> followers;
    entityManager.ForEachUVE<CameraFollowComponentUVE>(
        [&followers](const EntityUVE entity, const CameraFollowComponentUVE&) { followers.push_back(entity); });
    for (const EntityUVE camera : followers) {
        CameraFollowComponentUVE& follow = entityManager.GetComponentUVE<CameraFollowComponentUVE>(camera);
        if (follow.followPossessedPawn) {
            follow.target = pawn;
        }
    }
}

void ReleasePossessionFollowersUVE(IEntityManagerUVE& entityManager, const EntityUVE controller,
                                    const EntityUVE pawn) {
    if (!IsPlayerControllerUVE(entityManager, controller)) {
        return;
    }
    std::vector<EntityUVE> followers;
    entityManager.ForEachUVE<CameraFollowComponentUVE>(
        [&followers](const EntityUVE entity, const CameraFollowComponentUVE&) { followers.push_back(entity); });
    for (const EntityUVE camera : followers) {
        CameraFollowComponentUVE& follow = entityManager.GetComponentUVE<CameraFollowComponentUVE>(camera);
        if (follow.followPossessedPawn && follow.target == pawn) {
            follow.target = kInvalidEntityUVE;
        }
    }
}

} // namespace UVE::Scene
