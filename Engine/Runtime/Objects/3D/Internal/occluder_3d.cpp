// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "occluder_3d.h"

#include <algorithm>
#include <cmath>

#include "editor_internal_entity_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsOccluder3DObjectComponentValid(const Occluder3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F &&
           value.mode == Occluder3DObjectMode::ConservativeBox;
}

void ApplyOccluder3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const Occluder3DObjectDefinition& value) {
    ApplyObject3DRecipe(entityManager, entity, Occluder3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<Occluder3DComponent>(entity)) {
        entityManager.AddComponent<Occluder3DComponent>(entity, value.occluder);
    }
}

bool ResolveOccluder3DFullyHidden(const Occluder3DComponent& config,
                                     const Math::Vector3& occluderWorldPosition,
                                     const Math::Vector3& viewerWorldPosition,
                                     const Math::Vector3& pointWorld) noexcept {
    if (!IsOccluder3DObjectComponentValid(config)) {
        return false;
    }
    const auto containsPoint = [&config](const Math::Vector3& center,
                                         const Math::Vector3& point) noexcept {
        return std::abs(point.x - center.x) <= config.halfExtents.x &&
               std::abs(point.y - center.y) <= config.halfExtents.y &&
               std::abs(point.z - center.z) <= config.halfExtents.z;
    };
    if (!std::isfinite(viewerWorldPosition.x) || !std::isfinite(viewerWorldPosition.y) ||
        !std::isfinite(viewerWorldPosition.z)) {
        return false;
    }
    if (!std::isfinite(pointWorld.x) || !std::isfinite(pointWorld.y) ||
        !std::isfinite(pointWorld.z)) {
        return false;
    }
    if (containsPoint(occluderWorldPosition, viewerWorldPosition) ||
        containsPoint(occluderWorldPosition, pointWorld)) {
        return false;
    }

    const float directions[3U] = {pointWorld.x - viewerWorldPosition.x,
                                  pointWorld.y - viewerWorldPosition.y,
                                  pointWorld.z - viewerWorldPosition.z};
    const float viewerAxes[3U] = {viewerWorldPosition.x, viewerWorldPosition.y,
                                  viewerWorldPosition.z};
    const float lowerAxes[3U] = {occluderWorldPosition.x - config.halfExtents.x,
                                 occluderWorldPosition.y - config.halfExtents.y,
                                 occluderWorldPosition.z - config.halfExtents.z};
    const float upperAxes[3U] = {occluderWorldPosition.x + config.halfExtents.x,
                                 occluderWorldPosition.y + config.halfExtents.y,
                                 occluderWorldPosition.z + config.halfExtents.z};
    if (!std::isfinite(directions[0U]) || !std::isfinite(directions[1U]) ||
        !std::isfinite(directions[2U])) {
        return false;
    }
    float tEnter = 0.0F;
    float tExit = 1.0F;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const float direction = directions[axis];
        const float viewer = viewerAxes[axis];
        const float lower = lowerAxes[axis];
        const float upper = upperAxes[axis];
        if (direction == 0.0F) {
            if (viewer <= lower || viewer >= upper) {
                return false;
            }
            continue;
        }
        float near = (lower - viewer) / direction;
        float far = (upper - viewer) / direction;
        if (near > far) {
            std::swap(near, far);
        }
        tEnter = std::max(tEnter, near);
        tExit = std::min(tExit, far);
        if (tEnter >= tExit) {
            return false;
        }
    }
    return tEnter < tExit;
}

bool ResolveOccluder3DFullyHidesAabb(const Occluder3DComponent& config,
                                        const Math::Vector3& occluderWorldPosition,
                                        const Math::Vector3& viewerWorldPosition,
                                        const Math::Aabb& bounds) noexcept {
    if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) || !std::isfinite(bounds.min.z) ||
        !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y) || !std::isfinite(bounds.max.z) ||
        bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z) {
        return false;
    }
    const Math::Vector3 corners[8U] = {
        {bounds.min.x, bounds.min.y, bounds.min.z}, {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.min.z}, {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.min.z}, {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.min.z}, {bounds.max.x, bounds.max.y, bounds.max.z},
    };
    for (const Math::Vector3& corner : corners) {
        if (!ResolveOccluder3DFullyHidden(config, occluderWorldPosition, viewerWorldPosition, corner)) {
            return false;
        }
    }
    return true;
}

void CollectOccluder3DSnapshots(IEntityManager& entityManager, std::vector<Occluder3DSnapshot>& out) {
    entityManager.ForEach<WorldTransformComponent, Occluder3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const Occluder3DComponent& config) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty ||
                !config.enabled || !IsOccluder3DObjectComponentValid(config)) {
                return;
            }
            out.push_back(Occluder3DSnapshot{config, world.worldPosition});
        });
}

bool IsOccluder3DPointDrawHidden(const std::span<const Occluder3DSnapshot> occluders,
                                    const Math::Vector3& viewerWorldPosition,
                                    const Math::Vector3& pointWorld) noexcept {
    for (const Occluder3DSnapshot& occluder : occluders) {
        if (ResolveOccluder3DFullyHidden(occluder.config, occluder.worldPosition, viewerWorldPosition,
                                            pointWorld)) {
            return true;
        }
    }
    return false;
}

bool IsOccluder3DAabbDrawHidden(const std::span<const Occluder3DSnapshot> occluders,
                                   const Math::Vector3& viewerWorldPosition,
                                   const Math::Aabb& bounds) noexcept {
    for (const Occluder3DSnapshot& occluder : occluders) {
        if (ResolveOccluder3DFullyHidesAabb(occluder.config, occluder.worldPosition, viewerWorldPosition,
                                               bounds)) {
            return true;
        }
    }
    return false;
}

void CollectOccluder3DGizmos(IEntityManager& entityManager, std::vector<Occluder3DGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, Occluder3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const Occluder3DComponent& config) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty ||
                !IsOccluder3DObjectComponentValid(config)) {
                return;
            }
            Occluder3DGizmo gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.halfExtents = config.halfExtents;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3{0.95F, 0.45F, 0.28F}
                                         : Math::Vector3{0.50F, 0.46F, 0.42F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
