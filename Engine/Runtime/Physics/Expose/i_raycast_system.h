// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <optional>

#include "raycast_hit.h"
#include "raycast_query.h"
#include "i_entity_manager.h"

namespace UVE::Physics {

/// IRaycastSystem is the spec's `RaycastSystem` (Part 7.5). The point query routes known
/// Box/Sphere/Capsule colliders through exact bounded target geometry: oriented-box slab tests,
/// sphere quadratics, and finite capsule cylinder/cap tests. Unknown shape values retain the
/// conservative cached-AABB fallback. Shape casts remain a separate continuous-sweep API. The
/// interface is stateless and can later be replaced by a backend wrapper without call-site change.
/// Thread-safety: implementations should be stateless (no members), matching CollisionSystem's
/// contract — every method only reads the IEntityManager passed in; Raycast() performs no
/// mutation, so concurrent read-only calls are safe as long as the passed-in IEntityManager's
/// own concurrent-read guarantees hold (a pre-existing property, not something this interface
/// changes).
class IRaycastSystem {
public:
    virtual ~IRaycastSystem() = default;

    /// Returns the closest hit within `[0, query.maxDistance]`, or std::nullopt if nothing was
    /// hit. Only entities with both WorldTransformComponent and ColliderComponent are
    /// considered; `query.ignoreEntity` and every entity in `query.excludedEntities` are excluded
    /// outright (before the mask, so no layer can bring one back), `query.layerMask` filters via
    /// `(collider.collisionLayer & layerMask) != 0`. Ties (multiple entities at exactly the same
    /// distance) are broken deterministically: whichever entity ForEach's own deterministic
    /// chunk-order iteration encounters first wins — the same same-platform/same-build/
    /// same-input-sequence determinism scope PhysicsSystem (Increment 15) already documents,
    /// made an explicit contract here rather than an accidental byproduct.
    [[nodiscard]] virtual std::optional<RaycastHit> Raycast(Scene::IEntityManager& entityManager,
                                                                    const RaycastQuery& query) const = 0;
};

} // namespace UVE::Physics
