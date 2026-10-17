// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/path_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsPath2DComponentValidUVE(const Path2DComponentUVE& value) noexcept {
    if (value.points.size() > kMaximumPath2DPointsUVE) {
        return false;
    }
    for (const Math::Vector2UVE& pt : value.points) {
        if (!Math::IsFiniteUVE(pt)) {
            return false;
        }
    }
    return true;
}

Path2DComponentUVE SanitizePath2DComponentUVE(const Path2DComponentUVE& value) {
    Path2DComponentUVE out = value;
    if (out.points.size() > kMaximumPath2DPointsUVE) {
        out.points.resize(kMaximumPath2DPointsUVE);
    }
    for (Math::Vector2UVE& pt : out.points) {
        if (!Math::IsFiniteUVE(pt)) {
            pt = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    return out;
}

bool IsPath2DObjectDefinitionValidUVE(const Path2DObjectDefinitionUVE& value) noexcept {
    return IsPath2DComponentValidUVE(value.path);
}

void ApplyPath2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Path2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Path2DComponentUVE>(entity,
                                                      SanitizePath2DComponentUVE(value.path));
}

float Path2DUVE::ComputeTotalLengthUVE(const Path2DComponentUVE& path) noexcept {
    if (!IsPath2DComponentValidUVE(path) || path.points.size() < 2U) {
        return 0.0F;
    }
    float total = 0.0F;
    const std::size_t n = path.points.size();
    for (std::size_t i = 0U; i + 1U < n; ++i) {
        const Math::Vector2UVE d = path.points[i + 1U] - path.points[i];
        total += std::sqrt(d.x * d.x + d.y * d.y);
    }
    if (path.closed && n >= 3U) {
        const Math::Vector2UVE d = path.points.front() - path.points.back();
        total += std::sqrt(d.x * d.x + d.y * d.y);
    }
    return total;
}

Path2DSampledPoseUVE Path2DUVE::SamplePoseAtDistanceUVE(const Path2DComponentUVE& path,
                                                        const float distancePixels,
                                                        const bool loop) noexcept {
    Path2DSampledPoseUVE sample{};
    if (!IsPath2DComponentValidUVE(path) || path.points.empty()) {
        return sample;
    }
    if (path.points.size() == 1U) {
        sample.position = path.points.front();
        return sample;
    }
    const float totalLen = ComputeTotalLengthUVE(path);
    if (totalLen <= 1.0e-6F || !std::isfinite(distancePixels)) {
        sample.position = path.points.front();
        return sample;
    }
    float targetDist = distancePixels;
    if (loop || path.closed) {
        targetDist = std::fmod(distancePixels, totalLen);
        if (targetDist < 0.0F) {
            targetDist += totalLen;
        }
    } else {
        targetDist = std::clamp(distancePixels, 0.0F, totalLen);
    }

    const std::size_t n = path.points.size();
    const std::size_t segCount = (path.closed && n >= 3U) ? n : (n - 1U);
    float accum = 0.0F;
    for (std::size_t seg = 0U; seg < segCount; ++seg) {
        const Math::Vector2UVE& a = path.points[seg];
        const Math::Vector2UVE& b = path.points[(seg + 1U) % n];
        const Math::Vector2UVE d = b - a;
        const float segLen = std::sqrt(d.x * d.x + d.y * d.y);
        if (segLen <= 1.0e-6F) {
            continue;
        }
        if (accum + segLen >= targetDist || seg + 1U == segCount) {
            const float t = std::clamp((targetDist - accum) / segLen, 0.0F, 1.0F);
            sample.position = a + d * t;
            sample.tangent = d * (1.0F / segLen);
            sample.normal = Math::Vector2UVE{-sample.tangent.y, sample.tangent.x};
            sample.rotationRadians = std::atan2(sample.tangent.y, sample.tangent.x);
            return sample;
        }
        accum += segLen;
    }
    sample.position = path.points.back();
    return sample;
}

bool IsPathFollow2DComponentValidUVE(const PathFollow2DComponentUVE& value) noexcept {
    return std::isfinite(value.progressPixels) && std::isfinite(value.speedPixelsPerSecond) &&
           std::isfinite(value.hOffset) && std::isfinite(value.vOffset);
}

PathFollow2DComponentUVE SanitizePathFollow2DComponentUVE(
    const PathFollow2DComponentUVE& value) noexcept {
    PathFollow2DComponentUVE out = value;
    if (!std::isfinite(out.progressPixels)) {
        out.progressPixels = 0.0F;
    }
    if (!std::isfinite(out.speedPixelsPerSecond)) {
        out.speedPixelsPerSecond = 0.0F;
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

bool IsPathFollow2DObjectDefinitionValidUVE(
    const PathFollow2DObjectDefinitionUVE& value) noexcept {
    return IsPathFollow2DComponentValidUVE(value.follower);
}

void ApplyPathFollow2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const PathFollow2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<PathFollow2DComponentUVE>(
        entity, SanitizePathFollow2DComponentUVE(value.follower));
}

std::size_t UpdatePathFollow2DObjectsUVE(IEntityManagerUVE& entityManager,
                                         const float deltaTimeSeconds) {
    std::size_t updatedCount = 0U;
    entityManager.ForEachUVE<PathFollow2DComponentUVE>(
        [&](const EntityUVE entity, PathFollow2DComponentUVE& follower) {
            if (!follower.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsPathFollow2DComponentValidUVE(follower) ||
                follower.pathEntity == kInvalidEntityUVE ||
                !entityManager.IsAliveUVE(follower.pathEntity) ||
                !entityManager.HasComponentUVE<Path2DComponentUVE>(follower.pathEntity) ||
                !entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                return;
            }
            const Path2DComponentUVE& path =
                entityManager.GetComponentUVE<Path2DComponentUVE>(follower.pathEntity);
            const float totalLen = Path2DUVE::ComputeTotalLengthUVE(path);
            if (totalLen <= 1.0e-6F) {
                return;
            }
            if (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) {
                follower.progressPixels += follower.speedPixelsPerSecond * deltaTimeSeconds;
            }
            if (follower.loop || path.closed) {
                follower.progressPixels = std::fmod(follower.progressPixels, totalLen);
                if (follower.progressPixels < 0.0F) {
                    follower.progressPixels += totalLen;
                }
            } else {
                follower.progressPixels = std::clamp(follower.progressPixels, 0.0F, totalLen);
            }
            follower.progressRatio = std::clamp(follower.progressPixels / totalLen, 0.0F, 1.0F);
            const Path2DSampledPoseUVE sample =
                Path2DUVE::SamplePoseAtDistanceUVE(path, follower.progressPixels, follower.loop);
            const Math::Vector2UVE finalPos2D =
                sample.position + sample.tangent * follower.hOffset +
                sample.normal * follower.vOffset;

            TransformComponentUVE& local =
                entityManager.GetComponentUVE<TransformComponentUVE>(entity);
            local.localPosition.x = finalPos2D.x;
            local.localPosition.y = finalPos2D.y;
            if (follower.rotateToTangent) {
                const float halfAngle = 0.5F * sample.rotationRadians;
                local.localRotation =
                    Math::QuaternionUVE{0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
            }
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
            }
            ++updatedCount;
        });
    return updatedCount;
}

} // namespace UVE::Scene
