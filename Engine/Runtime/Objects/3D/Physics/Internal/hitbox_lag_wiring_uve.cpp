// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hitbox_lag_wiring_uve.h"

#include "uve/gameplay/health_events_uve.h"

namespace UVE::Scene {

Physics::LagTickUVE ShooterViewTickUVE(const Network::NetworkPredictionUVE& predictor) {
    return ShooterViewTickUVE(predictor, Network::kDefaultPredictionPawnUVE);
}

Physics::LagTickUVE ShooterViewTickUVE(const Network::NetworkPredictionUVE& predictor,
                                       const Network::PredictionPawnUVE pawn) {
    return predictor.LastAckedTickUVE(pawn);
}

FireLagShotReceiptUVE FireLagShotUVE(const Network::NetworkPredictionUVE& predictor,
                                     Network::ShotSenderUVE& sender,
                                     const Network::PredictionPawnUVE pawn,
                                     const float fraction) {
    const Physics::LagTickUVE view = ShooterViewTickUVE(predictor, pawn);
    if (view == Network::kInvalidPredictionTickUVE) {
        return FireLagShotReceiptUVE{FireLagShotOutcomeUVE::NoView, view};
    }
    if (!sender.FireUVE(pawn, view, fraction)) {
        return FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Refused, view};
    }
    return FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Fired, view};
}

Physics::LagShotReportUVE ResolveLagShotUVE(Physics::HitboxLagCompensatorUVE& compensator,
                                            const EntityUVE shooter,
                                            const Physics::LagTickUVE shooterTick,
                                            IEntityManagerUVE& entityManager) {
    if (shooterTick == Network::kInvalidPredictionTickUVE) {
        return Physics::LagShotReportUVE{};
    }
    return compensator.ResolveShotUVE(shooter, shooterTick, entityManager);
}

Physics::LagShotReportUVE ResolveLagShotAtUVE(Physics::HitboxLagCompensatorUVE& compensator,
                                              const EntityUVE shooter, const Physics::LagTickUVE tick,
                                              const float fraction,
                                              IEntityManagerUVE& entityManager) {
    if (tick == Network::kInvalidPredictionTickUVE) {
        return Physics::LagShotReportUVE{};
    }
    return compensator.ResolveShotAtUVE(shooter, tick, fraction, entityManager);
}

void MergeLagShotUVE(Physics::Hitbox3DSyncReportUVE& live,
                     const Physics::LagShotReportUVE& compensated) {
    live.strikes.insert(live.strikes.end(), compensated.pairs.begin(), compensated.pairs.end());
}

HealthDamageResultUVE ApplyLagRayHitUVE(IEntityManagerUVE& entityManager,
                                        Events::IEventSystemUVE& eventSystem,
                                        const EntityUVE shooter,
                                        const Physics::LagRayReportUVE& report,
                                        const float amount) {
    if (report.entity == kInvalidEntityUVE) {
        return HealthDamageResultUVE{};
    }
    const HealthDamageResultUVE damage =
        ApplyHitboxStrikeToHealthUVE(entityManager, report.entity, amount);
    if (damage.applied) {
        eventSystem.QueueEvent(Gameplay::HealthDamagedEventUVE{
            damage.entity, shooter, report.entity, damage.amount, damage.remaining});
        if (damage.depleted) {
            eventSystem.QueueEvent(Gameplay::HealthDepletedEventUVE{damage.entity});
        }
    }
    return damage;
}

} // namespace UVE::Scene
