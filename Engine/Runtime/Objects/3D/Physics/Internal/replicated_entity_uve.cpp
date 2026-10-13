// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/replicated_entity_uve.h"

#include <memory>
#include <utility>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {
namespace {

/// The current world pose when there is one to take: a live entity, a world transform, and
/// a finite position. The sampler's seed and every resample agree on this.
[[nodiscard]] std::optional<Math::Vector3UVE> TrySampleWorldUVE(
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

Network::PositionSamplerUVE MakeReplicationSamplerUVE(const IEntityManagerUVE& entityManager,
                                                       const EntityUVE entity) {
    const std::optional<Math::Vector3UVE> seed = TrySampleWorldUVE(entityManager, entity);
    if (!seed.has_value()) {
        return Network::PositionSamplerUVE{};
    }
    // Shared so the closure keeps its last good pose across calls: a const-callable lambda
    // may still write through the pointer it captured by value.
    auto last = std::make_shared<Math::Vector3UVE>(*seed);
    return [&entityManager, entity, last]() {
        if (const std::optional<Math::Vector3UVE> fresh = TrySampleWorldUVE(entityManager, entity);
            fresh.has_value()) {
            *last = *fresh;
        }
        return *last;
    };
}

Network::PositionApplierUVE MakeReplicationApplierUVE(IEntityManagerUVE& entityManager,
                                                       ISceneGraphUVE& sceneGraph,
                                                       const EntityUVE entity) {
    return [&entityManager, &sceneGraph, entity](const Math::Vector3UVE& position) {
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<TransformComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
            return;
        }
        // Roots only: the wire carries a world pose and the write lands on the local
        // transform, which agree only without a parent. No hierarchy component means never
        // parented, which counts as root.
        if (entityManager.HasComponentUVE<HierarchyComponentUVE>(entity) &&
            entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent !=
                kInvalidEntityUVE) {
            return;
        }
        if (!Math::IsFiniteUVE(position)) {
            return;
        }
        TransformComponentUVE transform =
            entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        transform.localPosition = position;
        // Through the graph, not around it - the mover's own rule - with the immediate
        // UpdateUVE() that makes the new pose visible to the rest of the tick instead of
        // next frame's pass.
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    };
}

bool RegisterReplicatedEntityUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                                 Network::NetworkReplicatorUVE& replicator,
                                 ReplicatedEntityMapUVE& map, const EntityUVE entity,
                                 const Network::NetworkEntityIdUVE netId,
                                 const Network::ReplicationAuthorityUVE authority,
                                 Network::RelevancyCheckUVE relevancy) {
    if (!entityManager.IsAliveUVE(entity) || map.contains(netId) ||
        FindReplicatedNetIdUVE(map, entity).has_value()) {
        return false;
    }
    Network::PositionSamplerUVE sampler = MakeReplicationSamplerUVE(entityManager, entity);
    if (!sampler) {
        return false;
    }
    if (!replicator.RegisterEntityUVE(netId, std::move(sampler),
                                      MakeReplicationApplierUVE(entityManager, sceneGraph, entity),
                                      authority, std::move(relevancy))) {
        return false;
    }
    map.emplace(netId, entity);
    return true;
}

bool UnregisterReplicatedEntityUVE(Network::NetworkReplicatorUVE& replicator,
                                   ReplicatedEntityMapUVE& map, const EntityUVE entity) {
    const std::optional<Network::NetworkEntityIdUVE> netId = FindReplicatedNetIdUVE(map, entity);
    if (!netId.has_value()) {
        return false;
    }
    return UnregisterReplicatedEntityByIdUVE(replicator, map, *netId);
}

bool UnregisterReplicatedEntityByIdUVE(Network::NetworkReplicatorUVE& replicator,
                                       ReplicatedEntityMapUVE& map,
                                       const Network::NetworkEntityIdUVE netId) {
    const auto found = map.find(netId);
    if (found == map.end()) {
        return false;
    }
    static_cast<void>(replicator.UnregisterEntityUVE(netId));
    map.erase(found);
    return true;
}

EntityUVE FindReplicatedEntityUVE(const ReplicatedEntityMapUVE& map,
                                  const Network::NetworkEntityIdUVE netId) {
    const auto found = map.find(netId);
    return found == map.end() ? kInvalidEntityUVE : found->second;
}

std::optional<Network::NetworkEntityIdUVE> FindReplicatedNetIdUVE(
    const ReplicatedEntityMapUVE& map, const EntityUVE entity) {
    for (const auto& [netId, registered] : map) {
        if (registered == entity) {
            return netId;
        }
    }
    return std::nullopt;
}

std::size_t PruneReplicatedEntitiesUVE(const IEntityManagerUVE& entityManager,
                                       Network::NetworkReplicatorUVE& replicator,
                                       ReplicatedEntityMapUVE& map) {
    std::size_t pruned = 0U;
    for (auto entry = map.begin(); entry != map.end();) {
        const EntityUVE entity = entry->second;
        const bool servable = entityManager.IsAliveUVE(entity) &&
                              entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) &&
                              entityManager.HasComponentUVE<TransformComponentUVE>(entity);
        if (servable) {
            ++entry;
            continue;
        }
        static_cast<void>(replicator.UnregisterEntityUVE(entry->first));
        entry = map.erase(entry);
        ++pruned;
    }
    return pruned;
}

} // namespace UVE::Scene
