// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interest_relevancy.h"

#include <optional>

#include "world_transform_component.h"
#include "vector3.h"

namespace UVE::Scene {
namespace {

/// Mirror of the replication sampler's seed: a live entity, a world transform, a finite pose.
[[nodiscard]] std::optional<Math::Vector3> TryReadWorld(
    const IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3 position =
        entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    if (!Math::IsFinite(position)) {
        return std::nullopt;
    }
    return position;
}

} // namespace

Network::RelevancyCheck MakeInterestRelevancy(
    Network::NetworkInterest& interest, const Network::InterestViewerId viewer,
    const Network::NetworkEntityId netId) {
    return [&interest, viewer, netId] { return interest.IsRelevant(viewer, netId); };
}

std::size_t BindInterestRelevancy(Network::NetworkReplicator& replicator,
                                     Network::NetworkInterest& interest,
                                     const ReplicatedEntityMap& map,
                                     const Network::InterestViewerId viewer) {
    std::size_t bound = 0U;
    for (const auto& entry : map) {
        if (replicator.SetRelevancy(
                entry.first, MakeInterestRelevancy(interest, viewer, entry.first))) {
            ++bound;
        }
    }
    return bound;
}

std::size_t SyncInterestEntities(const IEntityManager& entityManager,
                                    Network::NetworkInterest& interest,
                                    const ReplicatedEntityMap& map) {
    std::size_t synced = 0U;
    for (const auto& entry : map) {
        const std::optional<Math::Vector3> position =
            TryReadWorld(entityManager, entry.second);
        if (!position.has_value()) {
            continue;
        }
        // Upsert: first sightings join as distance-culled; the already-known (including the
        // app's always-relevant pre-adds, whose flags this never touches) just move.
        if (interest.AddEntity(entry.first, *position) ||
            interest.UpdateEntity(entry.first, *position)) {
            ++synced;
        }
    }
    return synced;
}

bool SyncInterestViewer(const IEntityManager& entityManager,
                           Network::NetworkInterest& interest,
                           const Network::InterestViewerId viewer, const Entity entity) {
    const std::optional<Math::Vector3> position = TryReadWorld(entityManager, entity);
    if (!position.has_value()) {
        return false;
    }
    return interest.UpdateViewer(viewer, *position);
}

} // namespace UVE::Scene
