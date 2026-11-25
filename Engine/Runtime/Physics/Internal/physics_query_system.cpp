// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "physics_query_system.h"

namespace UVE::Physics {

std::optional<SphereCastHit> PhysicsQuerySystem::SphereCast(
    Scene::IEntityManager& entityManager, const SphereCastQuery& query) const {
    return ShapeCastSystem::SphereCast(entityManager, query);
}

std::optional<BoxCastHit> PhysicsQuerySystem::BoxCast(
    Scene::IEntityManager& entityManager, const BoxCastQuery& query) const {
    return ShapeCastSystem::BoxCast(entityManager, query);
}

std::optional<CapsuleCastHit> PhysicsQuerySystem::CapsuleCast(
    Scene::IEntityManager& entityManager, const CapsuleCastQuery& query) const {
    return ShapeCastSystem::CapsuleCast(entityManager, query);
}

AreaOverlapQueryResult PhysicsQuerySystem::QueryAreaOverlaps(
    Scene::IEntityManager& entityManager, const std::size_t maximumResults) const {
    return AreaOverlapSystem::Query(entityManager, maximumResults);
}

CharacterControllerMoveResult PhysicsQuerySystem::MoveCharacter(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    const CharacterControllerInput& input) const {
    return CharacterController::Move(entityManager, sceneGraph, *m_collisionSystem, input);
}

CharacterControllerMoveResult PhysicsQuerySystem::MoveCharacterWithToI(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    const CharacterControllerInput& input) const {
    return CharacterController::MoveWithToI(entityManager, sceneGraph, *m_collisionSystem, input);
}

} // namespace UVE::Physics
