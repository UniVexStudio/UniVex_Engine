// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/spawn_point_query_uve.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "uve/component/world_transform_component_uve.h"

namespace UVE::Scene {

namespace {

/// Bounded, ordered insertion. The array always holds the first `kMaximumSpawnPointQueryResultsUVE`
/// candidates in stable content order no matter what order the pool hands them over in: a
/// candidate that sorts before the current last one displaces it, and whatever falls off the end
/// sets `overflowed` - which is exactly "there were more points than this list can carry", without
/// the query ever needing to know its iteration order in advance.
void InsertOrderedUVE(SpawnPoint3DQueryResultsUVE& results, SpawnPoint3DQueryResultUVE candidate) {
    std::size_t slot = results.count;
    if (results.count == kMaximumSpawnPointQueryResultsUVE) {
        if (!SortsBeforeSpawnPointUVE(candidate.entity, results.results[results.count - 1U].entity)) {
            results.overflowed = true;
            return;
        }
        slot = results.count - 1U;
        results.overflowed = true;
    } else {
        ++results.count;
    }

    while (slot > 0U && SortsBeforeSpawnPointUVE(candidate.entity, results.results[slot - 1U].entity)) {
        results.results[slot] = std::move(results.results[slot - 1U]);
        --slot;
    }
    results.results[slot] = std::move(candidate);
}

} // namespace

SpawnPoint3DQueryResultsUVE QuerySpawnPointsUVE(IEntityManagerUVE& entityManager,
                                                const SpawnPoint3DQueryUVE& query) {
    SpawnPoint3DQueryResultsUVE results;

    entityManager.ForEachUVE<SpawnPoint3DComponentUVE>(
        [&entityManager, &query, &results](const EntityUVE entity,
                                           SpawnPoint3DComponentUVE& spawnPoint) {
            if (!spawnPoint.enabled || !IsSpawnPoint3DObjectComponentValidUVE(spawnPoint)) {
                return;
            }
            if (!query.tag.empty() && spawnPoint.spawnTag != query.tag) {
                return;
            }
            if (!entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                return;
            }

            const WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            const std::optional<SpawnPoint3DPoseUVE> pose = ComposeSpawnPointPoseUVE(
                world.worldPosition, world.worldRotation, spawnPoint.localPosition,
                spawnPoint.localRotation);
            if (!pose.has_value()) {
                return;
            }

            InsertOrderedUVE(results, SpawnPoint3DQueryResultUVE{entity, spawnPoint.spawnTag,
                                                                *pose, spawnPoint.oneShot, false});
        });

    // Consumption runs over what was handed out, after the scan: the scan must not mutate the
    // component set it is walking, and a point that overflowed out of the list is not spent.
    if (query.consumeOneShot) {
        for (std::size_t index = 0U; index < results.count; ++index) {
            SpawnPoint3DQueryResultUVE& result = results.results[index];
            if (!result.oneShot ||
                !entityManager.HasComponentUVE<SpawnPoint3DComponentUVE>(result.entity)) {
                continue;
            }
            entityManager.GetComponentUVE<SpawnPoint3DComponentUVE>(result.entity).enabled = false;
            result.consumed = true;
        }
    }
    return results;
}

bool ConsumeSpawnPointUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (entity == kInvalidEntityUVE ||
        !entityManager.HasComponentUVE<SpawnPoint3DComponentUVE>(entity)) {
        return false;
    }
    SpawnPoint3DComponentUVE& spawnPoint =
        entityManager.GetComponentUVE<SpawnPoint3DComponentUVE>(entity);
    if (!spawnPoint.oneShot || !spawnPoint.enabled) {
        return false;
    }
    spawnPoint.enabled = false;
    return true;
}

} // namespace UVE::Scene
