// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/path_follow_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/path_3d_uve.h"

namespace UVE::Scene {

bool IsPathFollow3DComponentValidUVE(const PathFollow3DComponentUVE& value) noexcept {
    return std::isfinite(value.progressMetres) && std::isfinite(value.speedMetresPerSecond) &&
           std::isfinite(value.hOffset) && std::isfinite(value.vOffset);
}

PathFollow3DComponentUVE SanitizePathFollow3DComponentUVE(
    const PathFollow3DComponentUVE& value) noexcept {
    PathFollow3DComponentUVE out = value;
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

void StepPathFollow3DProgressUVE(PathFollow3DComponentUVE& value, const float pathTotalLength,
                                 const float deltaTimeSeconds) noexcept {
    if (!IsPathFollow3DComponentValidUVE(value)) {
        value = SanitizePathFollow3DComponentUVE(value);
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

bool IsPathFollow3DObjectDefinitionValidUVE(
    const PathFollow3DObjectDefinitionUVE& value) noexcept {
    return IsPathFollow3DComponentValidUVE(value.follower);
}

void ApplyPathFollow3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const PathFollow3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, PathFollow3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PathFollow3DComponentUVE>(
        entity, SanitizePathFollow3DComponentUVE(value.follower));
}

std::size_t UpdatePathFollow3DObjectsUVE(IEntityManagerUVE& entityManager,
                                         const float deltaTimeSeconds) {
    std::vector<EntityUVE> followers;
    entityManager.ForEachUVE<PathFollow3DComponentUVE>(
        [&](const EntityUVE entity, const PathFollow3DComponentUVE&) {
            followers.push_back(entity);
        });
    std::sort(followers.begin(), followers.end(), [](const EntityUVE lhs, const EntityUVE rhs) {
        if (lhs.index != rhs.index) {
            return lhs.index < rhs.index;
        }
        return lhs.generation < rhs.generation;
    });

    std::size_t updatedCount = 0U;
    for (const EntityUVE entity : followers) {
        if (!entityManager.IsAliveUVE(entity) ||
            entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<PathFollow3DComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
            continue;
        }
        PathFollow3DComponentUVE& follower =
            entityManager.GetComponentUVE<PathFollow3DComponentUVE>(entity);
        if (!follower.enabled || !IsPathFollow3DComponentValidUVE(follower) ||
            follower.pathEntity == kInvalidEntityUVE ||
            !entityManager.IsAliveUVE(follower.pathEntity) ||
            !entityManager.HasComponentUVE<Path3DComponentUVE>(follower.pathEntity)) {
            continue;
        }
        const Path3DComponentUVE& path =
            entityManager.GetComponentUVE<Path3DComponentUVE>(follower.pathEntity);
        const float totalLen = Path3DUVE::ComputeTotalLengthUVE(path);
        if (totalLen <= 1.0e-6F) {
            continue;
        }
        StepPathFollow3DProgressUVE(follower, totalLen, deltaTimeSeconds);
        const WorldTransformComponentUVE pathWorld =
            entityManager.HasComponentUVE<WorldTransformComponentUVE>(follower.pathEntity)
                ? entityManager.GetComponentUVE<WorldTransformComponentUVE>(follower.pathEntity)
                : WorldTransformComponentUVE{};
        const Path3DSampledPoseUVE sample = Path3DUVE::SampleWorldPoseAtDistanceUVE(
            path, pathWorld, follower.progressMetres, follower.loop);

        TransformComponentUVE updatedLocal =
            entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        updatedLocal.localPosition =
            sample.position + sample.right * follower.hOffset + sample.up * follower.vOffset;
        if (follower.rotateToTangent) {
            updatedLocal.localRotation = sample.rotation;
        }
        if (!IsTransformComponentValidUVE(updatedLocal)) {
            continue;
        }
        entityManager.GetComponentUVE<TransformComponentUVE>(entity) = updatedLocal;
        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
