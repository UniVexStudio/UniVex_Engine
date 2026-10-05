// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/marker_3d_query_uve.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "uve/component/world_transform_component_uve.h"

namespace UVE::Scene {

namespace {

[[nodiscard]] bool SortsBeforeMarker3DUVE(const EntityUVE lhs, const EntityUVE rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

/// Bounded, ordered insertion. The array always holds the first `kMaximumMarker3DQueryResultsUVE`
/// candidates in stable content order no matter what order the pool hands them over in: a
/// candidate that sorts before the current last one displaces it, and whatever falls off the end
/// sets `overflowed`.
void InsertOrderedUVE(Marker3DQueryResultsUVE& results, Marker3DQueryResultUVE candidate) {
    std::size_t slot = results.count;
    if (results.count == kMaximumMarker3DQueryResultsUVE) {
        if (!SortsBeforeMarker3DUVE(candidate.entity, results.results[results.count - 1U].entity)) {
            results.overflowed = true;
            return;
        }
        slot = results.count - 1U;
        results.overflowed = true;
    } else {
        ++results.count;
    }

    while (slot > 0U && SortsBeforeMarker3DUVE(candidate.entity, results.results[slot - 1U].entity)) {
        results.results[slot] = std::move(results.results[slot - 1U]);
        --slot;
    }
    results.results[slot] = std::move(candidate);
}

} // namespace

Marker3DQueryResultsUVE QueryMarkers3DUVE(IEntityManagerUVE& entityManager, const Marker3DQueryUVE& query) {
    Marker3DQueryResultsUVE results;

    entityManager.ForEachUVE<Marker3DComponentUVE>(
        [&entityManager, &query, &results](const EntityUVE entity, Marker3DComponentUVE& marker) {
            if (!marker.enabled || !IsMarker3DObjectComponentValidUVE(marker)) {
                return;
            }
            if (!query.name.empty() && marker.markerName != query.name) {
                return;
            }
            if (!entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                return;
            }

            const WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            const std::optional<Marker3DPoseUVE> pose = ComposeMarker3DPoseUVE(
                world.worldPosition, world.worldRotation, marker.localPosition, marker.localRotation);
            if (!pose.has_value()) {
                return;
            }

            InsertOrderedUVE(results, Marker3DQueryResultUVE{entity, marker.markerName, *pose});
        });

    return results;
}

std::optional<Marker3DQueryResultUVE> FindMarker3DUVE(IEntityManagerUVE& entityManager,
                                                      const std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    Marker3DQueryUVE query;
    query.name = std::string{name};
    const Marker3DQueryResultsUVE results = QueryMarkers3DUVE(entityManager, query);
    const Marker3DQueryResultUVE* const first = results.FirstUVE();
    if (first == nullptr) {
        return std::nullopt;
    }
    return *first;
}

} // namespace UVE::Scene
