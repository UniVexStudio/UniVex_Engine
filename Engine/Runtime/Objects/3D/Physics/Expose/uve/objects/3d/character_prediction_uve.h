// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the production Gameplay adapter for client-side prediction - component <->
// PredictedStateUVE, named as a follow-up in network_prediction_uve.h. A predicted pawn is an
// ordinary Character3D (transform, controller, pawn, possession) whose fixed step runs through
// a NetworkPredictionUVE instead of the local mover: the pawn's resolved intent becomes the
// tick's input, the predictor steps the real character intent plus kinematic integration, and
// the predicted or reconciled state lands back on the components for render, animation, and
// script to read.
//
// The production flow, per predicted pawn:
//
//   spawn:   PackPredictedStateUVE() the spawn pose -> construct the NetworkPredictionUVE with
//            it and MakeCharacterPredictionStepperUVE() -> tag the entity with
//            PredictedCharacterComponentUVE so the local mover refuses it (PredictedElsewhere)
//            and never double-steps it. Both sides agree the spawn out of band, like the
//            predictor's `initial` says.
//   client:  every fixed step, TickPredictedPawnUVE() (capture -> tick -> apply), then the
//            session pump (predictor.UpdateUVE()), then SyncPredictedPawnUVE() so reconciles
//            snap the components the same step they land.
//   server:  pump, then SyncPredictedPawnUVE() onto its own pawn entity. The server stepper is
//            usually the factory wrapped in a world clamp (floors it stands on, walls it stops
//            at) - the disagreement reconciliation exists for.
//
// Truth flows one way: the predictor owns the motion and the components are a view of it.
// Spawn is the only components-to-predictor moment; TickPredictedPawnUVE() never re-seeds from
// the scene, so a stray transform write cannot fork the tick stream. What the predictor does
// not track it does not touch: ApplyPredictedStateUVE() writes position, velocity, grounded,
// and the coyote/jump-buffer clocks, and leaves the contact state (ceiling, walls, floor
// normal, platforms, last motion) alone - if local movement ever resumes, the mover
// recomputes all of it from the world on the next step.
//
// What never crosses the wire: rotation (the captured move is already world-space - Player
// intent faced by the body's own yaw at capture time, AI intent authored world-space), and
// the authored settings (both ends construct the same stepper out of band). Predicted pawns
// are roots: Pack reads the world position and works parented, but Apply lands on the local
// transform and refuses a parented body rather than writing a world pose into a local slot.
//
// Pairing with replication is the documented split from network_prediction_uve.h: the pawn
// rides replication as Owner (sending its predicted position, ignoring inbound snapshots)
// while this channel carries the corrections back. The replication wiring has landed beside
// this file (replicated_entity_uve.h); the predictor is complete without it.
//

#include <cstdint>
#include <optional>

#include "uve/component/character_controller_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Scene {

/// The mover's two determinism clocks in seconds: coyote (time since last on the floor) and
/// the buffered jump's remaining time. Both restart a replay identically, which is why they
/// ride the app word instead of the wire as floats.
struct PredictionClocksUVE final {
    float coyoteSeconds = 0.0F;
    float bufferSeconds = 0.0F;

    [[nodiscard]] bool operator==(const PredictionClocksUVE&) const = default;
};

/// Packs the clocks into the app word as two millisecond halves (coyote high, jump buffer
/// low), saturating past 65 seconds - airtime that long is not coyote time. Zero, negative,
/// and non-finite inputs pack as zero, so a broken clock can never poison the word.
[[nodiscard]] std::uint32_t PackPredictionClocksUVE(float coyoteSeconds,
                                                    float bufferSeconds) noexcept;

/// The inverse: millisecond halves back to seconds. Lossy by design (sub-millisecond time is
/// gone), but the same packer runs on both ends, so equal simulations pack equal words.
[[nodiscard]] PredictionClocksUVE UnpackPredictionClocksUVE(std::uint32_t appState) noexcept;

/// The production stepper: the real character intent (StepCharacterIntentUVE) over a scratch
/// copy of `settings`, plus kinematic integration. Each step unpacks the state's velocity,
/// grounded, and clocks into the scratch, runs the intent, integrates the position, and packs
/// the result back - the scratch's post-intent grounded, so a jump's liftoff flows into the
/// prediction instead of reconciling every airborne tick. `settings` is captured by value (the
/// authored movement fields; the runtime ones are overwritten from the state each step) and
/// `gravityY` is the engine's signed gravity. Deterministic for equal arguments, like the
/// predictor requires; a server wraps this in its world clamp and the client guesses without.
[[nodiscard]] Network::PredictionStepperUVE MakeCharacterPredictionStepperUVE(
    const CharacterControllerComponentUVE& settings, float gravityY);

/// Resolves the pawn's intent exactly like the local mover would (possession, Player yaw
/// facing, AI passthrough) and converts it to the tick's input. Fails closed through the
/// resolver itself: unpossessed, unlinked, or non-finite input stands still.
[[nodiscard]] Network::PredictedInputUVE CapturePredictedInputUVE(IEntityManagerUVE& entityManager,
                                                                 EntityUVE entity);

/// Reads the pawn's motion state off the components: world position, velocity, grounded, and
/// the packed clocks. Nothing for a dead entity, a missing world transform or controller, or
/// a non-finite position or velocity. Parenting is fine here - a world position is a world
/// position - so this is also how the app seeds the spawn both sides agree on.
[[nodiscard]] std::optional<Network::PredictedStateUVE> PackPredictedStateUVE(
    const IEntityManagerUVE& entityManager, EntityUVE entity);

/// Writes the predictor's present onto the components: the position through the scene graph
/// (local transform, dirtied and immediately recomputed, the mover's own rule), the velocity,
/// grounded, and clocks onto the controller. False - and the entity untouched - for a dead
/// entity, missing transform/world/controller, a parented body, or a non-finite position or
/// velocity. The contact state is deliberately not written (see the scope note above).
bool ApplyPredictedStateUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                            EntityUVE entity, const Network::PredictedStateUVE& state);

/// One client fixed step for one pawn: capture the intent, tick the predictor, apply the
/// predicted present. Answers the tick. The stream stays dense whatever the entity does - a
/// pawn that lost its components still ticks (standing still) while the apply refuses - so
/// the server never waits on a gap for an entity problem.
[[nodiscard]] Network::PredictionTickUVE TickPredictedPawnUVE(IEntityManagerUVE& entityManager,
                                                              ISceneGraphUVE& sceneGraph,
                                                              EntityUVE entity,
                                                              Network::NetworkPredictionUVE& predictor);

/// Multi-pawn: one call per entity, each with its pawn id - the intent drives that pawn's
/// stream and the apply writes that pawn's present. Every pawn ticks at the predictor's
/// configured step; seed each (both sides alike) before its first tick.
[[nodiscard]] Network::PredictionTickUVE TickPredictedPawnUVE(IEntityManagerUVE& entityManager,
                                                              ISceneGraphUVE& sceneGraph,
                                                              EntityUVE entity,
                                                              Network::NetworkPredictionUVE& predictor,
                                                              Network::PredictionPawnUVE pawn);

/// The post-pump mirror: whatever the exchange just did - acked, reconciled, or nothing - the
/// components now show the predictor's present. False when the apply refuses (see
/// ApplyPredictedStateUVE); the predictor itself is always fine.
bool SyncPredictedPawnUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                           EntityUVE entity, Network::NetworkPredictionUVE& predictor);

/// The post-pump mirror for `pawn`: whatever the exchange just did - acked, reconciled, or
/// nothing - the components now show that pawn's present. False when the apply refuses (see
/// ApplyPredictedStateUVE); the predictor itself is always fine.
bool SyncPredictedPawnUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                           EntityUVE entity, Network::NetworkPredictionUVE& predictor,
                           Network::PredictionPawnUVE pawn);

} // namespace UVE::Scene
