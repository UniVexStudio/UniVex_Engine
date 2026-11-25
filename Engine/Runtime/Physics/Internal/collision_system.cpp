// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collision_system.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "aabb.h"
#include "collider_world_aabb_cache.h"
#include "shape_narrow_phase.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] bool LayersAcceptPair(const Detail::ColliderWorldAabb& first,
                                       const Detail::ColliderWorldAabb& second) noexcept {
    return (first.collisionMask & second.collisionLayer) != 0U &&
           (second.collisionMask & first.collisionLayer) != 0U;
}

[[nodiscard]] std::optional<Math::Penetration> ComputePairPenetration(
    const Detail::ColliderWorldAabb& first, const Detail::ColliderWorldAabb& second) {
    if (first.shapeType == Scene::ColliderShapeType::Sphere &&
        second.shapeType == Scene::ColliderShapeType::Sphere) {
        return Detail::ComputeSphereSpherePenetration(
            first.worldAabb.GetCenter(), first.shapeRadius, second.worldAabb.GetCenter(),
            second.shapeRadius);
    }
    if (first.shapeType == Scene::ColliderShapeType::Sphere &&
        second.shapeType == Scene::ColliderShapeType::Box) {
        return Detail::ComputeSphereOrientedBoxPenetration(
            second.worldAabb.GetCenter(), second.shapeHalfExtents, second.shapeRotation,
            first.worldAabb.GetCenter(), first.shapeRadius);
    }
    if (first.shapeType == Scene::ColliderShapeType::Box &&
        second.shapeType == Scene::ColliderShapeType::Sphere) {
        const std::optional<Math::Penetration> sphereToBox =
            Detail::ComputeSphereOrientedBoxPenetration(
                first.worldAabb.GetCenter(), first.shapeHalfExtents, first.shapeRotation,
                second.worldAabb.GetCenter(), second.shapeRadius);
        if (!sphereToBox.has_value()) {
            return std::nullopt;
        }
        return Math::Penetration{-sphereToBox->axis, sphereToBox->depth};
    }
    if (first.shapeType == Scene::ColliderShapeType::Capsule &&
        second.shapeType == Scene::ColliderShapeType::Capsule) {
        return Detail::ComputeCapsuleCapsulePenetration(
            first.shapeSegmentStart, first.shapeSegmentEnd, first.shapeRadius,
            second.shapeSegmentStart, second.shapeSegmentEnd, second.shapeRadius);
    }
    if (first.shapeType == Scene::ColliderShapeType::Capsule &&
        second.shapeType == Scene::ColliderShapeType::Sphere) {
        return Detail::ComputeCapsuleSpherePenetration(
            first.shapeSegmentStart, first.shapeSegmentEnd, first.shapeRadius,
            second.worldAabb.GetCenter(), second.shapeRadius);
    }
    if (first.shapeType == Scene::ColliderShapeType::Sphere &&
        second.shapeType == Scene::ColliderShapeType::Capsule) {
        const std::optional<Math::Penetration> capsuleToSphere =
            Detail::ComputeCapsuleSpherePenetration(
                second.shapeSegmentStart, second.shapeSegmentEnd, second.shapeRadius,
                first.worldAabb.GetCenter(), first.shapeRadius);
        if (!capsuleToSphere.has_value()) {
            return std::nullopt;
        }
        return Math::Penetration{-capsuleToSphere->axis, capsuleToSphere->depth};
    }
    if (first.shapeType == Scene::ColliderShapeType::Capsule &&
        second.shapeType == Scene::ColliderShapeType::Box) {
        return Detail::ComputeCapsuleOrientedBoxPenetration(
            second.worldAabb.GetCenter(), second.shapeHalfExtents, second.shapeRotation,
            first.shapeSegmentStart, first.shapeSegmentEnd, first.shapeRadius);
    }
    if (first.shapeType == Scene::ColliderShapeType::Box &&
        second.shapeType == Scene::ColliderShapeType::Capsule) {
        const std::optional<Math::Penetration> capsuleToBox =
            Detail::ComputeCapsuleOrientedBoxPenetration(
                first.worldAabb.GetCenter(), first.shapeHalfExtents, first.shapeRotation,
                second.shapeSegmentStart, second.shapeSegmentEnd, second.shapeRadius);
        if (!capsuleToBox.has_value()) {
            return std::nullopt;
        }
        return Math::Penetration{-capsuleToBox->axis, capsuleToBox->depth};
    }
    if (first.shapeType == Scene::ColliderShapeType::Box &&
        second.shapeType == Scene::ColliderShapeType::Box) {
        return Detail::ComputeOrientedBoxOrientedBoxPenetration(
            first.worldAabb.GetCenter(), first.shapeHalfExtents, first.shapeRotation,
            second.worldAabb.GetCenter(), second.shapeHalfExtents, second.shapeRotation);
    }
    return Math::ComputePenetration(first.worldAabb, second.worldAabb);
}

void AppendPairIfOverlapping(const std::vector<Detail::ColliderWorldAabb>& colliders,
                                const std::size_t firstIndex, const std::size_t secondIndex,
                                std::vector<CollisionPair>& pairs) {
    const Detail::ColliderWorldAabb& first = colliders[firstIndex];
    const Detail::ColliderWorldAabb& second = colliders[secondIndex];
    if (!LayersAcceptPair(first, second)) {
        return;
    }

    const std::optional<Math::Penetration> penetration = ComputePairPenetration(first, second);
    if (penetration.has_value()) {
        pairs.push_back(CollisionPair{first.entity, second.entity, penetration->axis, penetration->depth});
    }
}

} // namespace

std::vector<CollisionPair> CollisionSystem::DetectCollisions(Scene::IEntityManager& entityManager) const {
    Detail::DynamicAabbBvh bvh(Detail::BuildColliderWorldAabbCache(entityManager));
    const std::vector<Detail::ColliderWorldAabb>& colliders = bvh.GetColliders();

    std::vector<CollisionPair> pairs;
    std::vector<std::size_t> candidates;
    for (std::size_t firstIndex = 0U; firstIndex < colliders.size(); ++firstIndex) {
        if (!bvh.Query(colliders[firstIndex].worldAabb, candidates)) {
            for (std::size_t secondIndex = firstIndex + 1U; secondIndex < colliders.size(); ++secondIndex) {
                AppendPairIfOverlapping(colliders, firstIndex, secondIndex, pairs);
            }
            continue;
        }

        std::sort(candidates.begin(), candidates.end());
        for (const std::size_t secondIndex : candidates) {
            if (secondIndex > firstIndex) {
                AppendPairIfOverlapping(colliders, firstIndex, secondIndex, pairs);
            }
        }
    }
    return pairs;
}

} // namespace UVE::Physics

// EOF
