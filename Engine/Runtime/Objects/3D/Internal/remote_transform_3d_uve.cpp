// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/remote_transform_3d_uve.h"

#include <algorithm>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsRemoteTransform3DComponentValidUVE(
    const RemoteTransform3DComponentUVE& /*value*/) noexcept {
    return true;
}

bool HasRemoteTransform3DActiveChannelUVE(const RemoteTransform3DComponentUVE& value,
                                          const EntityUVE selfEntity) noexcept {
    return value.enabled && value.remoteTarget != kInvalidEntityUVE &&
           value.remoteTarget != selfEntity &&
           (value.updatePosition || value.updateRotation || value.updateScale);
}

bool IsRemoteTransform3DObjectDefinitionValidUVE(
    const RemoteTransform3DObjectDefinitionUVE& value) noexcept {
    return IsRemoteTransform3DComponentValidUVE(value.remoteTransform);
}

void ApplyRemoteTransform3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const RemoteTransform3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              RemoteTransform3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<RemoteTransform3DComponentUVE>(entity, value.remoteTransform);
}

std::size_t UpdateRemoteTransform3DObjectsUVE(IEntityManagerUVE& entityManager) {
    std::vector<EntityUVE> sources;
    entityManager.ForEachUVE<RemoteTransform3DComponentUVE>(
        [&](const EntityUVE entity, const RemoteTransform3DComponentUVE&) {
            sources.push_back(entity);
        });
    std::sort(sources.begin(), sources.end(), [](const EntityUVE lhs, const EntityUVE rhs) {
        if (lhs.index != rhs.index) {
            return lhs.index < rhs.index;
        }
        return lhs.generation < rhs.generation;
    });

    std::size_t updatedCount = 0U;
    for (const EntityUVE source : sources) {
        if (!entityManager.IsAliveUVE(source) ||
            entityManager.HasComponentUVE<DisabledComponentUVE>(source) ||
            !entityManager.HasComponentUVE<RemoteTransform3DComponentUVE>(source)) {
            continue;
        }
        const RemoteTransform3DComponentUVE& remote =
            entityManager.GetComponentUVE<RemoteTransform3DComponentUVE>(source);
        if (!HasRemoteTransform3DActiveChannelUVE(remote, source)) {
            continue;
        }
        const EntityUVE target = remote.remoteTarget;
        if (!entityManager.IsAliveUVE(target) ||
            !entityManager.HasComponentUVE<TransformComponentUVE>(target)) {
            continue;
        }

        Math::Vector3UVE srcPos{};
        Math::QuaternionUVE srcRot{};
        Math::Vector3UVE srcScale{1.0F, 1.0F, 1.0F};
        if (remote.useGlobalCoordinates &&
            entityManager.HasComponentUVE<WorldTransformComponentUVE>(source)) {
            const WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(source);
            if (!IsWorldTransformComponentValidUVE(world)) {
                continue;
            }
            srcPos = world.worldPosition;
            srcRot = world.worldRotation;
            srcScale = world.worldScale;
        } else if (entityManager.HasComponentUVE<TransformComponentUVE>(source)) {
            const TransformComponentUVE& local =
                entityManager.GetComponentUVE<TransformComponentUVE>(source);
            if (!IsTransformComponentValidUVE(local)) {
                continue;
            }
            srcPos = local.localPosition;
            srcRot = local.localRotation;
            srcScale = local.localScale;
        } else {
            continue;
        }

        TransformComponentUVE updatedLocal =
            entityManager.GetComponentUVE<TransformComponentUVE>(target);
        if (remote.updatePosition) {
            updatedLocal.localPosition = srcPos;
        }
        if (remote.updateRotation) {
            updatedLocal.localRotation = srcRot;
        }
        if (remote.updateScale) {
            updatedLocal.localScale = srcScale;
        }
        if (!IsTransformComponentValidUVE(updatedLocal)) {
            continue;
        }
        entityManager.GetComponentUVE<TransformComponentUVE>(target) = updatedLocal;
        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(target)) {
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(target).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
