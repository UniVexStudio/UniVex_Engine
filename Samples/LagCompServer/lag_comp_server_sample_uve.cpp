// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "lag_comp_server_sample_uve.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_shot_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/objects/3d/health_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/objects/3d/hitbox_lag_wiring_uve.h"
#include "uve/physics/hitbox_lag_compensation_uve.h"
#include "uve/physics/hitbox_strike_lifecycle_tracker_uve.h"
#include "uve/physics/hitbox_strike_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Samples {
namespace {

constexpr float kVictimStartX = -12.0F;
constexpr float kVictimSpeed = 0.15F;
constexpr float kVictimHealth = 10000.0F;
constexpr float kRayRange = 40.0F;
constexpr float kRayDamage = 2.0F;
constexpr std::uint64_t kBotPumpEvery = 10U;
constexpr std::uint64_t kEstablishMs = 1000U;
constexpr Network::NetworkChannelIdUVE kPredictionChannelUVE = 11U;
constexpr Network::NetworkChannelIdUVE kShotChannelUVE = 12U;
constexpr float kFireFractionUVE = 0.5F;

[[nodiscard]] Network::PredictionStepperUVE CarryStepperUVE() {
    return [](const Network::PredictedStateUVE& state, const Network::PredictedInputUVE& input,
              const float dt) {
        Network::PredictedStateUVE next = state;
        next.position += input.move * dt;
        return next;
    };
}

struct LagCompServerUVE final {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Physics::HitboxLagCompensatorUVE compensator;
    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    Network::LoopbackTransportPairUVE link = Network::CreateLoopbackTransportPairUVE(256U);
    // Built in the runner: multiplexed sessions first, then every feature shares them.
    std::unique_ptr<Network::NetworkSessionUVE> clientSession;
    std::unique_ptr<Network::NetworkSessionUVE> serverSession;
    std::unique_ptr<Network::NetworkPredictionUVE> client;
    std::unique_ptr<Network::NetworkPredictionUVE> server;
    std::unique_ptr<Network::ShotSenderUVE> shots;
    std::unique_ptr<Network::ShotReceiverUVE> shotInbox;
    Scene::EntityUVE turret = Scene::kInvalidEntityUVE;
    Scene::EntityUVE victim = Scene::kInvalidEntityUVE;

    [[nodiscard]] Scene::EntityUVE MakeHitboxUVE(const Math::Vector3UVE& position) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Scene::Hitbox3DComponentUVE hitbox;
        hitbox.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        hitbox.damageChannel = "bolt";
        entityManager.AddComponentUVE<Scene::Hitbox3DComponentUVE>(entity, hitbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] Scene::EntityUVE MakeVictimUVE(const Math::Vector3UVE& position) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Scene::Hurtbox3DComponentUVE hurtbox;
        hurtbox.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        hurtbox.damageChannel = "bolt";
        entityManager.AddComponentUVE<Scene::Hurtbox3DComponentUVE>(entity, hurtbox);
        Scene::HealthComponentUVE body;
        body.health = kVictimHealth;
        body.maxHealth = kVictimHealth;
        entityManager.AddComponentUVE<Scene::HealthComponentUVE>(entity, body);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    void MoveVictimUVE(const float x) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(victim);
        transform.localPosition = Math::Vector3UVE{x, 0.0F, 0.0F};
        sceneGraph.SetLocalTransformUVE(entityManager, victim, transform);
        sceneGraph.UpdateUVE(entityManager);
    }
};

// The live half of the ray story: the +x ray from the turret against the victim's live box
// (a 2-wide box centred on the x axis). Origin-inside counts as a hit, like the
// narrow-phase the rewound resolve runs.
[[nodiscard]] bool LiveRayHitsUVE(const float victimX) {
    return victimX + 1.0F > 0.0F && victimX - 1.0F <= kRayRange;
}

} // namespace

LagCompServerSampleResultUVE RunLagCompServerSampleUVE(
    const LagCompServerSampleConfigUVE& config) {
    LagCompServerSampleResultUVE result;
    auto log = [&](const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    };

    LagCompServerUVE game;
    game.clientSession = std::make_unique<Network::NetworkSessionUVE>(
        Network::SessionRoleUVE::Client,
        std::make_unique<Network::LoopbackEndpointUVE>(std::move(game.link.a)));
    game.serverSession = std::make_unique<Network::NetworkSessionUVE>(
        Network::SessionRoleUVE::Server,
        std::make_unique<Network::LoopbackEndpointUVE>(std::move(game.link.b)));
    if (!game.clientSession->EnableMultiplexingUVE() ||
        !game.serverSession->EnableMultiplexingUVE()) {
        result.failures.push_back("sessions refused multiplexing");
        result.success = false;
        return result;
    }
    game.client = std::make_unique<Network::NetworkPredictionUVE>(
        *game.clientSession, kPredictionChannelUVE, Network::SessionRoleUVE::Client,
        CarryStepperUVE(), Network::PredictedStateUVE{});
    game.server = std::make_unique<Network::NetworkPredictionUVE>(
        *game.serverSession, kPredictionChannelUVE, Network::SessionRoleUVE::Server,
        CarryStepperUVE(), Network::PredictedStateUVE{});
    game.shots =
        std::make_unique<Network::ShotSenderUVE>(*game.clientSession, kShotChannelUVE);
    game.shotInbox =
        std::make_unique<Network::ShotReceiverUVE>(*game.serverSession, kShotChannelUVE);
    game.turret = game.MakeHitboxUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    game.victim = game.MakeVictimUVE(Math::Vector3UVE{kVictimStartX, 0.0F, 0.0F});

    if (!game.client->SessionUVE().ConnectUVE(kEstablishMs, 7U)) {
        result.failures.push_back("bot failed to connect");
        result.success = false;
        return result;
    }
    for (int i = 0; i < 6; ++i) {
        game.client->UpdateUVE(kEstablishMs);
        game.server->UpdateUVE(kEstablishMs);
    }
    if (game.client->SessionUVE().StateUVE() != Network::SessionStateUVE::Established ||
        game.server->SessionUVE().StateUVE() != Network::SessionStateUVE::Established) {
        result.failures.push_back("session never established");
        result.success = false;
        return result;
    }

    LagCompServerSampleStatsUVE stats;
    Physics::LagTickUVE lastRecorded = 0U;
    std::uint64_t recordFailures = 0U;
    std::uint64_t fireFailures = 0U;
    std::uint64_t shotLiveAndRewound = 0U;
    std::uint64_t rayLiveAndRewound = 0U;
    std::uint64_t shotStrikesApplied = 0U;
    std::uint64_t rayApplied = 0U;
    bool anyTruncation = false;

    for (std::uint64_t i = 0U; i < config.ticks; ++i) {
        const std::uint64_t nowMs = kEstablishMs + 1U + i;
        static_cast<void>(game.client->TickUVE(Network::PredictedInputUVE{}));
        // The bot fires off its own stream (it knows its view); the server only ever sees
        // the intent. Firing before the pumps means same-tick delivery below.
        const Physics::LagTickUVE preApplied = game.server->LastAppliedTickUVE();
        if (preApplied >= 1U) {
            const Scene::FireLagShotReceiptUVE receipt = Scene::FireLagShotUVE(
                *game.client, *game.shots, Network::kDefaultPredictionPawnUVE, kFireFractionUVE);
            if (receipt.outcome == Scene::FireLagShotOutcomeUVE::Fired) {
                ++stats.shotsFired;
                ++stats.raysFired;
            } else if (receipt.outcome == Scene::FireLagShotOutcomeUVE::NoView) {
                ++stats.shotsNoView;
            } else {
                ++fireFailures;
            }
        }
        game.server->UpdateUVE(nowMs);
        if (i % kBotPumpEvery == 0U) {
            game.client->UpdateUVE(nowMs);
        }

        // Record AFTER the scene update, numbered with the applied prediction ticks.
        const float victimX = kVictimStartX + kVictimSpeed * static_cast<float>(i + 1U);
        game.MoveVictimUVE(victimX);
        const Physics::LagTickUVE applied = game.server->LastAppliedTickUVE();
        for (Physics::LagTickUVE t = lastRecorded + 1U; t <= applied; ++t) {
            if (!game.compensator.RecordUVE(t, game.entityManager)) {
                ++recordFailures;
            }
        }
        lastRecorded = applied;

        Physics::Hitbox3DSyncReportUVE live =
            Physics::SyncHitboxes3DUVE(game.entityManager);
        bool liveHit = false;
        for (const Physics::Hitbox3DStrikePairUVE& strike : live.strikes) {
            if (strike.hitbox == game.turret && strike.hurtbox == game.victim) {
                liveHit = true;
            }
        }

        // The server never reads the bot's stream: every resolve below runs on a polled
        // intent's transported view tick and fraction.
        const std::vector<Network::ShotIntentUVE> intents = game.shotInbox->PollUVE();
        if (applied >= 1U) {
            const Physics::LagTickUVE lagView = Scene::ShooterViewTickUVE(*game.client);
            if (lagView <= applied && applied - lagView > stats.maxViewLag) {
                stats.maxViewLag = applied - lagView;
            }
        }
        const bool liveRayHit = LiveRayHitsUVE(victimX);
        Math::RayUVE ray;
        ray.origin = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        ray.direction = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        for (const Network::ShotIntentUVE& intent : intents) {
            // The wire never carries a view-0 fire (the bot gates), but a forged one fails
            // closed here for both paths: the shot wiring would drop it, and the raw ray
            // resolve must never see a clamping tick.
            if (intent.viewTick == Network::kInvalidPredictionTickUVE) {
                continue;
            }
            if (liveHit) {
                ++stats.shotLiveHits;
            }
            const Physics::LagShotReportUVE shot = Scene::ResolveLagShotAtUVE(
                game.compensator, game.turret, intent.viewTick, intent.fraction,
                game.entityManager);
        const bool rewoundHit = !shot.pairs.empty();
        if (rewoundHit) {
            ++stats.shotRewoundHits;
        }
        if (liveHit && rewoundHit) {
            ++shotLiveAndRewound;
        }
        if (rewoundHit && !liveHit) {
            ++stats.shotSaves;
        }

            if (shot.interpolated) {
                ++stats.fracInterpolated;
            }
            Scene::MergeLagShotUVE(live, shot);
            if (liveRayHit) {
                ++stats.rayLiveHits;
            }
            const Physics::LagRayReportUVE rewoundRay = game.compensator.ResolveRayAtUVE(
                game.turret, ray, kRayRange, intent.viewTick, intent.fraction,
                game.entityManager);
            const bool rayHit = rewoundRay.entity != Scene::kInvalidEntityUVE;
            if (rayHit) {
                ++stats.rayRewoundHits;
            }
            if (liveRayHit && rayHit) {
                ++rayLiveAndRewound;
            }
            if (rayHit && !liveRayHit) {
                ++stats.raySaves;
            }
            const Scene::HealthDamageResultUVE rayDamage = Scene::ApplyLagRayHitUVE(
                game.entityManager, game.eventSystem, game.turret, rewoundRay, kRayDamage);
            if (rayDamage.applied) {
                ++rayApplied;
            }
        }
        const Physics::Hitbox3DStrikeLifecycleReportUVE lifecycle =
            game.tracker.UpdateUVE(live);
        if (lifecycle.inputSnapshotTruncated || lifecycle.transitionsTruncated) {
            anyTruncation = true;
        }
        for (const Physics::Hitbox3DStrikeTransitionUVE& transition : lifecycle.transitions) {
            if (transition.kind != Physics::Hitbox3DStrikeTransitionKindUVE::Entered) {
                continue;
            }
            ++stats.enteredEvents;
            const Scene::HealthDamageResultUVE strikeDamage =
                Scene::ApplyHitboxStrikeToHealthUVE(game.entityManager,
                                                   transition.strike.hurtbox);
            if (strikeDamage.applied) {
                ++shotStrikesApplied;
            }
        }

        if (i % 100U == 55U) {
            // Mid pump-window: pump-tick samples would show a just-caught-up view.
            log("tick " + std::to_string(i) + " applied " + std::to_string(applied) +
                " intents " + std::to_string(intents.size()) + " victim " +
                std::to_string(victimX) +
                " saves " + std::to_string(stats.shotSaves) + "/" +
                std::to_string(stats.raySaves));
        }
    }

    stats.ticks = config.ticks;
    stats.victimHealth =
        game.entityManager.GetComponentUVE<Scene::HealthComponentUVE>(game.victim).health;
    result.stats = stats;

    auto check = [&](const bool ok, const std::string& message) {
        if (!ok) {
            result.failures.push_back(message);
        }
    };
    check(fireFailures == 0U, "shot intents refused: " + std::to_string(fireFailures));
    check(recordFailures == 0U,
          "lag records refused: " + std::to_string(recordFailures));
    check(lastRecorded >= 1U && lastRecorded <= config.ticks,
          "applied ticks out of bounds: " + std::to_string(lastRecorded));
    check(stats.maxViewLag > 0U, "bot never lagged (view tracked applied exactly)");
    check(stats.shotsFired > 0U, "bot never fired");
    check(stats.shotSaves > 0U, "no shot saves (live and rewound never diverged)");
    check(stats.raySaves > 0U, "no ray saves (live and rewound rays never diverged)");
    check(stats.shotRewoundHits == shotLiveAndRewound + stats.shotSaves,
          "shot accounting broken");
    check(stats.rayRewoundHits == rayLiveAndRewound + stats.raySaves,
          "ray accounting broken");
    check(stats.enteredEvents > 0U, "no merged strikes entered through the tracker");
    check(stats.fracInterpolated > 0U, "fractional resolves never interpolated");
    check(!anyTruncation, "strike report truncated mid-run");
    const float expectedHealth =
        kVictimHealth - static_cast<float>(shotStrikesApplied) * 1.0F -
        static_cast<float>(rayApplied) * kRayDamage;
    check(std::abs(stats.victimHealth - expectedHealth) < 1e-3F,
          "victim health mismatch: " + std::to_string(stats.victimHealth) + " vs " +
              std::to_string(expectedHealth));

    result.success = result.failures.empty();
    log("lag-comp server ran " + std::to_string(stats.ticks) + " ticks, shots " +
        std::to_string(stats.shotsFired) + " (saves " + std::to_string(stats.shotSaves) +
        "), rays " + std::to_string(stats.raysFired) + " (saves " +
        std::to_string(stats.raySaves) + "), entered " +
        std::to_string(stats.enteredEvents) + ", max lag " +
        std::to_string(stats.maxViewLag) + ", " + std::to_string(result.failures.size()) +
        (result.failures.size() == 1U ? " violation" : " violations"));
    return result;
}

} // namespace UVE::Samples
