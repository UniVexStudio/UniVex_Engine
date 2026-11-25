// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the replicator wiring - the Gameplay adapter for entity replication. Each side
// registers the entities it replicates by stable net id; this file wires those registrations
// to the scene: samplers read world positions, appliers write local transforms through the
// scene graph, and the net-id map below is the local-entity mapping the core leaves outside
// Network. Rotation and scale never cross (positions only, like prediction); properties stay
// app-wired, with the recipe at the bottom of this note.
//
// The app owns one map per (entity manager, replicator) pair - two replicators sharing a
// manager need separate maps, because each replicator holds its own registrations - and
// calls Register/Unregister around the entity's replicated lifetime:
//
//   spawn:    RegisterReplicatedEntity(manager, graph, replicator, map, entity, netId,
//             authority) samples the spawn pose, wires both callbacks, and remembers the
//             mapping. Both sides agree net ids out of band (spawn order, level guids);
//             authority agrees out of band too (the peer never sees my roles).
//   despawn:  UnregisterReplicatedEntity() before destroying, or PruneReplicatedEntities()
//             sweeps the dead and the mutilated on a timer. The sampler holds its last good
//             pose for a dead entity (the peer holds still; no poison crosses), so a missed
//             unregister stalls visibly instead of corrupting.
//   pump:     replicator.Update() samples, sends, applies. Applies land through the graph
//             with the immediate Update() the mover's own rule demands, so the rest of the
//             tick reads the new poses.
//
// Lifetimes: the callbacks borrow the manager and the graph (both outlive the replicator, as
// documented on the factories); entities may die freely (see the despawn rule above). The map
// itself is a plain value - copyable, ownable, owing nothing.
//
// Authority shapes, and what to spawn for each: an Owner simulates locally and sends (a
// predicted pawn rides replication as Owner: TickPredictedPawn() moves it, replication
// carries the pose, inbound entries for it are ignored); a Proxy is a dumb doll (transform
// plus visuals, no mover components, nothing locally simulated to fight the applies); Shared
// does both (whoever's snapshot lands first wins the tick - move one side at a time or expect
// the documented last-writer flapping). Proxies are roots: Pack reads the world pose and a
// parented Owner samples fine, but applies land on the local transform and skip parented
// bodies rather than writing a world pose into a local slot.
//
// Relevancy is a plain predicate the app passes at registration (stored, live-rebindable via
// the core's SetRelevancy): `[&manager, npc, player]() { return Near(manager, npc,
// player); }` keeps the far world off the wire, and the interest core's grid answers the
// "near" question when distance alone is not enough. Properties ride the same session by
// (entity, property) id with app-chosen appliers - a health bar, a nameplate, a packed
// quaternion - through the core's RegisterProperty()/SetProperty() directly; positions
// are the adapter's job, values are the game's.
//

#include <cstddef>
#include <map>
#include <optional>

#include "entity.h"
#include "i_entity_manager.h"
#include "network_replication.h"
#include "i_scene_graph.h"

namespace UVE::Scene {

/// The local-entity mapping the core leaves outside Network: stable net id to entity handle,
/// one per (manager, replicator) pair, owned by the app. Handles may dangle (despawn without
/// unregister); PruneReplicatedEntities sweeps them.
using ReplicatedEntityMap = std::map<Network::NetworkEntityId, Entity>;

/// Samples `entity`'s world position, holding the last good pose when the entity dies, loses
/// its world transform, or goes non-finite mid-stream (the peer holds still; delta sees
/// "unchanged" and stays quiet). Empty when there is nothing to sample at factory time - a
/// dead entity, a missing world transform, a non-finite pose - so Register refuses instead
/// of seeding poison. Borrows the manager: it must outlive the replicator holding this.
[[nodiscard]] Network::PositionSampler MakeReplicationSampler(
    const IEntityManager& entityManager, Entity entity);

/// Applies received positions to `entity`'s local transform through the scene graph, with the
/// immediate Update() that makes each new pose visible to the rest of the tick. Infallible
/// as a factory - every refusal happens per call, silently: a dead entity, a missing local or
/// world transform, a parented body, or a non-finite incoming pose each skip the write and
/// leave the entity exactly where it was. Borrows the manager and the graph: both must
/// outlive the replicator holding this.
[[nodiscard]] Network::PositionApplier MakeReplicationApplier(
    IEntityManager& entityManager, ISceneGraph& sceneGraph, Entity entity);

/// Wires `entity` into the exchange under `netId`: samples the spawn pose, registers both
/// callbacks with the replicator, and remembers the mapping. False - registering nothing -
/// for a dead entity, an entity or id already in the map, an unsampleable pose, or a core
/// refusal (unregister first to rebind, here as in the core). The relevancy predicate runs
/// on the send tick for sending roles; empty means always relevant.
bool RegisterReplicatedEntity(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                 Network::NetworkReplicator& replicator,
                                 ReplicatedEntityMap& map, Entity entity,
                                 Network::NetworkEntityId netId,
                                 Network::ReplicationAuthority authority,
                                 Network::RelevancyCheck relevancy = Network::RelevancyCheck{});

/// Drops `entity`'s registration and forgets the mapping. True when the map held the entity
/// (the core call is best-effort: a direct core unregister first still cleans the map).
bool UnregisterReplicatedEntity(Network::NetworkReplicator& replicator,
                                   ReplicatedEntityMap& map, Entity entity);

/// Drops `netId`'s registration and forgets the mapping: the ownership-transfer answerer's
/// unregister, since handoffs speak net ids, not entities. True when the map held the id.
bool UnregisterReplicatedEntityById(Network::NetworkReplicator& replicator,
                                       ReplicatedEntityMap& map,
                                       Network::NetworkEntityId netId);

/// The entity registered under `netId`, or invalid when the map holds no such id. The cold
/// path behind transfer decisions and property routing; the hot paths capture entities
/// directly and never look up.
[[nodiscard]] Entity FindReplicatedEntity(const ReplicatedEntityMap& map,
                                                Network::NetworkEntityId netId);

/// The net id `entity` is registered under, or nothing. A reverse scan for a cold path.
[[nodiscard]] std::optional<Network::NetworkEntityId> FindReplicatedNetId(
    const ReplicatedEntityMap& map, Entity entity);

/// Sweeps registrations that can no longer serve: dead entities and bodies missing their
/// local or world transform. Unregisters each from the core, forgets each from the map, and
/// answers how many went. The despawn-without-unregister backstop; the disciplined app calls
/// Unregister first and sees zero here.
std::size_t PruneReplicatedEntities(const IEntityManager& entityManager,
                                       Network::NetworkReplicator& replicator,
                                       ReplicatedEntityMap& map);

} // namespace UVE::Scene
