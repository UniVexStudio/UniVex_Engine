// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_collision_system.h"

namespace UVE::Physics {

/// CollisionSystem is the concrete, engine-standard implementation of ICollisionSystem.
/// Deliberately stateless (no members, no PIMPL) — matches CameraSystem's/MeshRenderer's
/// precedent for utility services that always take the IEntityManager& they operate on as an
/// explicit parameter rather than owning a reference to one.
class CollisionSystem final : public ICollisionSystem {
public:
    [[nodiscard]] std::vector<CollisionPair> DetectCollisions(
        Scene::IEntityManager& entityManager) const override;
};

} // namespace UVE::Physics
