// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shape_cast_system.h"

#include <cmath>
#include <limits>
#include <vector>

#include "collider_component.h"
#include "i_entity_manager.h"
#include "aabb.h"
#include "vector3.h"
#include "collider_world_aabb_cache.h"
#include "shape_narrow_phase.h"
#include "physics_material.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] bool IsValidBoxCastRay(const Math::Ray& ray) noexcept {
    const float lengthSquared = Math::LengthSquared(ray.direction);
    return Math::IsFinite(ray.origin) && Math::IsFinite(ray.direction) &&
           std::isfinite(lengthSquared) && lengthSquared > 0.0F;
}

[[nodiscard]] bool IsFiniteAabb(const Math::Aabb& aabb) noexcept {
    return Math::IsFinite(aabb.min) && Math::IsFinite(aabb.max) &&
           aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
}

} // namespace

std::optional<SphereCastHit> ShapeCastSystem::SphereCast(
    Scene::IEntityManager& entityManager, const SphereCastQuery& query) {
    if (!std::isfinite(query.radius) || query.radius < 0.0F || !std::isfinite(query.maxDistance) ||
        query.maxDistance < 0.0F || query.layerMask == 0U) {
        return std::nullopt;
    }

    const std::vector<Detail::ColliderWorldAabb> colliders =
        Detail::BuildColliderWorldAabbCache(entityManager);
    std::optional<SphereCastHit> closestHit;
    for (const Detail::ColliderWorldAabb& collider : colliders) {
        if (collider.entity == query.ignoreEntity || collider.entity == query.alsoIgnoreEntity ||
            (collider.collisionLayer & query.layerMask) == 0U) {
            continue;
        }

        std::optional<Math::RayHit> hit;
        if (collider.shapeType == Scene::ColliderShapeType::Sphere) {
            hit = Detail::IntersectMovingSphereSphere(
                query.ray, query.radius,
                Math::Sphere{collider.worldAabb.GetCenter(), collider.shapeRadius}, query.maxDistance);
        } else if (collider.shapeType == Scene::ColliderShapeType::Capsule) {
            const float expandedRadius = query.radius + collider.shapeRadius;
            if (std::isfinite(expandedRadius) && expandedRadius >= 0.0F) {
                hit = Detail::IntersectRayCapsule(query.ray, collider.shapeSegmentStart, collider.shapeSegmentEnd,
                                                     expandedRadius, query.maxDistance);
            }
        } else {
            const Math::Vector3 radiusExtents{query.radius, query.radius, query.radius};
            const Math::Aabb expandedAabb{
                collider.worldAabb.min - radiusExtents,
                collider.worldAabb.max + radiusExtents,
            };
            if (!IsFiniteAabb(expandedAabb)) {
                continue;
            }
            hit = Math::IntersectRay(query.ray, expandedAabb, query.maxDistance);
        }
        if (!hit.has_value() || (closestHit.has_value() && hit->distance >= closestHit->distance)) {
            continue;
        }

        const Scene::ColliderComponent& colliderComponent =
            entityManager.GetComponent<Scene::ColliderComponent>(collider.entity);
        closestHit = SphereCastHit{collider.entity,
                                      query.ray.origin + query.ray.direction * hit->distance,
                                      hit->normal,
                                      hit->distance,
                                      MaterialOf(colliderComponent)};
    }
    return closestHit;
}

std::optional<CapsuleCastHit> ShapeCastSystem::CapsuleCast(
    Scene::IEntityManager& entityManager, const CapsuleCastQuery& query) {
    if (!IsValidBoxCastRay(query.ray) || !std::isfinite(query.radius) || query.radius <= 0.0F ||
        !std::isfinite(query.height) || query.height < 2.0F * query.radius ||
        !std::isfinite(query.maxDistance) || query.maxDistance < 0.0F || query.layerMask == 0U) {
        return std::nullopt;
    }

    const Math::Vector3 halfExtents{query.radius, query.height * 0.5F, query.radius};
    if (!Math::IsFinite(halfExtents)) {
        return std::nullopt;
    }
    const std::vector<Detail::ColliderWorldAabb> colliders =
        Detail::BuildColliderWorldAabbCache(entityManager);
    std::optional<CapsuleCastHit> closestHit;
    for (const Detail::ColliderWorldAabb& collider : colliders) {
        if (collider.entity == query.ignoreEntity || (collider.collisionLayer & query.layerMask) == 0U) {
            continue;
        }
        const Math::Aabb expandedAabb{
            collider.worldAabb.min - halfExtents,
            collider.worldAabb.max + halfExtents,
        };
        if (!IsFiniteAabb(expandedAabb)) {
            continue;
        }
        const std::optional<Math::RayHit> hit =
            Math::IntersectRay(query.ray, expandedAabb, query.maxDistance);
        if (!hit.has_value()) {
            continue;
        }
        const bool closer = !closestHit.has_value() || hit->distance < closestHit->distance;
        const bool tied = closestHit.has_value() && hit->distance == closestHit->distance &&
                          collider.entity.index < closestHit->entity.index;
        if (!closer && !tied) {
            continue;
        }
        const Scene::ColliderComponent& colliderComponent =
            entityManager.GetComponent<Scene::ColliderComponent>(collider.entity);
        closestHit = CapsuleCastHit{collider.entity,
                                       query.ray.origin + query.ray.direction * hit->distance,
                                       hit->normal,
                                       hit->distance,
                                       MaterialOf(colliderComponent)};
    }
    return closestHit;
}

std::optional<BoxCastHit> ShapeCastSystem::BoxCast(
    Scene::IEntityManager& entityManager, const BoxCastQuery& query) {
    if (!IsValidBoxCastRay(query.ray) || !Math::IsFinite(query.halfExtents) ||
        query.halfExtents.x < 0.0F || query.halfExtents.y < 0.0F || query.halfExtents.z < 0.0F ||
        !std::isfinite(query.maxDistance) || query.maxDistance < 0.0F || query.layerMask == 0U) {
        return std::nullopt;
    }

    const std::vector<Detail::ColliderWorldAabb> colliders =
        Detail::BuildColliderWorldAabbCache(entityManager);
    std::optional<BoxCastHit> closestHit;
    for (const Detail::ColliderWorldAabb& collider : colliders) {
        if (collider.entity == query.ignoreEntity || (collider.collisionLayer & query.layerMask) == 0U) {
            continue;
        }

        const Math::Aabb expandedAabb{
            collider.worldAabb.min - query.halfExtents,
            collider.worldAabb.max + query.halfExtents,
        };
        if (!IsFiniteAabb(expandedAabb)) {
            continue;
        }
        const std::optional<Math::RayHit> hit =
            Math::IntersectRay(query.ray, expandedAabb, query.maxDistance);
        if (!hit.has_value()) {
            continue;
        }
        const bool closer = !closestHit.has_value() || hit->distance < closestHit->distance;
        const bool tied = closestHit.has_value() && hit->distance == closestHit->distance &&
                          collider.entity.index < closestHit->entity.index;
        if (!closer && !tied) {
            continue;
        }

        const Scene::ColliderComponent& colliderComponent =
            entityManager.GetComponent<Scene::ColliderComponent>(collider.entity);
        closestHit = BoxCastHit{collider.entity,
                                   query.ray.origin + query.ray.direction * hit->distance,
                                   hit->normal,
                                   hit->distance,
                                   MaterialOf(colliderComponent)};
    }
    return closestHit;
}

} // namespace UVE::Physics
