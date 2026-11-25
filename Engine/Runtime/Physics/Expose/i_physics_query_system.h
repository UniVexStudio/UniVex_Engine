// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include "area_overlap_system.h"
#include "character_controller.h"
#include "shape_cast_system.h"

namespace UVE::Physics {

/// Read-only and caller-command physics query boundary above the existing bounded algorithms.
/// This interface owns no ECS, collider, controller, or backend state; implementations only route
/// calls to the existing shape-cast, overlap, and character-controller authorities.
/// Thread-safety follows IEntityManager and ISceneGraph: calls are main-thread commands unless
/// the caller separately provides concurrent-read/mutation guarantees for those scene services.
class IPhysicsQuerySystem {
public:
    virtual ~IPhysicsQuerySystem() = default;

    [[nodiscard]] virtual std::optional<SphereCastHit> SphereCast(
        Scene::IEntityManager& entityManager, const SphereCastQuery& query) const = 0;
    [[nodiscard]] virtual std::optional<BoxCastHit> BoxCast(
        Scene::IEntityManager& entityManager, const BoxCastQuery& query) const = 0;
    [[nodiscard]] virtual std::optional<CapsuleCastHit> CapsuleCast(
        Scene::IEntityManager& entityManager, const CapsuleCastQuery& query) const = 0;
    [[nodiscard]] virtual AreaOverlapQueryResult QueryAreaOverlaps(
        Scene::IEntityManager& entityManager,
        std::size_t maximumResults = kMaximumAreaOverlapResults) const = 0;
    [[nodiscard]] virtual CharacterControllerMoveResult MoveCharacter(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        const CharacterControllerInput& input) const = 0;
    [[nodiscard]] virtual CharacterControllerMoveResult MoveCharacterWithToI(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        const CharacterControllerInput& input) const = 0;
};

} // namespace UVE::Physics
