// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/detail/collider_world_aabb_cache_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/collider_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"

namespace UVE::Physics::Detail {
namespace {

[[nodiscard]] bool IsValidAabbUVE(const Math::AabbUVE& value) noexcept {
    return Math::IsFiniteUVE(value.min) && Math::IsFiniteUVE(value.max) && value.min.x <= value.max.x &&
           value.min.y <= value.max.y && value.min.z <= value.max.z;
}

} // namespace

std::vector<ColliderWorldAabbUVE> BuildColliderWorldAabbCacheUVE(Scene::IEntityManagerUVE& entityManager) {
    std::vector<ColliderWorldAabbUVE> cache;
    cache.reserve(std::min(entityManager.GetEntityCountUVE(), DynamicAabbBvhUVE::kMaximumProxiesUVE));
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::ColliderComponentUVE>(
        [&](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
                 const Scene::ColliderComponentUVE& collider) {
            if (cache.size() >= DynamicAabbBvhUVE::kMaximumProxiesUVE) {
                return;
            }
            // A physics object that has been taken out of the world is not here to be found by
            // anything: collisions, raycasts, shape casts and area overlaps all read this one
            // cache, so one answer covers every one of them.
            if (!Scene::IsPhysicsObjectInWorldUVE(entityManager, entity) ||
                !Scene::Collider3DUVE::IsParticipatingUVE(collider)) {
                return;
            }
            // The shape the mesh is actually drawn at. Everything below - broad-phase bounds,
            // narrow-phase extents, capsule segment - is read off this instead of the authored
            // collider, so one place decides what scale means and no consumer can disagree.
            const Scene::ColliderComponentUVE scaled =
                Scene::ScaleColliderUVE(collider, worldTransform.worldScale);
            const bool isSphere = scaled.shapeType == Scene::ColliderShapeTypeUVE::Sphere;
            const bool isCapsule = scaled.shapeType == Scene::ColliderShapeTypeUVE::Capsule;
            const Math::Vector3UVE localHalfExtents = Scene::GetColliderLocalHalfExtentsUVE(scaled);
            Math::QuaternionUVE shapeRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, shapeRotation)) {
                shapeRotation = {};
            }
            const float capsuleHalfSegment = isCapsule ? scaled.height * 0.5F - scaled.radius : 0.0F;
            const Math::Vector3UVE localCapsuleOffset{0.0F, capsuleHalfSegment, 0.0F};
            const Math::Vector3UVE worldCapsuleOffset =
                isCapsule ? Math::RotateVectorUVE(shapeRotation, localCapsuleOffset) : Math::Vector3UVE{};
            const Math::Vector3UVE capsuleSegmentStart = worldTransform.worldPosition - worldCapsuleOffset;
            const Math::Vector3UVE capsuleSegmentEnd = worldTransform.worldPosition + worldCapsuleOffset;
            const Math::Vector3UVE worldHalfExtents = isCapsule
                                                          ? Math::Vector3UVE{
                                                                scaled.radius + std::fabs(worldCapsuleOffset.x),
                                                                scaled.radius + std::fabs(worldCapsuleOffset.y),
                                                                scaled.radius + std::fabs(worldCapsuleOffset.z),
                                                            }
                                                          : isSphere
                                                                ? localHalfExtents
                                                                : Math::Vector3UVE{
                                                                      std::fabs(Math::RotateVectorUVE(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .x) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .x) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .x),
                                                                      std::fabs(Math::RotateVectorUVE(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .y) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .y) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .y),
                                                                      std::fabs(Math::RotateVectorUVE(
                                                                          shapeRotation, {localHalfExtents.x, 0.0F, 0.0F})
                                                                            .z) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, localHalfExtents.y, 0.0F})
                                                                                .z) +
                                                                          std::fabs(Math::RotateVectorUVE(
                                                                              shapeRotation, {0.0F, 0.0F, localHalfExtents.z})
                                                                                .z),
                                                                  };
            const Math::AabbUVE worldAabb =
                Math::AabbUVE::FromCenterExtentsUVE(worldTransform.worldPosition, worldHalfExtents);
            if (!IsValidAabbUVE(worldAabb)) {
                return;
            }
            cache.push_back(ColliderWorldAabbUVE{
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
                PhysicsMaterialUVE{scaled.friction, scaled.restitution, scaled.density},
            });
        });
    return cache;
}

} // namespace UVE::Physics::Detail
