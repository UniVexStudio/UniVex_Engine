// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "raycast_system.h"

#include "aabb.h"
#include "collider_world_aabb_cache.h"
#include "shape_narrow_phase.h"
#include "physics_material.h"
#include "collider_component.h"

namespace UVE::Physics {

namespace {

/// Whether the caller's exclusion list names this entity. Linear over a list that is bounded by
/// the component that authored it (8), on a path that is already O(colliders); a hash set would
/// cost a lookup per collider to save at most seven integer compares.
[[nodiscard]] bool IsExcludedEntity(const RaycastQuery& query,
                                       const Scene::Entity entity) noexcept {
    for (const Scene::Entity excluded : query.excludedEntities) {
        if (excluded == entity) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<RaycastHit> RaycastSystem::Raycast(Scene::IEntityManager& entityManager,
                                                            const RaycastQuery& query) const {
    const std::vector<Detail::ColliderWorldAabb> colliders = Detail::BuildColliderWorldAabbCache(entityManager);

    std::optional<RaycastHit> closestHit;
    float effectiveMaxDistance = query.maxDistance;
    for (const Detail::ColliderWorldAabb& collider : colliders) {
        // Exclusions come first and win over every other filter: a caller that named this entity
        // does not want it back no matter which layer it is on or how close it is.
        if (collider.entity == query.ignoreEntity || IsExcludedEntity(query, collider.entity)) {
            continue;
        }
        if ((collider.collisionLayer & query.layerMask) == 0) {
            continue;
        }
        if (!Math::IntersectRay(query.ray, collider.worldAabb, effectiveMaxDistance).has_value()) {
            continue;
        }

        std::optional<Math::RayHit> hit;
        switch (collider.shapeType) {
        case Scene::ColliderShapeType::Sphere:
            hit = Detail::IntersectRaySphere(
                query.ray,
                Math::Sphere{collider.worldAabb.GetCenter(), collider.shapeRadius}, effectiveMaxDistance);
            break;
        case Scene::ColliderShapeType::Capsule:
            hit = Detail::IntersectRayCapsule(
                query.ray, collider.shapeSegmentStart, collider.shapeSegmentEnd, collider.shapeRadius,
                effectiveMaxDistance);
            break;
        case Scene::ColliderShapeType::Box:
            hit = Detail::IntersectRayOrientedBox(
                query.ray,
                Math::OrientedBox{collider.worldAabb.GetCenter(), collider.shapeHalfExtents,
                                     collider.shapeRotation},
                effectiveMaxDistance);
            break;
        default:
            hit = Math::IntersectRay(query.ray, collider.worldAabb, effectiveMaxDistance);
            break;
        }
        if (!hit.has_value()) {
            continue;
        }
        // Strict `<` (never `<=`): ties are broken deterministically by first-encountered-in-
        // cache-order — i.e. ForEach's own deterministic chunk-order iteration — matching the
        // contract documented on IRaycastSystem::Raycast().
        if (closestHit.has_value() && hit->distance >= closestHit->distance) {
            continue;
        }

        const Scene::ColliderComponent& colliderComponent =
            entityManager.GetComponent<Scene::ColliderComponent>(collider.entity);
        closestHit = RaycastHit{collider.entity, query.ray.origin + query.ray.direction * hit->distance,
                                    hit->normal, hit->distance, MaterialOf(colliderComponent)};
        effectiveMaxDistance = hit->distance;
    }
    return closestHit;
}

} // namespace UVE::Physics
