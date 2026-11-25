// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "collider_world_aabb_cache.h"

#include <algorithm>
#include <cmath>

#include "collider_component.h"
#include "world_transform_component.h"
#include "vector3.h"
#include "abstract_physics_objects_3d.h"
#include "collider_3d.h"

namespace UVE::Physics::Detail {
namespace {

[[nodiscard]] bool IsValidAabb(const Math::Aabb& value) noexcept {
    return Math::IsFinite(value.min) && Math::IsFinite(value.max) && value.min.x <= value.max.x &&
           value.min.y <= value.max.y && value.min.z <= value.max.z;
}

} // namespace

std::vector<ColliderWorldAabb> BuildColliderWorldAabbCache(Scene::IEntityManager& entityManager) {
    std::vector<ColliderWorldAabb> cache;
    cache.reserve(std::min(entityManager.GetEntityCount(), DynamicAabbBvh::kMaximumProxies));
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::ColliderComponent>(
        [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                 const Scene::ColliderComponent& collider) {
            if (cache.size() >= DynamicAabbBvh::kMaximumProxies) {
                return;
            }
            // A physics object that has been taken out of the world is not here to be found by
            // anything: collisions, raycasts, shape casts and area overlaps all read this one
            // cache, so one answer covers every one of them.
            if (!Scene::IsPhysicsObjectInWorld(entityManager, entity) ||
                !Scene::Collider3D::IsParticipating(collider)) {
                return;
            }
            // The shape the mesh is actually drawn at. Everything below - broad-phase bounds,
            // narrow-phase extents, capsule segment - is read off this instead of the authored
            // collider, so one place decides what scale means and no consumer can disagree.
            const Scene::ColliderComponent scaled =
                Scene::ScaleCollider(collider, worldTransform.worldScale);
            const bool isSphere = scaled.shapeType == Scene::ColliderShapeType::Sphere;
            const bool isCapsule = scaled.shapeType == Scene::ColliderShapeType::Capsule;
            const Math::Vector3 localHalfExtents = Scene::GetColliderLocalHalfExtents(scaled);
            Math::Quaternion shapeRotation{};
            if (!Math::TryNormalize(worldTransform.worldRotation, shapeRotation)) {
                shapeRotation = {};
            }
            const float capsuleHalfSegment = isCapsule ? scaled.height * 0.5F - scaled.radius : 0.0F;
            const Math::Vector3 localCapsuleOffset{0.0F, capsuleHalfSegment, 0.0F};
            const Math::Vector3 worldCapsuleOffset =
                isCapsule ? Math::RotateVector(shapeRotation, localCapsuleOffset) : Math::Vector3{};
            const Math::Vector3 capsuleSegmentStart = worldTransform.worldPosition - worldCapsuleOffset;
            const Math::Vector3 capsuleSegmentEnd = worldTransform.worldPosition + worldCapsuleOffset;
            const Math::Vector3 worldHalfExtents = isCapsule
                                                          ? Math::Vector3{
                                                                scaled.radius + std::fabs(worldCapsuleOffset.x),
                                                                scaled.radius + std::fabs(worldCapsuleOffset.y),
                                                                scaled.radius + std::fabs(worldCapsuleOffset.z),
                                                            }
                                                          : isSphere
                                                                ? localHalfExtents
                                                                : Math::Vector3{
                                                                      std::fabs(Math::RotateVector(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .x) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .x) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .x),
                                                                      std::fabs(Math::RotateVector(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .y) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .y) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .y),
                                                                      std::fabs(Math::RotateVector(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .z) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .z) +
                                                                          std::fabs(Math::RotateVector(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .z),
                                                                  };
            const Math::Aabb worldAabb =
                Math::Aabb::FromCenterExtents(worldTransform.worldPosition, worldHalfExtents);
            if (!IsValidAabb(worldAabb)) {
                return;
            }
            cache.push_back(ColliderWorldAabb{
                entity,
                worldAabb,
                scaled.collisionLayer,
                scaled.collisionMask,
                scaled.shapeType,
                (isSphere || isCapsule) ? scaled.radius : 0.0F,
                localHalfExtents,
                shapeRotation,
                capsuleSegmentStart,
                capsuleSegmentEnd,
                PhysicsMaterial{scaled.friction, scaled.restitution, scaled.density},
            });
        });
    return cache;
}

} // namespace UVE::Physics::Detail
