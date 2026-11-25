// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "i_collision_system.h"
#include "i_physics_query_system.h"

namespace UVE::Physics {

/// Engine-composed implementation of IPhysicsQuerySystem. It is stateless apart from a
/// non-owning collision-system dependency used by CharacterController movement commands.
class PhysicsQuerySystem final : public IPhysicsQuerySystem {
public:
    explicit PhysicsQuerySystem(ICollisionSystem& collisionSystem) noexcept
        : m_collisionSystem(&collisionSystem) {}

    [[nodiscard]] std::optional<SphereCastHit> SphereCast(
        Scene::IEntityManager& entityManager, const SphereCastQuery& query) const override;
    [[nodiscard]] std::optional<BoxCastHit> BoxCast(
        Scene::IEntityManager& entityManager, const BoxCastQuery& query) const override;
    [[nodiscard]] std::optional<CapsuleCastHit> CapsuleCast(
        Scene::IEntityManager& entityManager, const CapsuleCastQuery& query) const override;
    [[nodiscard]] AreaOverlapQueryResult QueryAreaOverlaps(
        Scene::IEntityManager& entityManager,
        std::size_t maximumResults = kMaximumAreaOverlapResults) const override;
    [[nodiscard]] CharacterControllerMoveResult MoveCharacter(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        const CharacterControllerInput& input) const override;
    [[nodiscard]] CharacterControllerMoveResult MoveCharacterWithToI(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        const CharacterControllerInput& input) const override;

private:
    ICollisionSystem* m_collisionSystem;
};

} // namespace UVE::Physics
