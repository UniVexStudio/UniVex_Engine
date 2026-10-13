// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/interest_relevancy_uve.h"

#include <optional>

#include "uve/component/world_transform_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {
namespace {

/// Mirror of the replication sampler's seed: a live entity, a world transform, a finite pose.
[[nodiscard]] std::optional<Math::Vector3UVE> TryReadWorldUVE(
    const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Math::Vector3UVE position =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    if (!Math::IsFiniteUVE(position)) {
        return std::nullopt;
    }
    return position;
}

} // namespace

Network::RelevancyCheckUVE MakeInterestRelevancyUVE(
    Network::NetworkInterestUVE& interest, const Network::InterestViewerIdUVE viewer,
    const Network::NetworkEntityIdUVE netId) {
    return [&interest, viewer, netId] { return interest.IsRelevantUVE(viewer, netId); };
}

std::size_t BindInterestRelevancyUVE(Network::NetworkReplicatorUVE& replicator,
                                     Network::NetworkInterestUVE& interest,
                                     const ReplicatedEntityMapUVE& map,
                                     const Network::InterestViewerIdUVE viewer) {
    std::size_t bound = 0U;
    for (const auto& entry : map) {
        if (replicator.SetRelevancyUVE(
                entry.first, MakeInterestRelevancyUVE(interest, viewer, entry.first))) {
            ++bound;
        }
    }
    return bound;
}

std::size_t SyncInterestEntitiesUVE(const IEntityManagerUVE& entityManager,
                                    Network::NetworkInterestUVE& interest,
                                    const ReplicatedEntityMapUVE& map) {
    std::size_t synced = 0U;
    for (const auto& entry : map) {
        const std::optional<Math::Vector3UVE> position =
            TryReadWorldUVE(entityManager, entry.second);
        if (!position.has_value()) {
            continue;
        }
        // Upsert: first sightings join as distance-culled; the already-known (including the
        // app's always-relevant pre-adds, whose flags this never touches) just move.
        if (interest.AddEntityUVE(entry.first, *position) ||
            interest.UpdateEntityUVE(entry.first, *position)) {
            ++synced;
        }
    }
    return synced;
}

bool SyncInterestViewerUVE(const IEntityManagerUVE& entityManager,
                           Network::NetworkInterestUVE& interest,
                           const Network::InterestViewerIdUVE viewer, const EntityUVE entity) {
    const std::optional<Math::Vector3UVE> position = TryReadWorldUVE(entityManager, entity);
    if (!position.has_value()) {
        return false;
    }
    return interest.UpdateViewerUVE(viewer, *position);
}

} // namespace UVE::Scene
