// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_lag_wiring.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "health_events.h"
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
#include "hitbox_lag_compensation.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "hitbox_strike.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

[[nodiscard]] Network::PredictionStepper CarryStepper() {
    return [](const Network::PredictedState& state, const Network::PredictedInput& input,
              const float dt) {
        Network::PredictedState next = state;
        next.position += input.move * dt;
        return next;
    };
}

[[nodiscard]] Network::PredictedInput Push(const float x) {
    Network::PredictedInput input;
    input.move.x = x;
    return input;
}

struct LagHarness final {
    Network::LoopbackTransportPair link;
    Network::NetworkPrediction client;
    Network::NetworkPrediction server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.Session().State() == Network::SessionState::Established &&
               server.Session().State() == Network::SessionState::Established;
    }
};

[[nodiscard]] LagHarness MakeLagged() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession sessionA(Network::SessionRole::Client,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession sessionB(Network::SessionRole::Server,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkPrediction client(std::move(sessionA), Network::SessionRole::Client,
                                        CarryStepper(), Network::PredictedState{});
    Network::NetworkPrediction server(std::move(sessionB), Network::SessionRole::Server,
                                        CarryStepper(), Network::PredictedState{});
    return LagHarness{std::move(pair), std::move(client), std::move(server)};
}

struct ShotWire final {
    Network::LoopbackTransportPair link;
    Network::NetworkSession client;
    Network::NetworkSession server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.State() == Network::SessionState::Established &&
               server.State() == Network::SessionState::Established;
    }
};

[[nodiscard]] ShotWire MakeShotWire() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession client(Network::SessionRole::Client,
                                      std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession server(Network::SessionRole::Server,
                                      std::make_unique<Network::LoopbackEndpoint>(pair.b));
    return ShotWire{std::move(pair), std::move(client), std::move(server)};
}

void AckThree(LagHarness& harness) {
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        EXPECT_EQ(harness.client.Tick(Push(1.0F)), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
}

class HitboxLagWiringTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
    Physics::HitboxLagCompensator compensator;

    [[nodiscard]] Entity MakeHitbox(const Math::Vector3& position,
                                          const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee") {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hitbox3DComponent hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.damageChannel = channel;
        entityManager.AddComponent<Hitbox3DComponent>(entity, hitbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeHurtbox(const Math::Vector3& position,
                                           const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee") {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hurtbox3DComponent hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.damageChannel = channel;
        entityManager.AddComponent<Hurtbox3DComponent>(entity, hurtbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    void MakeHealth(const Entity entity, const float health = 100.0F) {
        HealthComponent body;
        body.health = health;
        body.maxHealth = 100.0F;
        entityManager.AddComponent<HealthComponent>(entity, body);
    }

    void Move(const Entity entity, const Math::Vector3& position) {
        TransformComponent transform =
            entityManager.GetComponent<TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }
};

TEST_F(HitboxLagWiringTest, ShooterView_MirrorsClientAck) {
    LagHarness harness = MakeLagged();
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(ShooterViewTick(harness.client), 0U);

    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(harness.client.Tick(Push(1.0F)), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }

    EXPECT_EQ(ShooterViewTick(harness.client), 3U);
    EXPECT_EQ(ShooterViewTick(harness.client, 0U), 3U);
}

TEST_F(HitboxLagWiringTest, ShooterView_PerPawn) {
    LagHarness harness = MakeLagged();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.client.SeedPawn(1U, Network::PredictedState{}));
    ASSERT_TRUE(harness.server.SeedPawn(1U, Network::PredictedState{}));

    ASSERT_EQ(harness.client.Tick(0U, Push(1.0F)), 1U);
    ASSERT_EQ(harness.client.Tick(1U, Push(2.0F)), 1U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.client.Tick(1U, Push(2.0F)), 2U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    EXPECT_EQ(ShooterViewTick(harness.client, 0U), 1U);
    EXPECT_EQ(ShooterViewTick(harness.client, 1U), 2U);
}

TEST_F(HitboxLagWiringTest, FireLagShot_FiredCarriesPawnViewAndFraction) {
    LagHarness prediction = MakeLagged();
    ASSERT_TRUE(prediction.Establish(1000U));
    AckThree(prediction);
    ShotWire wire = MakeShotWire();
    ASSERT_TRUE(wire.Establish(1000U));
    Network::ShotSender sender(wire.client);
    Network::ShotReceiver receiver(wire.server);

    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::Fired, 3U}));
    wire.UpdateBoth(1100U);
    const std::vector<Network::ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 0U);
    EXPECT_EQ(intents[0].seq, 1U);
    EXPECT_EQ(intents[0].viewTick, 3U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.5F);
}

TEST_F(HitboxLagWiringTest, FireLagShot_NoViewSendsNothing) {
    LagHarness prediction = MakeLagged();
    ASSERT_TRUE(prediction.Establish(1000U));
    ShotWire wire = MakeShotWire();
    ASSERT_TRUE(wire.Establish(1000U));
    Network::ShotSender sender(wire.client);
    Network::ShotReceiver receiver(wire.server);

    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::NoView,
                                     Network::kInvalidPredictionTick}));
    wire.UpdateBoth(1100U);
    EXPECT_TRUE(receiver.Poll().empty());

    // The sender was untouched: the first real fire is seq 1.
    AckThree(prediction);
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::Fired, 3U}));
    wire.UpdateBoth(1200U);
    const std::vector<Network::ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST_F(HitboxLagWiringTest, FireLagShot_RefusedBadFractionSendsNothing) {
    LagHarness prediction = MakeLagged();
    ASSERT_TRUE(prediction.Establish(1000U));
    AckThree(prediction);
    ShotWire wire = MakeShotWire();
    ASSERT_TRUE(wire.Establish(1000U));
    Network::ShotSender sender(wire.client);
    Network::ShotReceiver receiver(wire.server);

    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U,
                             std::numeric_limits<float>::quiet_NaN()),
              (FireLagShotReceipt{FireLagShotOutcome::Refused, 3U}));
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 1.0F),
              (FireLagShotReceipt{FireLagShotOutcome::Refused, 3U}));
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, -0.25F),
              (FireLagShotReceipt{FireLagShotOutcome::Refused, 3U}));
    wire.UpdateBoth(1100U);
    EXPECT_TRUE(receiver.Poll().empty());

    // Fraction refusals burn no seq: the next flight is seq 1.
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::Fired, 3U}));
    wire.UpdateBoth(1200U);
    const std::vector<Network::ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST_F(HitboxLagWiringTest, FireLagShot_PerPawnViewsGateIndependently) {
    LagHarness prediction = MakeLagged();
    ASSERT_TRUE(prediction.Establish(1000U));
    ASSERT_TRUE(prediction.client.SeedPawn(1U, Network::PredictedState{}));
    ASSERT_TRUE(prediction.server.SeedPawn(1U, Network::PredictedState{}));
    ASSERT_EQ(prediction.client.Tick(0U, Push(1.0F)), 1U);
    prediction.UpdateBoth(1000U);
    prediction.UpdateBoth(1000U);
    ShotWire wire = MakeShotWire();
    ASSERT_TRUE(wire.Establish(1000U));
    Network::ShotSender sender(wire.client);
    Network::ShotReceiver receiver(wire.server);

    EXPECT_EQ(FireLagShot(prediction.client, sender, 1U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::NoView,
                                     Network::kInvalidPredictionTick}));
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::Fired, 1U}));
    wire.UpdateBoth(1100U);
    const std::vector<Network::ShotIntent> intents = receiver.Poll();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 0U);
    EXPECT_EQ(intents[0].viewTick, 1U);
}

TEST_F(HitboxLagWiringTest, FireLagShot_RefusedSessionRefusalNamesIt) {
    LagHarness prediction = MakeLagged();
    ASSERT_TRUE(prediction.Establish(1000U));
    AckThree(prediction);
    ShotWire wire = MakeShotWire();
    Network::ShotSender sender(wire.client);
    Network::ShotReceiver receiver(wire.server);

    // The wire never established: the sender refuses, the receipt names it.
    EXPECT_EQ(FireLagShot(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceipt{FireLagShotOutcome::Refused, 3U}));
    // And nothing flew: establishing later delivers nothing.
    ASSERT_TRUE(wire.Establish(1100U));
    wire.UpdateBoth(1200U);
    EXPECT_TRUE(receiver.Poll().empty());
}

TEST_F(HitboxLagWiringTest, ResolveLagShot_InvalidTickFailsClosed) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // Tick 0 is "the server never acked this shooter": no view, no hit - even though the
    // boxes overlap in the only snapshot on record.
    const Physics::LagShotReport closed =
        ResolveLagShot(compensator, shooter, Network::kInvalidPredictionTick, entityManager);
    EXPECT_TRUE(closed.pairs.empty());

    // The contrast this guard exists for: the raw compensator clamps tick 0 to the oldest
    // history and HITS - ancient truth served to a shooter with no view.
    const Physics::LagShotReport raw =
        compensator.ResolveShot(shooter, 0U, entityManager);
    ASSERT_EQ(raw.pairs.size(), 1U);
    EXPECT_TRUE(raw.clamped);
}

TEST_F(HitboxLagWiringTest, ResolveLagShot_ForwardsValidTicks) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    EXPECT_EQ(ResolveLagShot(compensator, shooter, 1U, entityManager),
              compensator.ResolveShot(shooter, 1U, entityManager));
}

TEST_F(HitboxLagWiringTest, Merge_BeforeTrackerEntersRewoundPair) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {20.0F, 0.0F, 0.0F});

    // The present sees no hit: the only pair anywhere is the rewound one.
    Physics::Hitbox3DSyncReport live = Physics::SyncHitboxes3D(entityManager);
    ASSERT_TRUE(live.strikes.empty());
    const Physics::LagShotReport compensated =
        ResolveLagShot(compensator, shooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);

    MergeLagShot(live, compensated);
    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    const Physics::Hitbox3DStrikeLifecycleReport lifecycle = tracker.Update(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    EXPECT_EQ(lifecycle.transitions[0].kind,
              Physics::Hitbox3DStrikeTransitionKind::Entered);
    EXPECT_EQ(lifecycle.transitions[0].strike.hitbox, shooter);
    EXPECT_EQ(lifecycle.transitions[0].strike.hurtbox, target);
    EXPECT_NEAR(lifecycle.transitions[0].strike.penetrationDepth, 0.5F, kEpsilon);
}

TEST_F(HitboxLagWiringTest, Merge_ReplayedMergeStaysSilent) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {20.0F, 0.0F, 0.0F});

    Physics::Hitbox3DSyncReport live = Physics::SyncHitboxes3D(entityManager);
    const Physics::LagShotReport compensated =
        ResolveLagShot(compensator, shooter, 1U, entityManager);
    MergeLagShot(live, compensated);

    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update(live).transitions.size(), 1U);
    EXPECT_TRUE(tracker.Update(live).transitions.empty());
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST_F(HitboxLagWiringTest, Merge_SharedPairEntersOnce) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // Still overlapping live AND rewound: the merge is a plain append (both copies land),
    // and the tracker's identity dedupe keeps it one strike.
    Physics::Hitbox3DSyncReport live = Physics::SyncHitboxes3D(entityManager);
    ASSERT_EQ(live.strikes.size(), 1U);
    const Physics::LagShotReport compensated =
        ResolveLagShot(compensator, shooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    MergeLagShot(live, compensated);
    ASSERT_EQ(live.strikes.size(), 2U);

    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    const Physics::Hitbox3DStrikeLifecycleReport lifecycle = tracker.Update(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    EXPECT_EQ(lifecycle.transitions[0].kind,
              Physics::Hitbox3DStrikeTransitionKind::Entered);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST_F(HitboxLagWiringTest, Merge_TruncatedLiveRefusesExits) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update(Physics::SyncHitboxes3D(entityManager)).transitions.size(), 1U);

    // The next scan overflows: the merge still lands, but the tracker must infer no exits
    // from an incomplete snapshot - the fail-closed rule survives the merge.
    Move(target, {20.0F, 0.0F, 0.0F});
    Physics::Hitbox3DSyncReport live = Physics::SyncHitboxes3D(entityManager);
    live.strikesTruncated = true;
    const Physics::LagShotReport compensated =
        ResolveLagShot(compensator, shooter, 1U, entityManager);
    MergeLagShot(live, compensated);
    const Physics::Hitbox3DStrikeLifecycleReport lifecycle = tracker.Update(live);
    EXPECT_TRUE(lifecycle.transitions.empty());
    EXPECT_TRUE(lifecycle.inputSnapshotTruncated);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST_F(HitboxLagWiringTest, ApplyLagRayHit_MissAppliesNothing) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 5.0F, 0.0F});
    MakeHealth(target);
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReport miss =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(miss.entity, kInvalidEntity);

    std::vector<Gameplay::HealthDamagedEvent> damaged;
    std::vector<Gameplay::HealthDepletedEvent> depleted;
    const Events::EventSubscription damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscription depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEvent>(
            [&](const Gameplay::HealthDepletedEvent& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResult result =
        ApplyLagRayHit(entityManager, eventSystem, shooter, miss);
    EXPECT_FALSE(result.applied);
    eventSystem.DispatchQueued();
    EXPECT_TRUE(damaged.empty());
    EXPECT_TRUE(depleted.empty());
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(target).health, 100.0F);
}

TEST_F(HitboxLagWiringTest, ApplyLagRayHit_HitDamagesAndQueuesEvents) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    MakeHealth(target);
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {5.0F, 5.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The tick-1 box face at x = 4 is nine metres down the ray - the target has stepped
    // off the line in the present, but the rewound ray still finds where it was.
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReport hit =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);
    EXPECT_NEAR(hit.distance, 9.0F, kEpsilon);

    std::vector<Gameplay::HealthDamagedEvent> damaged;
    std::vector<Gameplay::HealthDepletedEvent> depleted;
    const Events::EventSubscription damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscription depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEvent>(
            [&](const Gameplay::HealthDepletedEvent& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResult result =
        ApplyLagRayHit(entityManager, eventSystem, shooter, hit);
    EXPECT_TRUE(result.applied);
    EXPECT_FALSE(result.depleted);
    EXPECT_FLOAT_EQ(result.amount, kDefaultHitboxStrikeDamage);
    EXPECT_FLOAT_EQ(result.remaining, 99.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(target).health, 99.0F);

    eventSystem.DispatchQueued();
    ASSERT_EQ(damaged.size(), 1U);
    EXPECT_EQ(damaged[0], (Gameplay::HealthDamagedEvent{target, shooter, target, 1.0F, 99.0F}));
    EXPECT_TRUE(depleted.empty());
}

TEST_F(HitboxLagWiringTest, ApplyLagRayHit_LethalQueuesDepleted) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    MakeHealth(target, 1.0F);
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReport hit =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);

    std::vector<Gameplay::HealthDamagedEvent> damaged;
    std::vector<Gameplay::HealthDepletedEvent> depleted;
    const Events::EventSubscription damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscription depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEvent>(
            [&](const Gameplay::HealthDepletedEvent& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResult result =
        ApplyLagRayHit(entityManager, eventSystem, shooter, hit, 40.0F);
    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.depleted);
    EXPECT_FLOAT_EQ(result.remaining, 0.0F);

    eventSystem.DispatchQueued();
    ASSERT_EQ(damaged.size(), 1U);
    EXPECT_EQ(damaged[0].hurtbox, target);
    ASSERT_EQ(depleted.size(), 1U);
    EXPECT_EQ(depleted[0].entity, target);
}

TEST_F(HitboxLagWiringTest, ApplyLagRayHit_UnappliedQueuesNothing) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // A clean rewound hit on a body with no health: the report names the victim, but there
    // is no consequence for the world to accept, so no event may follow.
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReport hit =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);

    std::vector<Gameplay::HealthDamagedEvent> damaged;
    const Events::EventSubscription damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEvent>(
            [&](const Gameplay::HealthDamagedEvent& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);

    const HealthDamageResult result =
        ApplyLagRayHit(entityManager, eventSystem, shooter, hit);
    EXPECT_FALSE(result.applied);
    eventSystem.DispatchQueued();
    EXPECT_TRUE(damaged.empty());
}

TEST_F(HitboxLagWiringTest, EndToEnd_LaggedShooterStillHits) {
    LagHarness harness = MakeLagged();
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.Tick(Push(1.0F)), 1U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    // The shooter's view comes from its own prediction stream: one tick in, one tick acked.
    const Physics::LagTick view = ShooterViewTick(harness.client);
    ASSERT_EQ(view, 1U);

    // The server recorded that tick while the boxes overlapped, then the target escaped.
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    MakeHealth(target);
    ASSERT_TRUE(compensator.Record(view, entityManager));
    Move(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The present sees no hit - the Entered below exists only because the rewound pair
    // merged in before the diff.
    Physics::Hitbox3DSyncReport live = Physics::SyncHitboxes3D(entityManager);
    ASSERT_TRUE(live.strikes.empty());
    const Physics::LagShotReport compensated =
        ResolveLagShot(compensator, shooter, view, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    MergeLagShot(live, compensated);

    Physics::Hitbox3DStrikeLifecycleTracker tracker;
    const Physics::Hitbox3DStrikeLifecycleReport lifecycle = tracker.Update(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    const HealthDamageResult damage = ApplyHitboxStrikeToHealth(
        entityManager, lifecycle.transitions[0].strike.hurtbox);
    EXPECT_TRUE(damage.applied);
    EXPECT_FLOAT_EQ(damage.remaining, 99.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(target).health, 99.0F);
}

TEST_F(HitboxLagWiringTest, ResolveLagShotAt_InvalidTickFailsClosed) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // No view, no hit - at any fraction.
    const Physics::LagShotReport closed = ResolveLagShotAt(
        compensator, shooter, Network::kInvalidPredictionTick, 0.5F, entityManager);
    EXPECT_TRUE(closed.pairs.empty());

    // The contrast: the raw compensator clamps tick 0 to the oldest floor and HITS.
    const Physics::LagShotReport raw =
        compensator.ResolveShotAt(shooter, 0U, 0.5F, entityManager);
    ASSERT_EQ(raw.pairs.size(), 1U);
    EXPECT_TRUE(raw.clamped);
    EXPECT_FALSE(raw.interpolated);
}

TEST_F(HitboxLagWiringTest, ResolveLagShotAt_ForwardsFraction) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {-4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The midpoint victim, through the guard: identical to the raw fractional resolve.
    const Physics::LagShotReport wired =
        ResolveLagShotAt(compensator, shooter, 1U, 0.5F, entityManager);
    EXPECT_EQ(wired, compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager));
    ASSERT_EQ(wired.pairs.size(), 1U);
    EXPECT_TRUE(wired.interpolated);
}

} // namespace
} // namespace UVE::Scene::Tests
