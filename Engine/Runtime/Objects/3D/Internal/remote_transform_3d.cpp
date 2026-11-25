// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "remote_transform_3d.h"

#include <algorithm>
#include <vector>

#include "disabled_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsRemoteTransform3DComponentValid(
    const RemoteTransform3DComponent& /*value*/) noexcept {
    return true;
}

bool HasRemoteTransform3DActiveChannel(const RemoteTransform3DComponent& value,
                                          const Entity selfEntity) noexcept {
    return value.enabled && value.remoteTarget != kInvalidEntity &&
           value.remoteTarget != selfEntity &&
           (value.updatePosition || value.updateRotation || value.updateScale);
}

bool IsRemoteTransform3DObjectDefinitionValid(
    const RemoteTransform3DObjectDefinition& value) noexcept {
    return IsRemoteTransform3DComponentValid(value.remoteTransform);
}

void ApplyRemoteTransform3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const RemoteTransform3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              RemoteTransform3DObjectDefinition::defaultName);
    entityManager.AddComponent<RemoteTransform3DComponent>(entity, value.remoteTransform);
}

std::size_t UpdateRemoteTransform3DObjects(IEntityManager& entityManager) {
    std::vector<Entity> sources;
    entityManager.ForEach<RemoteTransform3DComponent>(
        [&](const Entity entity, const RemoteTransform3DComponent&) {
            sources.push_back(entity);
        });
    std::sort(sources.begin(), sources.end(), [](const Entity lhs, const Entity rhs) {
        if (lhs.index != rhs.index) {
            return lhs.index < rhs.index;
        }
        return lhs.generation < rhs.generation;
    });

    std::size_t updatedCount = 0U;
    for (const Entity source : sources) {
        if (!entityManager.IsAlive(source) ||
            entityManager.HasComponent<DisabledComponent>(source) ||
            !entityManager.HasComponent<RemoteTransform3DComponent>(source)) {
            continue;
        }
        const RemoteTransform3DComponent& remote =
            entityManager.GetComponent<RemoteTransform3DComponent>(source);
        if (!HasRemoteTransform3DActiveChannel(remote, source)) {
            continue;
        }
        const Entity target = remote.remoteTarget;
        if (!entityManager.IsAlive(target) ||
            !entityManager.HasComponent<TransformComponent>(target)) {
            continue;
        }

        Math::Vector3 srcPos{};
        Math::Quaternion srcRot{};
        Math::Vector3 srcScale{1.0F, 1.0F, 1.0F};
        if (remote.useGlobalCoordinates &&
            entityManager.HasComponent<WorldTransformComponent>(source)) {
            const WorldTransformComponent& world =
                entityManager.GetComponent<WorldTransformComponent>(source);
            if (!IsWorldTransformComponentValid(world)) {
                continue;
            }
            srcPos = world.worldPosition;
            srcRot = world.worldRotation;
            srcScale = world.worldScale;
        } else if (entityManager.HasComponent<TransformComponent>(source)) {
            const TransformComponent& local =
                entityManager.GetComponent<TransformComponent>(source);
            if (!IsTransformComponentValid(local)) {
                continue;
            }
            srcPos = local.localPosition;
            srcRot = local.localRotation;
            srcScale = local.localScale;
        } else {
            continue;
        }

        TransformComponent updatedLocal =
            entityManager.GetComponent<TransformComponent>(target);
        if (remote.updatePosition) {
            updatedLocal.localPosition = srcPos;
        }
        if (remote.updateRotation) {
            updatedLocal.localRotation = srcRot;
        }
        if (remote.updateScale) {
            updatedLocal.localScale = srcScale;
        }
        if (!IsTransformComponentValid(updatedLocal)) {
            continue;
        }
        entityManager.GetComponent<TransformComponent>(target) = updatedLocal;
        if (entityManager.HasComponent<WorldTransformComponent>(target)) {
            entityManager.GetComponent<WorldTransformComponent>(target).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
