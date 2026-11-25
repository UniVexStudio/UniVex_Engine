// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_overlap_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "area_component.h"
#include "world_transform_component.h"
#include "aabb.h"
#include "vector3.h"
#include "collider_world_aabb_cache.h"
#include "shape_narrow_phase.h"

namespace UVE::Physics {

namespace {

[[nodiscard]] bool IsValidAabb(const Math::Aabb& value) noexcept {
    return Math::IsFinite(value.min) && Math::IsFinite(value.max) && value.min.x <= value.max.x &&
           value.min.y <= value.max.y && value.min.z <= value.max.z;
}

struct AreaWorldAabb final {
    Scene::Entity entity;
    Math::Aabb worldAabb;
    std::uint32_t collisionLayer;
    std::uint32_t collisionMask;
    bool monitorable;
};

struct AreaWorldAabbCache final {
    std::vector<AreaWorldAabb> areas;
    bool truncated = false;
};

[[nodiscard]] AreaWorldAabbCache BuildAreaWorldAabbCache(
    Scene::IEntityManager& entityManager, const AreaOverlapParticipation participation) {
    AreaWorldAabbCache cache;
    cache.areas.reserve(kMaximumAreaOverlapQueryAreas);
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::AreaComponent>(
        [&cache, participation](const Scene::Entity entity,
                                const Scene::WorldTransformComponent& worldTransform,
                                const Scene::AreaComponent& area) {
            if (!Scene::IsAreaComponentValid(area)) {
                return;
            }
            if (participation == AreaOverlapParticipation::MonitoringAreas && !area.monitoring) {
                return;
            }
            if (cache.areas.size() >= kMaximumAreaOverlapQueryAreas) {
                cache.truncated = true;
                return;
            }
            const Math::Aabb worldAabb =
                Math::Aabb::FromCenterExtents(worldTransform.worldPosition, area.halfExtents);
            if (!IsValidAabb(worldAabb)) {
                return;
            }
            cache.areas.push_back(
                AreaWorldAabb{entity, worldAabb, area.collisionLayer, area.collisionMask, area.monitorable});
        });
    return cache;
}

[[nodiscard]] std::optional<Math::Penetration> ComputeAreaColliderPenetration(
    const AreaWorldAabb& area, const Detail::ColliderWorldAabb& collider) {
    switch (collider.shapeType) {
    case Scene::ColliderShapeType::Sphere:
        return Detail::ComputeSphereAabbPenetration(
            area.worldAabb, Math::Sphere{collider.worldAabb.GetCenter(), collider.shapeRadius});
    case Scene::ColliderShapeType::Capsule:
        return Detail::ComputeCapsuleAabbPenetration(
            area.worldAabb, collider.shapeSegmentStart, collider.shapeSegmentEnd, collider.shapeRadius);
    case Scene::ColliderShapeType::Box:
        return Detail::ComputeOrientedBoxOrientedBoxPenetration(
            area.worldAabb.GetCenter(), area.worldAabb.GetExtents(), {},
            collider.worldAabb.GetCenter(), collider.shapeHalfExtents, collider.shapeRotation);
    default:
        return Math::ComputePenetration(area.worldAabb, collider.worldAabb);
    }
}

} // namespace

AreaOverlapQueryResult AreaOverlapSystem::Query(
    Scene::IEntityManager& entityManager, const std::size_t maximumResults,
    const AreaOverlapParticipation participation) {
    const AreaWorldAabbCache areaCache = BuildAreaWorldAabbCache(entityManager, participation);
    const std::vector<Detail::ColliderWorldAabb> colliders = Detail::BuildColliderWorldAabbCache(entityManager);

    const std::size_t resultCap = std::min(maximumResults, kMaximumAreaOverlapResults);
    AreaOverlapQueryResult result;
    result.inspectedAreas = areaCache.areas.size();
    result.inspectedColliders = colliders.size();
    result.truncated = areaCache.truncated;
    result.overlaps.reserve(std::min(resultCap, colliders.size()));
    const auto appendOverlap = [&result, resultCap](const AreaOverlapPair& overlap) {
        if (result.overlaps.size() >= resultCap) {
            result.truncated = true;
            return;
        }
        result.overlaps.push_back(overlap);
    };
    for (const AreaWorldAabb& area : areaCache.areas) {
        for (const Detail::ColliderWorldAabb& collider : colliders) {
            if ((area.collisionMask & collider.collisionLayer) == 0U ||
                (collider.collisionMask & area.collisionLayer) == 0U) {
                continue;
            }
            const std::optional<Math::Penetration> penetration =
                ComputeAreaColliderPenetration(area, collider);
            if (!penetration.has_value() || !std::isfinite(penetration->depth) || penetration->depth <= 0.0F) {
                continue;
            }
            appendOverlap(AreaOverlapPair{area.entity, collider.entity, penetration->depth});
        }
        for (const AreaWorldAabb& otherArea : areaCache.areas) {
            if (area.entity == otherArea.entity || !otherArea.monitorable ||
                (area.collisionMask & otherArea.collisionLayer) == 0U ||
                (otherArea.collisionMask & area.collisionLayer) == 0U) {
                continue;
            }
            const std::optional<Math::Penetration> penetration =
                Math::ComputePenetration(area.worldAabb, otherArea.worldAabb);
            if (!penetration.has_value() || !std::isfinite(penetration->depth) || penetration->depth <= 0.0F) {
                continue;
            }
            appendOverlap(AreaOverlapPair{area.entity, otherArea.entity, penetration->depth});
        }
    }
    return result;
}

} // namespace UVE::Physics
