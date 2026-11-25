// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the production Gameplay wiring for the hitbox lag compensator - the resolve side
// named as a follow-up in hitbox_lag_compensation_uve.h. The compensator records rewound
// poses and resolves shots and rays against them, but on its own it answers nobody: nobody
// records the server tick, nobody names the shooter's view tick, nobody merges the rewound
// pairs into the live report, and nobody turns a rewound ray into damage. These four
// functions close those seams:
//
//   record:  the dedicated server loop records one snapshot per fixed tick, AFTER applying
//            that tick's inputs and updating the scene graph, numbered with the SAME ticks
//            the prediction layer applies. Prediction ticks start at 1, so record ticks do
//            too - tick 0 is never a legitimate view, it means "the server never acked this
//            shooter". There is deliberately no record wrapper here: Record already is
//            the API, and the contract is numbering, not code.
//   view:    ShooterViewTick() reads the shooter's view from its prediction stream -
//            the newest server tick the client has acked. A shooter the server never heard
//            from has no view, and a shooter with no view cannot hit.
//   shots:   ResolveLagShot() fails closed on tick 0 (the compensator would clamp it to
//            the oldest history - ancient, and wrong for a fresh shooter) and forwards
//            every legitimate tick (ResolveLagShotAt() is the fractional twin:
//             same guard, interpolated poses). MergeLagShot() appends the rewound pairs to the live
//            scan BEFORE the lifecycle tracker diffs it, so a lagged pair that was already
//            active stays silent while a rewound-only pair enters exactly once carrying
//            rewound evidence - the production rule the compensator's merge test proves.
//   rays:    gameplay resolves the ray itself (it owns the trigger, the origin, and the
//            range) and ApplyLagRayHit() lands the consequences: damage plus the health
//            events, mirroring EngineCore::SyncHitbox3DObjects's Entered handling
//            minus the strike edge. A ray is not a strike - no hitbox is involved, there is
//            no enter/exit lifetime - so it never touches the strike tracker.
//
// All three follow-ups landed: interpolated resolves (ResolveShotAt/ResolveRayAt),
// rewound world scale, and automatic engine-tick recording (EngineCore records every
// hitbox sync and drains SubmitLagShot's inbox). Games on EngineCore resolve against
// GetLagTick numbering; server-loop recording stays for dedicated servers that drive
// the compensator directly.
//   fire:    FireLagShot() is the client prequel - one call that reads the pawn's view,
//            fails closed without one, and fires view plus fraction through the sender,
//            naming the outcome in a receipt so call sites stop branching the view gate
//            by hand.
//

#include <cstdint>

#include "entity.h"
#include "i_entity_manager.h"
#include "i_event_system.h"
#include "network_prediction.h"
#include "network_shot.h"
#include "health.h"
#include "hitbox_lag_compensation.h"
#include "hitbox_strike.h"

namespace UVE::Scene {

/// The shooter's view of the server: the newest tick the client's prediction stream has
/// acked, in the lag clock the server records with. The dedicated server numbers its lag
/// snapshots with the ticks its prediction layer applied, so this ack IS the tick the
/// shooter's shot must resolve against - no mapping table, no clock sync, the same u64.
[[nodiscard]] Physics::LagTick ShooterViewTick(
    const Network::NetworkPrediction& predictor);
/// Per-pawn view for a multi-pawn stream: each pawn's acks advance independently, so each
/// pawn's shots resolve against its own newest acked tick.
[[nodiscard]] Physics::LagTick ShooterViewTick(
    const Network::NetworkPrediction& predictor, Network::PredictionPawn pawn);

/// How a FireLagShot call ended: the shot flew, the pawn had no view yet (the server
/// never acked it - expected early, not a fault), or the sender refused (a bad fraction
/// or a session refusal - the shot never flew).
enum class FireLagShotOutcome : std::uint8_t { Fired, NoView, Refused };

/// The receipt: the outcome plus the pawn's view (invalid only for NoView - a refused
/// shot names the view it attempted).
struct FireLagShotReceipt final {
    FireLagShotOutcome outcome = FireLagShotOutcome::NoView;
    Physics::LagTick viewTick = Network::kInvalidPredictionTick;

    [[nodiscard]] bool operator==(const FireLagShotReceipt&) const noexcept = default;
};

/// Fires one lag-compensated shot in a single call: reads the pawn's view tick from its
/// prediction stream, fails closed (NoView, the sender untouched) when the server never
/// acked it, and otherwise fires view plus fraction through the sender. The fraction is
/// the fire offset within the view tick, [0, 1): the sender owns that guard, and the game
/// owns the phase math that produces it - no engine clock is read here.
[[nodiscard]] FireLagShotReceipt FireLagShot(
    const Network::NetworkPrediction& predictor, Network::ShotSender& sender,
    Network::PredictionPawn pawn, float fraction);

/// Resolves the shooter's rewound overlaps, failing closed where the compensator would
/// clamp: tick 0 (kInvalidPredictionTick) means the server never acked this shooter, so
/// there is no legitimate view and the shot misses WITHOUT consulting history - forwarding
/// it would clamp to the oldest snapshot and hit where someone stood a second ago. Every
/// legitimate tick forwards to HitboxLagCompensator::ResolveShot unchanged, clamp
/// flag and all: the server did ack those ticks, so their history is the shooter's truth.
[[nodiscard]] Physics::LagShotReport ResolveLagShot(
    Physics::HitboxLagCompensator& compensator, Entity shooter,
    Physics::LagTick shooterTick, IEntityManager& entityManager);

/// Fractional-tick shot resolve with the same fail-closed guard: tick 0 misses without
/// consulting history, every legitimate tick forwards to
/// HitboxLagCompensator::ResolveShotAt. The fraction is the shooter's fire offset
/// within its view tick - validity is the compensator's (out-of-range fails closed there
/// too). The client-to-server fraction transport rides the shot protocol (ShotIntent
/// view tick plus fire fraction); the lag-comp server demo fires and resolves real intents.
[[nodiscard]] Physics::LagShotReport ResolveLagShotAt(
    Physics::HitboxLagCompensator& compensator, Entity shooter, Physics::LagTick tick,
    float fraction, IEntityManager& entityManager);

/// Appends the rewound pairs to the live scan. Call this BEFORE the strike lifecycle
/// tracker diffs the report: a pairing the live scan already holds stays one strike (the
// tracker dedupes its input by identity, so the merge cannot double-enter), while a
/// rewound-only pair enters carrying rewound depth, axis, and channel as its evidence.
/// The counts keep describing the live scan - hurtboxCount, hitboxCount, and the
/// truncation flag are the scan's audit trail, not the merge's - and the fail-closed rule
/// survives the merge: a snapshot past the tracker's bound still diffs as truncated and
/// still infers no exits.
void MergeLagShot(Physics::Hitbox3DSyncReport& live,
                     const Physics::LagShotReport& compensated);

/// Lands a rewound ray's consequences with live-parity meaning: a miss (no entity) applies
/// nothing and queues nothing, while a hit applies `amount` to the rewound victim and
/// queues HealthDamagedEvent (plus HealthDepletedEvent on a kill), naming the
/// shooter as the hitbox so kill feeds and revenge tracking read lagged rays exactly like
/// live strikes. No strike edge is queued - rays bypass the strike tracker by design - and
/// an unapplied hit (no health, invulnerable, already dead) queues nothing: the report
/// says what the ray found, and only a consequence the world accepted becomes an event.
HealthDamageResult ApplyLagRayHit(IEntityManager& entityManager,
                                        Events::IEventSystem& eventSystem,
                                        Entity shooter,
                                        const Physics::LagRayReport& report,
                                        float amount = kDefaultHitboxStrikeDamage);

} // namespace UVE::Scene
