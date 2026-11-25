// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <vector>

#include "collision_pair.h"
#include "i_entity_manager.h"

namespace UVE::Physics {

/// ICollisionSystem is the spec's `CollisionSystem` (Part 7.5): pure collision *detection*
/// over every entity with both `Scene::WorldTransformComponent` and
/// `Scene::ColliderComponent`. The implementation uses a copied per-query median-split AABB BVH to
/// reduce broad-phase candidate work while preserving the legacy world-AABB narrow phase; the
/// detail-level DynamicAabbBvh policy also supports refit-only updates for moved copied proxies; each
/// returned pair remains oriented and ordered by the original cache encounter indices so
/// sequential resolution is unchanged. The legacy box overlap is exact, while sphere/capsule
/// descriptors use conservative AABB bounds in v1 and need a future exact narrow phase. Deliberately returns plain
/// `CollisionPair` values rather than mutating anything: resolving overlaps (deciding which
/// body moves, by how much) is `IPhysicsSystem`'s job, not this one's.
/// Thread-safety: the public collision service remains stateless (holding no members), matching
/// `CameraSystem`'s/`MeshRenderer`'s contract — every method only reads the
/// `IEntityManager` passed in.
class ICollisionSystem {
public:
    virtual ~ICollisionSystem() = default;

    /// Returns every overlapping pair of entities with both `WorldTransformComponent` and
    /// `ColliderComponent`, in a deterministic order matching the cache's encounter-index
    /// nested-pair order, independent of BVH traversal order — callers that resolve pairs
    /// sequentially can rely on this order being stable run-to-run given the same entity
    /// creation sequence.
    [[nodiscard]] virtual std::vector<CollisionPair> DetectCollisions(
        Scene::IEntityManager& entityManager) const = 0;
};

} // namespace UVE::Physics
