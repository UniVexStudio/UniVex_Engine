// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/collider_debug_scene_uve.h"

#include <cmath>

#include "uve/component/collider_component_uve.h"
#include "uve/component/disabled_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"

namespace UVE::Scene {

ColliderDebugSceneStatsUVE BuildColliderDebugSceneUVE(IEntityManagerUVE& entityManager,
                                                      const ColliderDebugSceneSettingsUVE& settings,
                                                      const std::span<Physics::ColliderDebugLineUVE> outLines) {
    ColliderDebugSceneStatsUVE stats;
    if (outLines.empty() || settings.maximumColliders == 0U) {
        stats.truncated = settings.maximumColliders == 0U;
        stats.reachedColliderLimit = settings.maximumColliders == 0U;
        return stats;
    }

    entityManager.ForEachUVE<WorldTransformComponentUVE, ColliderComponentUVE>(
        [&](const EntityUVE entity, const WorldTransformComponentUVE& worldTransform,
            const ColliderComponentUVE& collider) {
            if (stats.truncated) return;
            if (entity == settings.excludedEntity) return;
            if (stats.colliderCount >= settings.maximumColliders) {
                stats.reachedColliderLimit = true;
                stats.truncated = true;
                return;
            }
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) return;

            // The collider's own switch, and then the object's Process mode: `includeDisabled`
            // speaks for the first only. An object taken out of the physics world contributes no
            // contacts at all, so drawing it would be a lie the author cannot see.
            const bool participating = Collider3DUVE::IsParticipatingUVE(collider);
            if (!participating && !settings.includeDisabled) return;
            if (participating && !IsPhysicsObjectInWorldUVE(entityManager, entity)) return;
            if (settings.layerMask != 0U && (collider.collisionLayer & settings.layerMask) == 0U) return;

            // Whole colliders only: a shape whose tessellation does not fit is skipped rather than
            // half-drawn, so `truncated` always means "some shapes are missing", never "one of them
            // is wrong".
            const std::size_t capacity = Physics::ComputeColliderDebugLineCapacityUVE(collider, settings.style);
            if (capacity == 0U) return;
            if (stats.lineCount + capacity > outLines.size()) {
                stats.truncated = true;
                return;
            }

            const std::span<Physics::ColliderDebugLineUVE> remaining = outLines.subspan(stats.lineCount);
            Physics::ColliderDebugBuildResultUVE built = Physics::BuildColliderDebugLinesUVE(
                collider, worldTransform.worldPosition, worldTransform.worldRotation, remaining,
                settings.style, worldTransform.worldScale);
            if (built.lineCount == 0U) return;
            stats.lineCount += built.lineCount;
            ++stats.colliderCount;
            if (built.truncated) stats.truncated = true;
        });
    return stats;
}

} // namespace UVE::Scene
