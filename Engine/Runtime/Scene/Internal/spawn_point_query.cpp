// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spawn_point_query.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "world_transform_component.h"

namespace UVE::Scene {

namespace {

/// Bounded, ordered insertion. The array always holds the first `kMaximumSpawnPointQueryResults`
/// candidates in stable content order no matter what order the pool hands them over in: a
/// candidate that sorts before the current last one displaces it, and whatever falls off the end
/// sets `overflowed` - which is exactly "there were more points than this list can carry", without
/// the query ever needing to know its iteration order in advance.
void InsertOrdered(SpawnPoint3DQueryResults& results, SpawnPoint3DQueryResult candidate) {
    std::size_t slot = results.count;
    if (results.count == kMaximumSpawnPointQueryResults) {
        if (!SortsBeforeSpawnPoint(candidate.entity, results.results[results.count - 1U].entity)) {
            results.overflowed = true;
            return;
        }
        slot = results.count - 1U;
        results.overflowed = true;
    } else {
        ++results.count;
    }

    while (slot > 0U && SortsBeforeSpawnPoint(candidate.entity, results.results[slot - 1U].entity)) {
        results.results[slot] = std::move(results.results[slot - 1U]);
        --slot;
    }
    results.results[slot] = std::move(candidate);
}

} // namespace

SpawnPoint3DQueryResults QuerySpawnPoints(IEntityManager& entityManager,
                                                const SpawnPoint3DQuery& query) {
    SpawnPoint3DQueryResults results;

    entityManager.ForEach<SpawnPoint3DComponent>(
        [&entityManager, &query, &results](const Entity entity,
                                           SpawnPoint3DComponent& spawnPoint) {
            if (!spawnPoint.enabled || !IsSpawnPoint3DObjectComponentValid(spawnPoint)) {
                return;
            }
            if (!query.tag.empty() && spawnPoint.spawnTag != query.tag) {
                return;
            }
            if (!entityManager.HasComponent<WorldTransformComponent>(entity)) {
                return;
            }

            const WorldTransformComponent& world =
                entityManager.GetComponent<WorldTransformComponent>(entity);
            const std::optional<SpawnPoint3DPose> pose = ComposeSpawnPointPose(
                world.worldPosition, world.worldRotation, spawnPoint.localPosition,
                spawnPoint.localRotation);
            if (!pose.has_value()) {
                return;
            }

            InsertOrdered(results, SpawnPoint3DQueryResult{entity, spawnPoint.spawnTag,
                                                                *pose, spawnPoint.oneShot, false});
        });

    // Consumption runs over what was handed out, after the scan: the scan must not mutate the
    // component set it is walking, and a point that overflowed out of the list is not spent.
    if (query.consumeOneShot) {
        for (std::size_t index = 0U; index < results.count; ++index) {
            SpawnPoint3DQueryResult& result = results.results[index];
            if (!result.oneShot ||
                !entityManager.HasComponent<SpawnPoint3DComponent>(result.entity)) {
                continue;
            }
            entityManager.GetComponent<SpawnPoint3DComponent>(result.entity).enabled = false;
            result.consumed = true;
        }
    }
    return results;
}

bool ConsumeSpawnPoint(IEntityManager& entityManager, const Entity entity) {
    if (entity == kInvalidEntity ||
        !entityManager.HasComponent<SpawnPoint3DComponent>(entity)) {
        return false;
    }
    SpawnPoint3DComponent& spawnPoint =
        entityManager.GetComponent<SpawnPoint3DComponent>(entity);
    if (!spawnPoint.oneShot || !spawnPoint.enabled) {
        return false;
    }
    spawnPoint.enabled = false;
    return true;
}

} // namespace UVE::Scene
