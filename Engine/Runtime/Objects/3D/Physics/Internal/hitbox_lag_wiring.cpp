// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_lag_wiring.h"

#include "health_events.h"

namespace UVE::Scene {

Physics::LagTick ShooterViewTick(const Network::NetworkPrediction& predictor) {
    return ShooterViewTick(predictor, Network::kDefaultPredictionPawn);
}

Physics::LagTick ShooterViewTick(const Network::NetworkPrediction& predictor,
                                       const Network::PredictionPawn pawn) {
    return predictor.LastAckedTick(pawn);
}

FireLagShotReceipt FireLagShot(const Network::NetworkPrediction& predictor,
                                     Network::ShotSender& sender,
                                     const Network::PredictionPawn pawn,
                                     const float fraction) {
    const Physics::LagTick view = ShooterViewTick(predictor, pawn);
    if (view == Network::kInvalidPredictionTick) {
        return FireLagShotReceipt{FireLagShotOutcome::NoView, view};
    }
    if (!sender.Fire(pawn, view, fraction)) {
        return FireLagShotReceipt{FireLagShotOutcome::Refused, view};
    }
    return FireLagShotReceipt{FireLagShotOutcome::Fired, view};
}

Physics::LagShotReport ResolveLagShot(Physics::HitboxLagCompensator& compensator,
                                            const Entity shooter,
                                            const Physics::LagTick shooterTick,
                                            IEntityManager& entityManager) {
    if (shooterTick == Network::kInvalidPredictionTick) {
        return Physics::LagShotReport{};
    }
    return compensator.ResolveShot(shooter, shooterTick, entityManager);
}

Physics::LagShotReport ResolveLagShotAt(Physics::HitboxLagCompensator& compensator,
                                              const Entity shooter, const Physics::LagTick tick,
                                              const float fraction,
                                              IEntityManager& entityManager) {
    if (tick == Network::kInvalidPredictionTick) {
        return Physics::LagShotReport{};
    }
    return compensator.ResolveShotAt(shooter, tick, fraction, entityManager);
}

void MergeLagShot(Physics::Hitbox3DSyncReport& live,
                     const Physics::LagShotReport& compensated) {
    live.strikes.insert(live.strikes.end(), compensated.pairs.begin(), compensated.pairs.end());
}

HealthDamageResult ApplyLagRayHit(IEntityManager& entityManager,
                                        Events::IEventSystem& eventSystem,
                                        const Entity shooter,
                                        const Physics::LagRayReport& report,
                                        const float amount) {
    if (report.entity == kInvalidEntity) {
        return HealthDamageResult{};
    }
    const HealthDamageResult damage =
        ApplyHitboxStrikeToHealth(entityManager, report.entity, amount);
    if (damage.applied) {
        eventSystem.QueueEvent(Gameplay::HealthDamagedEvent{
            damage.entity, shooter, report.entity, damage.amount, damage.remaining});
        if (damage.depleted) {
            eventSystem.QueueEvent(Gameplay::HealthDepletedEvent{damage.entity});
        }
    }
    return damage;
}

} // namespace UVE::Scene
