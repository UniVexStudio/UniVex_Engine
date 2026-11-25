// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "lag_comp_server_sample.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "ray.h"
#include "vector3.h"
#include "memory_manager.h"
#include "network_prediction.h"
#include "network_session.h"
#include "network_shot.h"
#include "network_transport.h"
#include "health.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "hitbox_lag_wiring.h"
#include "hitbox_lag_compensation.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "hitbox_strike.h"
#include "scene_graph.h"

namespace UVE::Samples {
namespace {

constexpr float kVictimStartX = -12.0F;
constexpr float kVictimSpeed = 0.15F;
constexpr float kVictimHealth = 10000.0F;
constexpr float kRayRange = 40.0F;
constexpr float kRayDamage = 2.0F;
constexpr std::uint64_t kBotPumpEvery = 10U;
constexpr std::uint64_t kEstablishMs = 1000U;
constexpr Network::NetworkChannelId kPredictionChannel = 11U;
constexpr Network::NetworkChannelId kShotChannel = 12U;
constexpr float kFireFraction = 0.5F;

[[nodiscard]] Network::PredictionStepper CarryStepper() {
    return [](const Network::PredictedState& state, const Network::PredictedInput& input,
              const float dt) {
        Network::PredictedState next = state;
        next.position += input.move * dt;
        return next;
    };
}

struct LagCompServer final {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Physics::HitboxLagCompensator compensator;
    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    Network::LoopbackTransportPair link = Network::CreateLoopbackTransportPair(256U);
    // Built in the runner: multiplexed sessions first, then every feature shares them.
    std::unique_ptr<Network::NetworkSession> clientSession;
    std::unique_ptr<Network::NetworkSession> serverSession;
    std::unique_ptr<Network::NetworkPrediction> client;
    std::unique_ptr<Network::NetworkPrediction> server;
    std::unique_ptr<Network::ShotSender> shots;
    std::unique_ptr<Network::ShotReceiver> shotInbox;
    Scene::Entity turret = Scene::kInvalidEntity;
    Scene::Entity victim = Scene::kInvalidEntity;

    [[nodiscard]] Scene::Entity MakeHitbox(const Math::Vector3& position) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Scene::Hitbox3DComponent hitbox;
        hitbox.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
        hitbox.damageChannel = "bolt";
        entityManager.AddComponent<Scene::Hitbox3DComponent>(entity, hitbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Scene::Entity MakeVictim(const Math::Vector3& position) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Scene::Hurtbox3DComponent hurtbox;
        hurtbox.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
        hurtbox.damageChannel = "bolt";
        entityManager.AddComponent<Scene::Hurtbox3DComponent>(entity, hurtbox);
        Scene::HealthComponent body;
        body.health = kVictimHealth;
        body.maxHealth = kVictimHealth;
        entityManager.AddComponent<Scene::HealthComponent>(entity, body);
        sceneGraph.Update(entityManager);
        return entity;
    }

    void MoveVictim(const float x) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(victim);
        transform.localPosition = Math::Vector3{x, 0.0F, 0.0F};
        sceneGraph.SetLocalTransform(entityManager, victim, transform);
        sceneGraph.Update(entityManager);
    }
};

// The live half of the ray story: the +x ray from the turret against the victim's live box
// (a 2-wide box centred on the x axis). Origin-inside counts as a hit, like the
// narrow-phase the rewound resolve runs.
[[nodiscard]] bool LiveRayHits(const float victimX) {
    return victimX + 1.0F > 0.0F && victimX - 1.0F <= kRayRange;
}

} // namespace

LagCompServerSampleResult RunLagCompServerSample(
    const LagCompServerSampleConfig& config) {
    LagCompServerSampleResult result;
    auto log = [&](const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    };

    LagCompServer game;
    game.clientSession = std::make_unique<Network::NetworkSession>(
        Network::SessionRole::Client,
        std::make_unique<Network::LoopbackEndpoint>(std::move(game.link.a)));
    game.serverSession = std::make_unique<Network::NetworkSession>(
        Network::SessionRole::Server,
        std::make_unique<Network::LoopbackEndpoint>(std::move(game.link.b)));
    if (!game.clientSession->EnableMultiplexing() ||
        !game.serverSession->EnableMultiplexing()) {
        result.failures.push_back("sessions refused multiplexing");
        result.success = false;
        return result;
    }
    game.client = std::make_unique<Network::NetworkPrediction>(
        *game.clientSession, kPredictionChannel, Network::SessionRole::Client,
        CarryStepper(), Network::PredictedState{});
    game.server = std::make_unique<Network::NetworkPrediction>(
        *game.serverSession, kPredictionChannel, Network::SessionRole::Server,
        CarryStepper(), Network::PredictedState{});
    game.shots =
        std::make_unique<Network::ShotSender>(*game.clientSession, kShotChannel);
    game.shotInbox =
        std::make_unique<Network::ShotReceiver>(*game.serverSession, kShotChannel);
    game.turret = game.MakeHitbox(Math::Vector3{0.0F, 0.0F, 0.0F});
    game.victim = game.MakeVictim(Math::Vector3{kVictimStartX, 0.0F, 0.0F});

    if (!game.client->Session().Connect(kEstablishMs, 7U)) {
        result.failures.push_back("bot failed to connect");
        result.success = false;
        return result;
    }
    for (int i = 0; i < 6; ++i) {
        game.client->Update(kEstablishMs);
        game.server->Update(kEstablishMs);
    }
    if (game.client->Session().State() != Network::SessionState::Established ||
        game.server->Session().State() != Network::SessionState::Established) {
        result.failures.push_back("session never established");
        result.success = false;
        return result;
    }

    LagCompServerSampleStats stats;
    Physics::LagTick lastRecorded = 0U;
    std::uint64_t recordFailures = 0U;
    std::uint64_t fireFailures = 0U;
    std::uint64_t shotLiveAndRewound = 0U;
    std::uint64_t rayLiveAndRewound = 0U;
    std::uint64_t shotStrikesApplied = 0U;
    std::uint64_t rayApplied = 0U;
    bool anyTruncation = false;

    for (std::uint64_t i = 0U; i < config.ticks; ++i) {
        const std::uint64_t nowMs = kEstablishMs + 1U + i;
        static_cast<void>(game.client->Tick(Network::PredictedInput{}));
        // The bot fires off its own stream (it knows its view); the server only ever sees
        // the intent. Firing before the pumps means same-tick delivery below.
        const Physics::LagTick preApplied = game.server->LastAppliedTick();
        if (preApplied >= 1U) {
            const Scene::FireLagShotReceipt receipt = Scene::FireLagShot(
                *game.client, *game.shots, Network::kDefaultPredictionPawn, kFireFraction);
            if (receipt.outcome == Scene::FireLagShotOutcome::Fired) {
                ++stats.shotsFired;
                ++stats.raysFired;
            } else if (receipt.outcome == Scene::FireLagShotOutcome::NoView) {
                ++stats.shotsNoView;
            } else {
                ++fireFailures;
            }
        }
        game.server->Update(nowMs);
        if (i % kBotPumpEvery == 0U) {
            game.client->Update(nowMs);
        }

        // Record AFTER the scene update, numbered with the applied prediction ticks.
        const float victimX = kVictimStartX + kVictimSpeed * static_cast<float>(i + 1U);
        game.MoveVictim(victimX);
        const Physics::LagTick applied = game.server->LastAppliedTick();
        for (Physics::LagTick t = lastRecorded + 1U; t <= applied; ++t) {
            if (!game.compensator.Record(t, game.entityManager)) {
                ++recordFailures;
            }
        }
        lastRecorded = applied;

        Physics::Hitbox3DSyncReport live =
            Physics::SyncHitboxes3D(game.entityManager);
        bool liveHit = false;
        for (const Physics::Hitbox3DStrikePair& strike : live.strikes) {
            if (strike.hitbox == game.turret && strike.hurtbox == game.victim) {
                liveHit = true;
            }
        }

        // The server never reads the bot's stream: every resolve below runs on a polled
        // intent's transported view tick and fraction.
        const std::vector<Network::ShotIntent> intents = game.shotInbox->Poll();
        if (applied >= 1U) {
            const Physics::LagTick lagView = Scene::ShooterViewTick(*game.client);
            if (lagView <= applied && applied - lagView > stats.maxViewLag) {
                stats.maxViewLag = applied - lagView;
            }
        }
        const bool liveRayHit = LiveRayHits(victimX);
        Math::Ray ray;
        ray.origin = Math::Vector3{0.0F, 0.0F, 0.0F};
        ray.direction = Math::Vector3{1.0F, 0.0F, 0.0F};
        for (const Network::ShotIntent& intent : intents) {
            // The wire never carries a view-0 fire (the bot gates), but a forged one fails
            // closed here for both paths: the shot wiring would drop it, and the raw ray
            // resolve must never see a clamping tick.
            if (intent.viewTick == Network::kInvalidPredictionTick) {
                continue;
            }
            if (liveHit) {
                ++stats.shotLiveHits;
            }
            const Physics::LagShotReport shot = Scene::ResolveLagShotAt(
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
            Scene::MergeLagShot(live, shot);
            if (liveRayHit) {
                ++stats.rayLiveHits;
            }
            const Physics::LagRayReport rewoundRay = game.compensator.ResolveRayAt(
                game.turret, ray, kRayRange, intent.viewTick, intent.fraction,
                game.entityManager);
            const bool rayHit = rewoundRay.entity != Scene::kInvalidEntity;
            if (rayHit) {
                ++stats.rayRewoundHits;
            }
            if (liveRayHit && rayHit) {
                ++rayLiveAndRewound;
            }
            if (rayHit && !liveRayHit) {
                ++stats.raySaves;
            }
            const Scene::HealthDamageResult rayDamage = Scene::ApplyLagRayHit(
                game.entityManager, game.eventSystem, game.turret, rewoundRay, kRayDamage);
            if (rayDamage.applied) {
                ++rayApplied;
            }
        }
        const Physics::Hitbox3DStrikeLifecycleReport lifecycle =
            game.tracker.Update(live);
        if (lifecycle.inputSnapshotTruncated || lifecycle.transitionsTruncated) {
            anyTruncation = true;
        }
        for (const Physics::Hitbox3DStrikeTransition& transition : lifecycle.transitions) {
            if (transition.kind != Physics::Hitbox3DStrikeTransitionKind::Entered) {
                continue;
            }
            ++stats.enteredEvents;
            const Scene::HealthDamageResult strikeDamage =
                Scene::ApplyHitboxStrikeToHealth(game.entityManager,
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
        game.entityManager.GetComponent<Scene::HealthComponent>(game.victim).health;
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
