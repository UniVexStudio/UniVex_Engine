// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "path_follow_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "disabled_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"
#include "path_3d.h"

namespace UVE::Scene {

bool IsPathFollow3DComponentValid(const PathFollow3DComponent& value) noexcept {
    return std::isfinite(value.progressMetres) && std::isfinite(value.speedMetresPerSecond) &&
           std::isfinite(value.hOffset) && std::isfinite(value.vOffset);
}

PathFollow3DComponent SanitizePathFollow3DComponent(
    const PathFollow3DComponent& value) noexcept {
    PathFollow3DComponent out = value;
    if (!std::isfinite(out.progressMetres)) {
        out.progressMetres = 0.0F;
    }
    if (!std::isfinite(out.speedMetresPerSecond)) {
        out.speedMetresPerSecond = 0.0F;
    }
    if (!std::isfinite(out.hOffset)) {
        out.hOffset = 0.0F;
    }
    if (!std::isfinite(out.vOffset)) {
        out.vOffset = 0.0F;
    }
    if (!std::isfinite(out.progressRatio)) {
        out.progressRatio = 0.0F;
    }
    return out;
}

void StepPathFollow3DProgress(PathFollow3DComponent& value, const float pathTotalLength,
                                 const float deltaTimeSeconds) noexcept {
    if (!IsPathFollow3DComponentValid(value)) {
        value = SanitizePathFollow3DComponent(value);
    }
    if (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) {
        value.progressMetres += value.speedMetresPerSecond * deltaTimeSeconds;
    }
    if (!std::isfinite(pathTotalLength) || pathTotalLength <= 1.0e-6F) {
        value.progressMetres = 0.0F;
        value.progressRatio = 0.0F;
        return;
    }
    if (value.loop) {
        value.progressMetres = std::fmod(value.progressMetres, pathTotalLength);
        if (value.progressMetres < 0.0F) {
            value.progressMetres += pathTotalLength;
        }
    } else {
        value.progressMetres = std::clamp(value.progressMetres, 0.0F, pathTotalLength);
    }
    value.progressRatio = std::clamp(value.progressMetres / pathTotalLength, 0.0F, 1.0F);
}

bool IsPathFollow3DObjectDefinitionValid(
    const PathFollow3DObjectDefinition& value) noexcept {
    return IsPathFollow3DComponentValid(value.follower);
}

void ApplyPathFollow3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const PathFollow3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, PathFollow3DObjectDefinition::defaultName);
    entityManager.AddComponent<PathFollow3DComponent>(
        entity, SanitizePathFollow3DComponent(value.follower));
}

std::size_t UpdatePathFollow3DObjects(IEntityManager& entityManager,
                                         const float deltaTimeSeconds) {
    std::vector<Entity> followers;
    entityManager.ForEach<PathFollow3DComponent>(
        [&](const Entity entity, const PathFollow3DComponent&) {
            followers.push_back(entity);
        });
    std::sort(followers.begin(), followers.end(), [](const Entity lhs, const Entity rhs) {
        if (lhs.index != rhs.index) {
            return lhs.index < rhs.index;
        }
        return lhs.generation < rhs.generation;
    });

    std::size_t updatedCount = 0U;
    for (const Entity entity : followers) {
        if (!entityManager.IsAlive(entity) ||
            entityManager.HasComponent<DisabledComponent>(entity) ||
            !entityManager.HasComponent<PathFollow3DComponent>(entity) ||
            !entityManager.HasComponent<TransformComponent>(entity)) {
            continue;
        }
        PathFollow3DComponent& follower =
            entityManager.GetComponent<PathFollow3DComponent>(entity);
        if (!follower.enabled || !IsPathFollow3DComponentValid(follower) ||
            follower.pathEntity == kInvalidEntity ||
            !entityManager.IsAlive(follower.pathEntity) ||
            !entityManager.HasComponent<Path3DComponent>(follower.pathEntity)) {
            continue;
        }
        const Path3DComponent& path =
            entityManager.GetComponent<Path3DComponent>(follower.pathEntity);
        const float totalLen = Path3D::ComputeTotalLength(path);
        if (totalLen <= 1.0e-6F) {
            continue;
        }
        StepPathFollow3DProgress(follower, totalLen, deltaTimeSeconds);
        const WorldTransformComponent pathWorld =
            entityManager.HasComponent<WorldTransformComponent>(follower.pathEntity)
                ? entityManager.GetComponent<WorldTransformComponent>(follower.pathEntity)
                : WorldTransformComponent{};
        const Path3DSampledPose sample = Path3D::SampleWorldPoseAtDistance(
            path, pathWorld, follower.progressMetres, follower.loop);

        TransformComponent updatedLocal =
            entityManager.GetComponent<TransformComponent>(entity);
        updatedLocal.localPosition =
            sample.position + sample.right * follower.hOffset + sample.up * follower.vOffset;
        if (follower.rotateToTangent) {
            updatedLocal.localRotation = sample.rotation;
        }
        if (!IsTransformComponentValid(updatedLocal)) {
            continue;
        }
        entityManager.GetComponent<TransformComponent>(entity) = updatedLocal;
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
