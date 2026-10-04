// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/raycast_system_uve.h"

#include "uve/math/aabb_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"
#include "uve/physics/physics_material_uve.h"
#include "uve/component/collider_component_uve.h"

namespace UVE::Physics {

namespace {

/// Whether the caller's exclusion list names this entity. Linear over a list that is bounded by
/// the component that authored it (8), on a path that is already O(colliders); a hash set would
/// cost a lookup per collider to save at most seven integer compares.
[[nodiscard]] bool IsExcludedEntityUVE(const RaycastQueryUVE& query,
                                       const Scene::EntityUVE entity) noexcept {
    for (const Scene::EntityUVE excluded : query.excludedEntities) {
        if (excluded == entity) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<RaycastHitUVE> RaycastSystemUVE::RaycastUVE(Scene::IEntityManagerUVE& entityManager,
                                                            const RaycastQueryUVE& query) const {
    const std::vector<Detail::ColliderWorldAabbUVE> colliders = Detail::BuildColliderWorldAabbCacheUVE(entityManager);

    std::optional<RaycastHitUVE> closestHit;
    for (const Detail::ColliderWorldAabbUVE& collider : colliders) {
        // Exclusions come first and win over every other filter: a caller that named this entity
        // does not want it back no matter which layer it is on or how close it is.
        if (collider.entity == query.ignoreEntity || IsExcludedEntityUVE(query, collider.entity)) {
            continue;
        }
        if ((collider.collisionLayer & query.layerMask) == 0) {
            continue;
        }

        std::optional<Math::RayHitUVE> hit;
        switch (collider.shapeType) {
        case Scene::ColliderShapeTypeUVE::Sphere:
            hit = Detail::IntersectRaySphereUVE(
                query.ray, collider.worldAabb.GetCenterUVE(), collider.shapeRadius, query.maxDistance);
            break;
        case Scene::ColliderShapeTypeUVE::Capsule:
            hit = Detail::IntersectRayCapsuleUVE(
                query.ray, collider.shapeSegmentStart, collider.shapeSegmentEnd, collider.shapeRadius,
                query.maxDistance);
            break;
        case Scene::ColliderShapeTypeUVE::Box:
            hit = Detail::IntersectRayOrientedBoxUVE(
                query.ray, collider.worldAabb.GetCenterUVE(), collider.shapeHalfExtents,
                collider.shapeRotation, query.maxDistance);
            break;
        default:
            hit = Math::IntersectRayUVE(query.ray, collider.worldAabb, query.maxDistance);
            break;
        }
        if (!hit.has_value()) {
            continue;
        }
        // Strict `<` (never `<=`): ties are broken deterministically by first-encountered-in-
        // cache-order — i.e. ForEachUVE's own deterministic chunk-order iteration — matching the
        // contract documented on IRaycastSystemUVE::RaycastUVE().
        if (closestHit.has_value() && hit->distance >= closestHit->distance) {
            continue;
        }

        const Scene::ColliderComponentUVE& colliderComponent =
            entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(collider.entity);
        closestHit = RaycastHitUVE{collider.entity, query.ray.origin + query.ray.direction * hit->distance,
                                    hit->normal, hit->distance, MaterialOfUVE(colliderComponent)};
    }
    return closestHit;
}

} // namespace UVE::Physics
