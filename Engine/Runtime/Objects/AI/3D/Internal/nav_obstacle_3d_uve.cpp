// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/nav_obstacle_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/nav_seeker_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsNavigationObstacle3DComponentValidUVE(
    const NavigationObstacle3DComponentUVE& value) noexcept {
    return std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.height) &&
           value.height > 0.0F && IsFinite3DObjectVectorUVE(value.velocity) &&
           value.avoidanceLayers != 0U;
}

NavigationObstacle3DComponentUVE SanitizeNavigationObstacle3DComponentUVE(
    const NavigationObstacle3DComponentUVE& value) noexcept {
    NavigationObstacle3DComponentUVE out = value;
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.75F;
    }
    if (!std::isfinite(out.height) || out.height <= 0.0F) {
        out.height = 2.0F;
    }
    if (!IsFinite3DObjectVectorUVE(out.velocity)) {
        out.velocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (out.avoidanceLayers == 0U) {
        out.avoidanceLayers = 1U;
    }
    return out;
}

bool IsNavigationObstacle3DObjectDefinitionValidUVE(
    const NavigationObstacle3DObjectDefinitionUVE& value) noexcept {
    return IsNavigationObstacle3DComponentValidUVE(value.obstacle);
}

void ApplyNavigationObstacle3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const NavigationObstacle3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              NavigationObstacle3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<NavigationObstacle3DComponentUVE>(
        entity, SanitizeNavigationObstacle3DComponentUVE(value.obstacle));
}

bool IsNavigationLink3DComponentValidUVE(const NavigationLink3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.startOffset) &&
           IsFinite3DObjectVectorUVE(value.endOffset) &&
           Math::LengthSquaredUVE(value.endOffset - value.startOffset) > 1.0e-6F &&
           std::isfinite(value.travelCostMultiplier) && value.travelCostMultiplier > 0.0F &&
           value.navigationLayers != 0U;
}

NavigationLink3DComponentUVE SanitizeNavigationLink3DComponentUVE(
    const NavigationLink3DComponentUVE& value) noexcept {
    NavigationLink3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.startOffset)) {
        out.startOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVectorUVE(out.endOffset) ||
        Math::LengthSquaredUVE(out.endOffset - out.startOffset) <= 1.0e-6F) {
        out.endOffset = out.startOffset + Math::Vector3UVE{0.0F, 0.0F, -2.0F};
    }
    if (!std::isfinite(out.travelCostMultiplier) || out.travelCostMultiplier <= 0.0F) {
        out.travelCostMultiplier = 1.0F;
    }
    if (out.navigationLayers == 0U) {
        out.navigationLayers = 1U;
    }
    return out;
}

bool IsNavigationLink3DObjectDefinitionValidUVE(
    const NavigationLink3DObjectDefinitionUVE& value) noexcept {
    return IsNavigationLink3DComponentValidUVE(value.link);
}

void ApplyNavigationLink3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const NavigationLink3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              NavigationLink3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<NavigationLink3DComponentUVE>(
        entity, SanitizeNavigationLink3DComponentUVE(value.link));
}

float ResolveNavigationLink3DWorldEndpointsAndCostUVE(
    const NavigationLink3DComponentUVE& link, const WorldTransformComponentUVE& worldTransform,
    Math::Vector3UVE& outWorldStart, Math::Vector3UVE& outWorldEnd) noexcept {
    const NavigationLink3DComponentUVE safe = SanitizeNavigationLink3DComponentUVE(link);
    outWorldStart = TransformWorldPointUVE(worldTransform, safe.startOffset);
    outWorldEnd = TransformWorldPointUVE(worldTransform, safe.endOffset);
    const float dist = std::sqrt(Math::LengthSquaredUVE(outWorldEnd - outWorldStart));
    return dist * safe.travelCostMultiplier;
}

std::size_t ApplyNavigationObstacles3DToAgentsUVE(IEntityManagerUVE& entityManager) {
    struct ObstacleSnapshotUVE final {
        EntityUVE entity;
        Math::Vector3UVE position;
        float radius;
        std::uint32_t layers;
    };
    std::vector<ObstacleSnapshotUVE> obstacles;
    entityManager.ForEachUVE<NavigationObstacle3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const NavigationObstacle3DComponentUVE& obs,
            const WorldTransformComponentUVE& world) {
            if (!obs.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsNavigationObstacle3DComponentValidUVE(obs) ||
                !IsWorldTransformComponentValidUVE(world)) {
                return;
            }
            obstacles.push_back(
                ObstacleSnapshotUVE{entity, world.worldPosition, obs.radius, obs.avoidanceLayers});
        });
    if (obstacles.empty()) {
        return 0U;
    }

    std::size_t deflectedCount = 0U;
    entityManager.ForEachUVE<NavSeeker3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE agentEntity, NavSeeker3DComponentUVE& seeker,
            const WorldTransformComponentUVE& agentWorld) {
            if (!seeker.enabled || !seeker.avoidanceEnabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(agentEntity) ||
                !IsWorldTransformComponentValidUVE(agentWorld)) {
                return;
            }
            bool deflected = false;
            for (const ObstacleSnapshotUVE& obs : obstacles) {
                if (obs.entity == agentEntity ||
                    (obs.layers & seeker.navigationLayers) == 0U) {
                    continue;
                }
                const float combinedRadius = obs.radius + seeker.avoidanceRadius;
                const Math::Vector3UVE delta{agentWorld.worldPosition.x - obs.position.x, 0.0F,
                                             agentWorld.worldPosition.z - obs.position.z};
                const float distSq = Math::LengthSquaredUVE(delta);
                if (distSq >= combinedRadius * combinedRadius) {
                    continue;
                }
                const float dist = std::sqrt(distSq);
                const Math::Vector3UVE pushDir =
                    (dist > 1.0e-5F) ? (delta * (1.0F / dist))
                                     : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
                const float penetrationRatio =
                    std::clamp((combinedRadius - dist) / combinedRadius, 0.0F, 1.0F);
                seeker.desiredVelocity =
                    seeker.desiredVelocity + pushDir * (seeker.maxSpeed * penetrationRatio);
                deflected = true;
            }
            if (deflected) {
                ++deflectedCount;
            }
        });
    return deflectedCount;
}

} // namespace UVE::Scene
