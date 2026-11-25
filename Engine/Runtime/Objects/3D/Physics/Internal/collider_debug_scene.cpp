// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_debug_scene.h"

#include <cmath>

#include "collider_component.h"
#include "disabled_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "abstract_physics_objects_3d.h"
#include "collider_3d.h"

namespace UVE::Scene {

ColliderDebugSceneStats BuildColliderDebugScene(IEntityManager& entityManager,
                                                      const ColliderDebugSceneSettings& settings,
                                                      const std::span<Physics::ColliderDebugLine> outLines) {
    ColliderDebugSceneStats stats;
    if (outLines.empty() || settings.maximumColliders == 0U) {
        stats.truncated = settings.maximumColliders == 0U;
        stats.reachedColliderLimit = settings.maximumColliders == 0U;
        return stats;
    }

    entityManager.ForEach<WorldTransformComponent, ColliderComponent>(
        [&](const Entity entity, const WorldTransformComponent& worldTransform,
            const ColliderComponent& collider) {
            if (stats.truncated) return;
            if (entity == settings.excludedEntity) return;
            if (stats.colliderCount >= settings.maximumColliders) {
                stats.reachedColliderLimit = true;
                stats.truncated = true;
                return;
            }
            if (entityManager.HasComponent<DisabledComponent>(entity)) return;

            // The collider's own switch, and then the object's Process mode: `includeDisabled`
            // speaks for the first only. An object taken out of the physics world contributes no
            // contacts at all, so drawing it would be a lie the author cannot see.
            const bool participating = Collider3D::IsParticipating(collider);
            if (!participating && !settings.includeDisabled) return;
            if (participating && !IsPhysicsObjectInWorld(entityManager, entity)) return;
            if (settings.layerMask != 0U && (collider.collisionLayer & settings.layerMask) == 0U) return;

            // Whole colliders only: a shape whose tessellation does not fit is skipped rather than
            // half-drawn, so `truncated` always means "some shapes are missing", never "one of them
            // is wrong".
            const std::size_t capacity = Physics::ComputeColliderDebugLineCapacity(collider, settings.style);
            if (capacity == 0U) return;
            if (stats.lineCount + capacity > outLines.size()) {
                stats.truncated = true;
                return;
            }

            const std::span<Physics::ColliderDebugLine> remaining = outLines.subspan(stats.lineCount);
            Physics::ColliderDebugBuildResult built = Physics::BuildColliderDebugLines(
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
