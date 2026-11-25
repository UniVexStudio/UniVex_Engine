// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "nav_obstacle_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "nav_seeker_3d.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsNavigationObstacle3DComponentValid(
    const NavigationObstacle3DComponent& value) noexcept {
    return std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.height) &&
           value.height > 0.0F && IsFinite3DObjectVector(value.velocity) &&
           value.avoidanceLayers != 0U;
}

NavigationObstacle3DComponent SanitizeNavigationObstacle3DComponent(
    const NavigationObstacle3DComponent& value) noexcept {
    NavigationObstacle3DComponent out = value;
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.75F;
    }
    if (!std::isfinite(out.height) || out.height <= 0.0F) {
        out.height = 2.0F;
    }
    if (!IsFinite3DObjectVector(out.velocity)) {
        out.velocity = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (out.avoidanceLayers == 0U) {
        out.avoidanceLayers = 1U;
    }
    return out;
}

bool IsNavigationObstacle3DObjectDefinitionValid(
    const NavigationObstacle3DObjectDefinition& value) noexcept {
    return IsNavigationObstacle3DComponentValid(value.obstacle);
}

void ApplyNavigationObstacle3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const NavigationObstacle3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              NavigationObstacle3DObjectDefinition::defaultName);
    entityManager.AddComponent<NavigationObstacle3DComponent>(
        entity, SanitizeNavigationObstacle3DComponent(value.obstacle));
}

bool IsNavigationLink3DComponentValid(const NavigationLink3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.startOffset) &&
           IsFinite3DObjectVector(value.endOffset) &&
           Math::LengthSquared(value.endOffset - value.startOffset) > 1.0e-6F &&
           std::isfinite(value.travelCostMultiplier) && value.travelCostMultiplier > 0.0F &&
           value.navigationLayers != 0U;
}

NavigationLink3DComponent SanitizeNavigationLink3DComponent(
    const NavigationLink3DComponent& value) noexcept {
    NavigationLink3DComponent out = value;
    if (!IsFinite3DObjectVector(out.startOffset)) {
        out.startOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVector(out.endOffset) ||
        Math::LengthSquared(out.endOffset - out.startOffset) <= 1.0e-6F) {
        out.endOffset = out.startOffset + Math::Vector3{0.0F, 0.0F, -2.0F};
    }
    if (!std::isfinite(out.travelCostMultiplier) || out.travelCostMultiplier <= 0.0F) {
        out.travelCostMultiplier = 1.0F;
    }
    if (out.navigationLayers == 0U) {
        out.navigationLayers = 1U;
    }
    return out;
}

bool IsNavigationLink3DObjectDefinitionValid(
    const NavigationLink3DObjectDefinition& value) noexcept {
    return IsNavigationLink3DComponentValid(value.link);
}

void ApplyNavigationLink3DObjectDefinition(IEntityManager& entityManager,
                                              const Entity entity,
                                              const NavigationLink3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              NavigationLink3DObjectDefinition::defaultName);
    entityManager.AddComponent<NavigationLink3DComponent>(
        entity, SanitizeNavigationLink3DComponent(value.link));
}

float ResolveNavigationLink3DWorldEndpointsAndCost(
    const NavigationLink3DComponent& link, const WorldTransformComponent& worldTransform,
    Math::Vector3& outWorldStart, Math::Vector3& outWorldEnd) noexcept {
    const NavigationLink3DComponent safe = SanitizeNavigationLink3DComponent(link);
    outWorldStart = TransformWorldPoint(worldTransform, safe.startOffset);
    outWorldEnd = TransformWorldPoint(worldTransform, safe.endOffset);
    const float dist = std::sqrt(Math::LengthSquared(outWorldEnd - outWorldStart));
    return dist * safe.travelCostMultiplier;
}

std::size_t ApplyNavigationObstacles3DToAgents(IEntityManager& entityManager) {
    struct ObstacleSnapshot final {
        Entity entity;
        Math::Vector3 position;
        float radius;
        std::uint32_t layers;
    };
    std::vector<ObstacleSnapshot> obstacles;
    entityManager.ForEach<NavigationObstacle3DComponent, WorldTransformComponent>(
        [&](const Entity entity, const NavigationObstacle3DComponent& obs,
            const WorldTransformComponent& world) {
            if (!obs.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsNavigationObstacle3DComponentValid(obs) ||
                !IsWorldTransformComponentValid(world)) {
                return;
            }
            obstacles.push_back(
                ObstacleSnapshot{entity, world.worldPosition, obs.radius, obs.avoidanceLayers});
        });
    if (obstacles.empty()) {
        return 0U;
    }

    std::size_t deflectedCount = 0U;
    entityManager.ForEach<NavSeeker3DComponent, WorldTransformComponent>(
        [&](const Entity agentEntity, NavSeeker3DComponent& seeker,
            const WorldTransformComponent& agentWorld) {
            if (!seeker.enabled || !seeker.avoidanceEnabled ||
                entityManager.HasComponent<DisabledComponent>(agentEntity) ||
                !IsWorldTransformComponentValid(agentWorld)) {
                return;
            }
            bool deflected = false;
            for (const ObstacleSnapshot& obs : obstacles) {
                if (obs.entity == agentEntity ||
                    (obs.layers & seeker.navigationLayers) == 0U) {
                    continue;
                }
                const float combinedRadius = obs.radius + seeker.avoidanceRadius;
                const Math::Vector3 delta{agentWorld.worldPosition.x - obs.position.x, 0.0F,
                                             agentWorld.worldPosition.z - obs.position.z};
                const float distSq = Math::LengthSquared(delta);
                if (distSq >= combinedRadius * combinedRadius) {
                    continue;
                }
                const float dist = std::sqrt(distSq);
                const Math::Vector3 pushDir =
                    (dist > 1.0e-5F) ? (delta * (1.0F / dist))
                                     : Math::Vector3{1.0F, 0.0F, 0.0F};
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
