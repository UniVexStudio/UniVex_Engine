// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "marker_3d_query.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "world_transform_component.h"

namespace UVE::Scene {

namespace {

[[nodiscard]] bool SortsBeforeMarker3D(const Entity lhs, const Entity rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

/// Bounded, ordered insertion. The array always holds the first `kMaximumMarker3DQueryResults`
/// candidates in stable content order no matter what order the pool hands them over in: a
/// candidate that sorts before the current last one displaces it, and whatever falls off the end
/// sets `overflowed`.
void InsertOrdered(Marker3DQueryResults& results, Marker3DQueryResult candidate) {
    std::size_t slot = results.count;
    if (results.count == kMaximumMarker3DQueryResults) {
        if (!SortsBeforeMarker3D(candidate.entity, results.results[results.count - 1U].entity)) {
            results.overflowed = true;
            return;
        }
        slot = results.count - 1U;
        results.overflowed = true;
    } else {
        ++results.count;
    }

    while (slot > 0U && SortsBeforeMarker3D(candidate.entity, results.results[slot - 1U].entity)) {
        results.results[slot] = std::move(results.results[slot - 1U]);
        --slot;
    }
    results.results[slot] = std::move(candidate);
}

} // namespace

Marker3DQueryResults QueryMarkers3D(IEntityManager& entityManager, const Marker3DQuery& query) {
    Marker3DQueryResults results;

    entityManager.ForEach<Marker3DComponent>(
        [&entityManager, &query, &results](const Entity entity, Marker3DComponent& marker) {
            if (!Marker3D::IsQueryable(marker) ||
                !Marker3D::MatchesNameFilter(marker, query.name)) {
                return;
            }
            if (!entityManager.HasComponent<WorldTransformComponent>(entity)) {
                return;
            }

            const WorldTransformComponent& world =
                entityManager.GetComponent<WorldTransformComponent>(entity);
            const std::optional<Marker3DPose> pose =
                Marker3D::ResolveWorldPose(marker, world.worldPosition, world.worldRotation);
            if (!pose.has_value()) {
                return;
            }

            InsertOrdered(results, Marker3DQueryResult{entity, marker.markerName, *pose});
        });

    return results;
}

std::optional<Marker3DQueryResult> FindMarker3D(IEntityManager& entityManager,
                                                      const std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    Marker3DQuery query;
    query.name = std::string{name};
    const Marker3DQueryResults results = QueryMarkers3D(entityManager, query);
    const Marker3DQueryResult* const first = results.First();
    if (first == nullptr) {
        return std::nullopt;
    }
    return *first;
}

} // namespace UVE::Scene
