// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/occluder_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsOccluder3DObjectComponentValidUVE(const Occluder3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F &&
           value.mode == Occluder3DObjectModeUVE::ConservativeBox;
}

void ApplyOccluder3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const Occluder3DObjectDefinitionUVE& value) {
    ApplyObject3DRecipeUVE(entityManager, entity, Occluder3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Occluder3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Occluder3DComponentUVE>(entity, value.occluder);
    }
}

bool ResolveOccluder3DFullyHiddenUVE(const Occluder3DComponentUVE& config,
                                     const Math::Vector3UVE& occluderWorldPosition,
                                     const Math::Vector3UVE& viewerWorldPosition,
                                     const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsOccluder3DObjectComponentValidUVE(config)) {
        return false;
    }
    const auto containsPoint = [&config](const Math::Vector3UVE& center,
                                         const Math::Vector3UVE& point) noexcept {
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

bool ResolveOccluder3DFullyHidesAabbUVE(const Occluder3DComponentUVE& config,
                                        const Math::Vector3UVE& occluderWorldPosition,
                                        const Math::Vector3UVE& viewerWorldPosition,
                                        const Math::AabbUVE& bounds) noexcept {
    if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) || !std::isfinite(bounds.min.z) ||
        !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y) || !std::isfinite(bounds.max.z) ||
        bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z) {
        return false;
    }
    const Math::Vector3UVE corners[8U] = {
        {bounds.min.x, bounds.min.y, bounds.min.z}, {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.min.z}, {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.min.z}, {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.min.z}, {bounds.max.x, bounds.max.y, bounds.max.z},
    };
    for (const Math::Vector3UVE& corner : corners) {
        if (!ResolveOccluder3DFullyHiddenUVE(config, occluderWorldPosition, viewerWorldPosition, corner)) {
            return false;
        }
    }
    return true;
}

void CollectOccluder3DSnapshotsUVE(IEntityManagerUVE& entityManager, std::vector<Occluder3DSnapshotUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, Occluder3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const Occluder3DComponentUVE& config) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty ||
                !config.enabled || !IsOccluder3DObjectComponentValidUVE(config)) {
                return;
            }
            out.push_back(Occluder3DSnapshotUVE{config, world.worldPosition});
        });
}

bool IsOccluder3DPointDrawHiddenUVE(const std::span<const Occluder3DSnapshotUVE> occluders,
                                    const Math::Vector3UVE& viewerWorldPosition,
                                    const Math::Vector3UVE& pointWorld) noexcept {
    for (const Occluder3DSnapshotUVE& occluder : occluders) {
        if (ResolveOccluder3DFullyHiddenUVE(occluder.config, occluder.worldPosition, viewerWorldPosition,
                                            pointWorld)) {
            return true;
        }
    }
    return false;
}

bool IsOccluder3DAabbDrawHiddenUVE(const std::span<const Occluder3DSnapshotUVE> occluders,
                                   const Math::Vector3UVE& viewerWorldPosition,
                                   const Math::AabbUVE& bounds) noexcept {
    for (const Occluder3DSnapshotUVE& occluder : occluders) {
        if (ResolveOccluder3DFullyHidesAabbUVE(occluder.config, occluder.worldPosition, viewerWorldPosition,
                                               bounds)) {
            return true;
        }
    }
    return false;
}

void CollectOccluder3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<Occluder3DGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, Occluder3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const Occluder3DComponentUVE& config) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty ||
                !IsOccluder3DObjectComponentValidUVE(config)) {
                return;
            }
            Occluder3DGizmoUVE gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.halfExtents = config.halfExtents;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3UVE{0.95F, 0.45F, 0.28F}
                                         : Math::Vector3UVE{0.50F, 0.46F, 0.42F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
