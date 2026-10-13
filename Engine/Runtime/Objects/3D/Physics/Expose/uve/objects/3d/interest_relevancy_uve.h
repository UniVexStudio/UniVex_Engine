// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the interest wiring - the Gameplay adapter for relevancy. The interest core judges
// (viewer, net id) pairs from positions it is pushed; this file pushes scene positions into
// it and binds its verdicts back onto the replicator's per-entity relevancy filters, so
// distance gates the wire with no per-entity boilerplate:
//
//   spawn:    RegisterReplicatedEntityUVE() as usual, then BindInterestRelevancyUVE() once
//             per viewer after a spawn burst (idempotent - rebinding swaps in an equivalent
//             check, so per-spawn binding is fine too).
//   pump:     SyncInterestEntitiesUVE() moves every mapped entity's world pose into interest,
//             SyncInterestViewerUVE() moves the viewer onto its pawn, then replicator.
//             UpdateUVE() samples and sends only what the viewer needs. Irrelevant entities
//             skip sampling and sending (an all-irrelevant tick still emits the keepalive).
//   despawn:  UnregisterReplicatedEntityUVE() plus interest.RemoveEntityUVE() together; a
//             forgotten interest record is inert (its entity no longer sends) but counts
//             toward EntityCountUVE until removed.
//
// Lifetimes: the checks borrow the interest core (it must outlive every replicator holding
// one); the syncs borrow nothing. Viewers are the app's: SyncInterestViewerUVE() only moves
// a viewer the app added with its radius - radii are policy, positions are plumbing.
// Always-relevant entities are the app's too: pre-add them with the flag and the sync's
// upsert keeps them (it never flips flags); everything the sync adds itself culls by
// distance. Tiers and rates are policy the same way: the app assigns them, the bound
// checks throttle automatically, and the sync's upserts never touch them.
//

#include <cstddef>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/network/network_interest_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/objects/3d/replicated_entity_uve.h"

namespace UVE::Scene {

/// The relevancy check for one (viewer, net id) pair: the interest verdict, fail-open for
/// unknown ids exactly like the core. Borrows the interest core: it must outlive the
/// replicator holding this.
[[nodiscard]] Network::RelevancyCheckUVE MakeInterestRelevancyUVE(
    Network::NetworkInterestUVE& interest, Network::InterestViewerIdUVE viewer,
    Network::NetworkEntityIdUVE netId);

/// Binds every mapped entity's relevancy filter to the viewer's interest verdicts (live via
/// the core's SetRelevancyUVE, so already-bound ids just swap in an equivalent check), and
/// answers how many bound. A short count means map/core drift - an id the map holds but the
/// replicator no longer does - so prune first when the map may be stale.
std::size_t BindInterestRelevancyUVE(Network::NetworkReplicatorUVE& replicator,
                                     Network::NetworkInterestUVE& interest,
                                     const ReplicatedEntityMapUVE& map,
                                     Network::InterestViewerIdUVE viewer);

/// Pushes every mapped entity's world pose into interest (adding first sightings as
/// distance-culled, updating the rest; never flipping always-relevant flags), and answers
/// how many landed. Dead entities, missing world transforms, non-finite poses, and poses
/// outside the interest map box skip silently - the prune timer's job, not this pump's.
std::size_t SyncInterestEntitiesUVE(const IEntityManagerUVE& entityManager,
                                    Network::NetworkInterestUVE& interest,
                                    const ReplicatedEntityMapUVE& map);

/// Moves `viewer` onto `entity`'s world pose. False - moving nothing - for a dead entity, a
/// missing world transform, a non-finite pose, or an unknown viewer; viewers are added with
/// their radius by the app, never here.
bool SyncInterestViewerUVE(const IEntityManagerUVE& entityManager,
                           Network::NetworkInterestUVE& interest,
                           Network::InterestViewerIdUVE viewer, EntityUVE entity);

} // namespace UVE::Scene
