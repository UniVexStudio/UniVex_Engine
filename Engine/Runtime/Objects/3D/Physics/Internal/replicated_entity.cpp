// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "replicated_entity.h"

#include <memory>
#include <utility>

#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "vector3.h"

namespace UVE::Scene {
namespace {

/// The current world pose when there is one to take: a live entity, a world transform, and
/// a finite position. The sampler's seed and every resample agree on this.
[[nodiscard]] std::optional<Math::Vector3> TrySampleWorld(
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

Network::PositionSampler MakeReplicationSampler(const IEntityManager& entityManager,
                                                       const Entity entity) {
    const std::optional<Math::Vector3> seed = TrySampleWorld(entityManager, entity);
    if (!seed.has_value()) {
        return Network::PositionSampler{};
    }
    // Shared so the closure keeps its last good pose across calls: a const-callable lambda
    // may still write through the pointer it captured by value.
    auto last = std::make_shared<Math::Vector3>(*seed);
    return [&entityManager, entity, last]() {
        if (const std::optional<Math::Vector3> fresh = TrySampleWorld(entityManager, entity);
            fresh.has_value()) {
            *last = *fresh;
        }
        return *last;
    };
}

Network::PositionApplier MakeReplicationApplier(IEntityManager& entityManager,
                                                       ISceneGraph& sceneGraph,
                                                       const Entity entity) {
    return [&entityManager, &sceneGraph, entity](const Math::Vector3& position) {
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<TransformComponent>(entity) ||
            !entityManager.HasComponent<WorldTransformComponent>(entity)) {
            return;
        }
        // Roots only: the wire carries a world pose and the write lands on the local
        // transform, which agree only without a parent. No hierarchy component means never
        // parented, which counts as root.
        if (entityManager.HasComponent<HierarchyComponent>(entity) &&
            entityManager.GetComponent<HierarchyComponent>(entity).parent !=
                kInvalidEntity) {
            return;
        }
        if (!Math::IsFinite(position)) {
            return;
        }
        TransformComponent transform =
            entityManager.GetComponent<TransformComponent>(entity);
        transform.localPosition = position;
        // Through the graph, not around it - the mover's own rule - with the immediate
        // Update() that makes the new pose visible to the rest of the tick instead of
        // next frame's pass.
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    };
}

bool RegisterReplicatedEntity(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                 Network::NetworkReplicator& replicator,
                                 ReplicatedEntityMap& map, const Entity entity,
                                 const Network::NetworkEntityId netId,
                                 const Network::ReplicationAuthority authority,
                                 Network::RelevancyCheck relevancy) {
    if (!entityManager.IsAlive(entity) || map.contains(netId) ||
        FindReplicatedNetId(map, entity).has_value()) {
        return false;
    }
    Network::PositionSampler sampler = MakeReplicationSampler(entityManager, entity);
    if (!sampler) {
        return false;
    }
    if (!replicator.RegisterEntity(netId, std::move(sampler),
                                      MakeReplicationApplier(entityManager, sceneGraph, entity),
                                      authority, std::move(relevancy))) {
        return false;
    }
    map.emplace(netId, entity);
    return true;
}

bool UnregisterReplicatedEntity(Network::NetworkReplicator& replicator,
                                   ReplicatedEntityMap& map, const Entity entity) {
    const std::optional<Network::NetworkEntityId> netId = FindReplicatedNetId(map, entity);
    if (!netId.has_value()) {
        return false;
    }
    return UnregisterReplicatedEntityById(replicator, map, *netId);
}

bool UnregisterReplicatedEntityById(Network::NetworkReplicator& replicator,
                                       ReplicatedEntityMap& map,
                                       const Network::NetworkEntityId netId) {
    const auto found = map.find(netId);
    if (found == map.end()) {
        return false;
    }
    static_cast<void>(replicator.UnregisterEntity(netId));
    map.erase(found);
    return true;
}

Entity FindReplicatedEntity(const ReplicatedEntityMap& map,
                                  const Network::NetworkEntityId netId) {
    const auto found = map.find(netId);
    return found == map.end() ? kInvalidEntity : found->second;
}

std::optional<Network::NetworkEntityId> FindReplicatedNetId(
    const ReplicatedEntityMap& map, const Entity entity) {
    for (const auto& [netId, registered] : map) {
        if (registered == entity) {
            return netId;
        }
    }
    return std::nullopt;
}

std::size_t PruneReplicatedEntities(const IEntityManager& entityManager,
                                       Network::NetworkReplicator& replicator,
                                       ReplicatedEntityMap& map) {
    std::size_t pruned = 0U;
    for (auto entry = map.begin(); entry != map.end();) {
        const Entity entity = entry->second;
        const bool servable = entityManager.IsAlive(entity) &&
                              entityManager.HasComponent<WorldTransformComponent>(entity) &&
                              entityManager.HasComponent<TransformComponent>(entity);
        if (servable) {
            ++entry;
            continue;
        }
        static_cast<void>(replicator.UnregisterEntity(entry->first));
        entry = map.erase(entry);
        ++pruned;
    }
    return pruned;
}

} // namespace UVE::Scene
