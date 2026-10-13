// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hitbox_lag_wiring_uve.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/health_events_uve.h"
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
#include "uve/physics/hitbox_lag_compensation_uve.h"
#include "uve/physics/hitbox_strike_lifecycle_tracker_uve.h"
#include "uve/physics/hitbox_strike_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

[[nodiscard]] Network::PredictionStepperUVE CarryStepperUVE() {
    return [](const Network::PredictedStateUVE& state, const Network::PredictedInputUVE& input,
              const float dt) {
        Network::PredictedStateUVE next = state;
        next.position += input.move * dt;
        return next;
    };
}

[[nodiscard]] Network::PredictedInputUVE PushUVE(const float x) {
    Network::PredictedInputUVE input;
    input.move.x = x;
    return input;
}

struct LagHarnessUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkPredictionUVE client;
    Network::NetworkPredictionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.SessionUVE().StateUVE() == Network::SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] LagHarnessUVE MakeLaggedUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE sessionA(Network::SessionRoleUVE::Client,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE sessionB(Network::SessionRoleUVE::Server,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkPredictionUVE client(std::move(sessionA), Network::SessionRoleUVE::Client,
                                        CarryStepperUVE(), Network::PredictedStateUVE{});
    Network::NetworkPredictionUVE server(std::move(sessionB), Network::SessionRoleUVE::Server,
                                        CarryStepperUVE(), Network::PredictedStateUVE{});
    return LagHarnessUVE{std::move(pair), std::move(client), std::move(server)};
}

struct ShotWireUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkSessionUVE client;
    Network::NetworkSessionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.StateUVE() == Network::SessionStateUVE::Established &&
               server.StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] ShotWireUVE MakeShotWireUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE client(Network::SessionRoleUVE::Client,
                                      std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE server(Network::SessionRoleUVE::Server,
                                      std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    return ShotWireUVE{std::move(pair), std::move(client), std::move(server)};
}

void AckThreeUVE(LagHarnessUVE& harness) {
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        EXPECT_EQ(harness.client.TickUVE(PushUVE(1.0F)), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
}

class HitboxLagWiringUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
    Physics::HitboxLagCompensatorUVE compensator;

    [[nodiscard]] EntityUVE MakeHitboxUVE(const Math::Vector3UVE& position,
                                          const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee") {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hitbox3DComponentUVE hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hitbox3DComponentUVE>(entity, hitbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeHurtboxUVE(const Math::Vector3UVE& position,
                                           const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee") {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hurtbox3DComponentUVE hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(entity, hurtbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    void MakeHealthUVE(const EntityUVE entity, const float health = 100.0F) {
        HealthComponentUVE body;
        body.health = health;
        body.maxHealth = 100.0F;
        entityManager.AddComponentUVE<HealthComponentUVE>(entity, body);
    }

    void MoveUVE(const EntityUVE entity, const Math::Vector3UVE& position) {
        TransformComponentUVE transform =
            entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }
};

TEST_F(HitboxLagWiringUVETest, ShooterView_MirrorsClientAck) {
    LagHarnessUVE harness = MakeLaggedUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(ShooterViewTickUVE(harness.client), 0U);

    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PushUVE(1.0F)), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }

    EXPECT_EQ(ShooterViewTickUVE(harness.client), 3U);
    EXPECT_EQ(ShooterViewTickUVE(harness.client, 0U), 3U);
}

TEST_F(HitboxLagWiringUVETest, ShooterView_PerPawn) {
    LagHarnessUVE harness = MakeLaggedUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.client.SeedPawnUVE(1U, Network::PredictedStateUVE{}));
    ASSERT_TRUE(harness.server.SeedPawnUVE(1U, Network::PredictedStateUVE{}));

    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(1.0F)), 1U);
    ASSERT_EQ(harness.client.TickUVE(1U, PushUVE(2.0F)), 1U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.client.TickUVE(1U, PushUVE(2.0F)), 2U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    EXPECT_EQ(ShooterViewTickUVE(harness.client, 0U), 1U);
    EXPECT_EQ(ShooterViewTickUVE(harness.client, 1U), 2U);
}

TEST_F(HitboxLagWiringUVETest, FireLagShot_FiredCarriesPawnViewAndFraction) {
    LagHarnessUVE prediction = MakeLaggedUVE();
    ASSERT_TRUE(prediction.EstablishUVE(1000U));
    AckThreeUVE(prediction);
    ShotWireUVE wire = MakeShotWireUVE();
    ASSERT_TRUE(wire.EstablishUVE(1000U));
    Network::ShotSenderUVE sender(wire.client);
    Network::ShotReceiverUVE receiver(wire.server);

    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Fired, 3U}));
    wire.UpdateBothUVE(1100U);
    const std::vector<Network::ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 0U);
    EXPECT_EQ(intents[0].seq, 1U);
    EXPECT_EQ(intents[0].viewTick, 3U);
    EXPECT_FLOAT_EQ(intents[0].fraction, 0.5F);
}

TEST_F(HitboxLagWiringUVETest, FireLagShot_NoViewSendsNothing) {
    LagHarnessUVE prediction = MakeLaggedUVE();
    ASSERT_TRUE(prediction.EstablishUVE(1000U));
    ShotWireUVE wire = MakeShotWireUVE();
    ASSERT_TRUE(wire.EstablishUVE(1000U));
    Network::ShotSenderUVE sender(wire.client);
    Network::ShotReceiverUVE receiver(wire.server);

    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::NoView,
                                     Network::kInvalidPredictionTickUVE}));
    wire.UpdateBothUVE(1100U);
    EXPECT_TRUE(receiver.PollUVE().empty());

    // The sender was untouched: the first real fire is seq 1.
    AckThreeUVE(prediction);
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Fired, 3U}));
    wire.UpdateBothUVE(1200U);
    const std::vector<Network::ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST_F(HitboxLagWiringUVETest, FireLagShot_RefusedBadFractionSendsNothing) {
    LagHarnessUVE prediction = MakeLaggedUVE();
    ASSERT_TRUE(prediction.EstablishUVE(1000U));
    AckThreeUVE(prediction);
    ShotWireUVE wire = MakeShotWireUVE();
    ASSERT_TRUE(wire.EstablishUVE(1000U));
    Network::ShotSenderUVE sender(wire.client);
    Network::ShotReceiverUVE receiver(wire.server);

    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U,
                             std::numeric_limits<float>::quiet_NaN()),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Refused, 3U}));
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 1.0F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Refused, 3U}));
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, -0.25F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Refused, 3U}));
    wire.UpdateBothUVE(1100U);
    EXPECT_TRUE(receiver.PollUVE().empty());

    // Fraction refusals burn no seq: the next flight is seq 1.
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Fired, 3U}));
    wire.UpdateBothUVE(1200U);
    const std::vector<Network::ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].seq, 1U);
}

TEST_F(HitboxLagWiringUVETest, FireLagShot_PerPawnViewsGateIndependently) {
    LagHarnessUVE prediction = MakeLaggedUVE();
    ASSERT_TRUE(prediction.EstablishUVE(1000U));
    ASSERT_TRUE(prediction.client.SeedPawnUVE(1U, Network::PredictedStateUVE{}));
    ASSERT_TRUE(prediction.server.SeedPawnUVE(1U, Network::PredictedStateUVE{}));
    ASSERT_EQ(prediction.client.TickUVE(0U, PushUVE(1.0F)), 1U);
    prediction.UpdateBothUVE(1000U);
    prediction.UpdateBothUVE(1000U);
    ShotWireUVE wire = MakeShotWireUVE();
    ASSERT_TRUE(wire.EstablishUVE(1000U));
    Network::ShotSenderUVE sender(wire.client);
    Network::ShotReceiverUVE receiver(wire.server);

    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 1U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::NoView,
                                     Network::kInvalidPredictionTickUVE}));
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Fired, 1U}));
    wire.UpdateBothUVE(1100U);
    const std::vector<Network::ShotIntentUVE> intents = receiver.PollUVE();
    ASSERT_EQ(intents.size(), 1U);
    EXPECT_EQ(intents[0].pawn, 0U);
    EXPECT_EQ(intents[0].viewTick, 1U);
}

TEST_F(HitboxLagWiringUVETest, FireLagShot_RefusedSessionRefusalNamesIt) {
    LagHarnessUVE prediction = MakeLaggedUVE();
    ASSERT_TRUE(prediction.EstablishUVE(1000U));
    AckThreeUVE(prediction);
    ShotWireUVE wire = MakeShotWireUVE();
    Network::ShotSenderUVE sender(wire.client);
    Network::ShotReceiverUVE receiver(wire.server);

    // The wire never established: the sender refuses, the receipt names it.
    EXPECT_EQ(FireLagShotUVE(prediction.client, sender, 0U, 0.5F),
              (FireLagShotReceiptUVE{FireLagShotOutcomeUVE::Refused, 3U}));
    // And nothing flew: establishing later delivers nothing.
    ASSERT_TRUE(wire.EstablishUVE(1100U));
    wire.UpdateBothUVE(1200U);
    EXPECT_TRUE(receiver.PollUVE().empty());
}

TEST_F(HitboxLagWiringUVETest, ResolveLagShot_InvalidTickFailsClosed) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // Tick 0 is "the server never acked this shooter": no view, no hit - even though the
    // boxes overlap in the only snapshot on record.
    const Physics::LagShotReportUVE closed =
        ResolveLagShotUVE(compensator, shooter, Network::kInvalidPredictionTickUVE, entityManager);
    EXPECT_TRUE(closed.pairs.empty());

    // The contrast this guard exists for: the raw compensator clamps tick 0 to the oldest
    // history and HITS - ancient truth served to a shooter with no view.
    const Physics::LagShotReportUVE raw =
        compensator.ResolveShotUVE(shooter, 0U, entityManager);
    ASSERT_EQ(raw.pairs.size(), 1U);
    EXPECT_TRUE(raw.clamped);
}

TEST_F(HitboxLagWiringUVETest, ResolveLagShot_ForwardsValidTicks) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    EXPECT_EQ(ResolveLagShotUVE(compensator, shooter, 1U, entityManager),
              compensator.ResolveShotUVE(shooter, 1U, entityManager));
}

TEST_F(HitboxLagWiringUVETest, Merge_BeforeTrackerEntersRewoundPair) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {20.0F, 0.0F, 0.0F});

    // The present sees no hit: the only pair anywhere is the rewound one.
    Physics::Hitbox3DSyncReportUVE live = Physics::SyncHitboxes3DUVE(entityManager);
    ASSERT_TRUE(live.strikes.empty());
    const Physics::LagShotReportUVE compensated =
        ResolveLagShotUVE(compensator, shooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);

    MergeLagShotUVE(live, compensated);
    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    const Physics::Hitbox3DStrikeLifecycleReportUVE lifecycle = tracker.UpdateUVE(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    EXPECT_EQ(lifecycle.transitions[0].kind,
              Physics::Hitbox3DStrikeTransitionKindUVE::Entered);
    EXPECT_EQ(lifecycle.transitions[0].strike.hitbox, shooter);
    EXPECT_EQ(lifecycle.transitions[0].strike.hurtbox, target);
    EXPECT_NEAR(lifecycle.transitions[0].strike.penetrationDepth, 0.5F, kEpsilon);
}

TEST_F(HitboxLagWiringUVETest, Merge_ReplayedMergeStaysSilent) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {20.0F, 0.0F, 0.0F});

    Physics::Hitbox3DSyncReportUVE live = Physics::SyncHitboxes3DUVE(entityManager);
    const Physics::LagShotReportUVE compensated =
        ResolveLagShotUVE(compensator, shooter, 1U, entityManager);
    MergeLagShotUVE(live, compensated);

    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE(live).transitions.size(), 1U);
    EXPECT_TRUE(tracker.UpdateUVE(live).transitions.empty());
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST_F(HitboxLagWiringUVETest, Merge_SharedPairEntersOnce) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // Still overlapping live AND rewound: the merge is a plain append (both copies land),
    // and the tracker's identity dedupe keeps it one strike.
    Physics::Hitbox3DSyncReportUVE live = Physics::SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(live.strikes.size(), 1U);
    const Physics::LagShotReportUVE compensated =
        ResolveLagShotUVE(compensator, shooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    MergeLagShotUVE(live, compensated);
    ASSERT_EQ(live.strikes.size(), 2U);

    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    const Physics::Hitbox3DStrikeLifecycleReportUVE lifecycle = tracker.UpdateUVE(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    EXPECT_EQ(lifecycle.transitions[0].kind,
              Physics::Hitbox3DStrikeTransitionKindUVE::Entered);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST_F(HitboxLagWiringUVETest, Merge_TruncatedLiveRefusesExits) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE(Physics::SyncHitboxes3DUVE(entityManager)).transitions.size(), 1U);

    // The next scan overflows: the merge still lands, but the tracker must infer no exits
    // from an incomplete snapshot - the fail-closed rule survives the merge.
    MoveUVE(target, {20.0F, 0.0F, 0.0F});
    Physics::Hitbox3DSyncReportUVE live = Physics::SyncHitboxes3DUVE(entityManager);
    live.strikesTruncated = true;
    const Physics::LagShotReportUVE compensated =
        ResolveLagShotUVE(compensator, shooter, 1U, entityManager);
    MergeLagShotUVE(live, compensated);
    const Physics::Hitbox3DStrikeLifecycleReportUVE lifecycle = tracker.UpdateUVE(live);
    EXPECT_TRUE(lifecycle.transitions.empty());
    EXPECT_TRUE(lifecycle.inputSnapshotTruncated);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST_F(HitboxLagWiringUVETest, ApplyLagRayHit_MissAppliesNothing) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 5.0F, 0.0F});
    MakeHealthUVE(target);
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReportUVE miss =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(miss.entity, kInvalidEntityUVE);

    std::vector<Gameplay::HealthDamagedEventUVE> damaged;
    std::vector<Gameplay::HealthDepletedEventUVE> depleted;
    const Events::EventSubscriptionUVE damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEventUVE>(
            [&](const Gameplay::HealthDamagedEventUVE& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscriptionUVE depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEventUVE>(
            [&](const Gameplay::HealthDepletedEventUVE& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResultUVE result =
        ApplyLagRayHitUVE(entityManager, eventSystem, shooter, miss);
    EXPECT_FALSE(result.applied);
    eventSystem.DispatchQueuedUVE();
    EXPECT_TRUE(damaged.empty());
    EXPECT_TRUE(depleted.empty());
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(target).health, 100.0F);
}

TEST_F(HitboxLagWiringUVETest, ApplyLagRayHit_HitDamagesAndQueuesEvents) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    MakeHealthUVE(target);
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {5.0F, 5.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The tick-1 box face at x = 4 is nine metres down the ray - the target has stepped
    // off the line in the present, but the rewound ray still finds where it was.
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReportUVE hit =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);
    EXPECT_NEAR(hit.distance, 9.0F, kEpsilon);

    std::vector<Gameplay::HealthDamagedEventUVE> damaged;
    std::vector<Gameplay::HealthDepletedEventUVE> depleted;
    const Events::EventSubscriptionUVE damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEventUVE>(
            [&](const Gameplay::HealthDamagedEventUVE& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscriptionUVE depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEventUVE>(
            [&](const Gameplay::HealthDepletedEventUVE& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResultUVE result =
        ApplyLagRayHitUVE(entityManager, eventSystem, shooter, hit);
    EXPECT_TRUE(result.applied);
    EXPECT_FALSE(result.depleted);
    EXPECT_FLOAT_EQ(result.amount, kDefaultHitboxStrikeDamageUVE);
    EXPECT_FLOAT_EQ(result.remaining, 99.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(target).health, 99.0F);

    eventSystem.DispatchQueuedUVE();
    ASSERT_EQ(damaged.size(), 1U);
    EXPECT_EQ(damaged[0], (Gameplay::HealthDamagedEventUVE{target, shooter, target, 1.0F, 99.0F}));
    EXPECT_TRUE(depleted.empty());
}

TEST_F(HitboxLagWiringUVETest, ApplyLagRayHit_LethalQueuesDepleted) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    MakeHealthUVE(target, 1.0F);
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReportUVE hit =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);

    std::vector<Gameplay::HealthDamagedEventUVE> damaged;
    std::vector<Gameplay::HealthDepletedEventUVE> depleted;
    const Events::EventSubscriptionUVE damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEventUVE>(
            [&](const Gameplay::HealthDamagedEventUVE& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);
    const Events::EventSubscriptionUVE depletedSub =
        eventSystem.Subscribe<Gameplay::HealthDepletedEventUVE>(
            [&](const Gameplay::HealthDepletedEventUVE& event) { depleted.push_back(event); });
    static_cast<void>(depletedSub);

    const HealthDamageResultUVE result =
        ApplyLagRayHitUVE(entityManager, eventSystem, shooter, hit, 40.0F);
    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.depleted);
    EXPECT_FLOAT_EQ(result.remaining, 0.0F);

    eventSystem.DispatchQueuedUVE();
    ASSERT_EQ(damaged.size(), 1U);
    EXPECT_EQ(damaged[0].hurtbox, target);
    ASSERT_EQ(depleted.size(), 1U);
    EXPECT_EQ(depleted[0].entity, target);
}

TEST_F(HitboxLagWiringUVETest, ApplyLagRayHit_UnappliedQueuesNothing) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // A clean rewound hit on a body with no health: the report names the victim, but there
    // is no consequence for the world to accept, so no event may follow.
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const Physics::LagRayReportUVE hit =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    ASSERT_EQ(hit.entity, target);

    std::vector<Gameplay::HealthDamagedEventUVE> damaged;
    const Events::EventSubscriptionUVE damagedSub =
        eventSystem.Subscribe<Gameplay::HealthDamagedEventUVE>(
            [&](const Gameplay::HealthDamagedEventUVE& event) { damaged.push_back(event); });
    static_cast<void>(damagedSub);

    const HealthDamageResultUVE result =
        ApplyLagRayHitUVE(entityManager, eventSystem, shooter, hit);
    EXPECT_FALSE(result.applied);
    eventSystem.DispatchQueuedUVE();
    EXPECT_TRUE(damaged.empty());
}

TEST_F(HitboxLagWiringUVETest, EndToEnd_LaggedShooterStillHits) {
    LagHarnessUVE harness = MakeLaggedUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.TickUVE(PushUVE(1.0F)), 1U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    // The shooter's view comes from its own prediction stream: one tick in, one tick acked.
    const Physics::LagTickUVE view = ShooterViewTickUVE(harness.client);
    ASSERT_EQ(view, 1U);

    // The server recorded that tick while the boxes overlapped, then the target escaped.
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    MakeHealthUVE(target);
    ASSERT_TRUE(compensator.RecordUVE(view, entityManager));
    MoveUVE(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The present sees no hit - the Entered below exists only because the rewound pair
    // merged in before the diff.
    Physics::Hitbox3DSyncReportUVE live = Physics::SyncHitboxes3DUVE(entityManager);
    ASSERT_TRUE(live.strikes.empty());
    const Physics::LagShotReportUVE compensated =
        ResolveLagShotUVE(compensator, shooter, view, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    MergeLagShotUVE(live, compensated);

    Physics::Hitbox3DStrikeLifecycleTrackerUVE tracker;
    const Physics::Hitbox3DStrikeLifecycleReportUVE lifecycle = tracker.UpdateUVE(live);
    ASSERT_EQ(lifecycle.transitions.size(), 1U);
    const HealthDamageResultUVE damage = ApplyHitboxStrikeToHealthUVE(
        entityManager, lifecycle.transitions[0].strike.hurtbox);
    EXPECT_TRUE(damage.applied);
    EXPECT_FLOAT_EQ(damage.remaining, 99.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(target).health, 99.0F);
}

TEST_F(HitboxLagWiringUVETest, ResolveLagShotAt_InvalidTickFailsClosed) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // No view, no hit - at any fraction.
    const Physics::LagShotReportUVE closed = ResolveLagShotAtUVE(
        compensator, shooter, Network::kInvalidPredictionTickUVE, 0.5F, entityManager);
    EXPECT_TRUE(closed.pairs.empty());

    // The contrast: the raw compensator clamps tick 0 to the oldest floor and HITS.
    const Physics::LagShotReportUVE raw =
        compensator.ResolveShotAtUVE(shooter, 0U, 0.5F, entityManager);
    ASSERT_EQ(raw.pairs.size(), 1U);
    EXPECT_TRUE(raw.clamped);
    EXPECT_FALSE(raw.interpolated);
}

TEST_F(HitboxLagWiringUVETest, ResolveLagShotAt_ForwardsFraction) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {-4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The midpoint victim, through the guard: identical to the raw fractional resolve.
    const Physics::LagShotReportUVE wired =
        ResolveLagShotAtUVE(compensator, shooter, 1U, 0.5F, entityManager);
    EXPECT_EQ(wired, compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager));
    ASSERT_EQ(wired.pairs.size(), 1U);
    EXPECT_TRUE(wired.interpolated);
}

} // namespace
} // namespace UVE::Scene::Tests
