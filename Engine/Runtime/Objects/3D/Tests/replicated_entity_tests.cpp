// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "replicated_entity.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "character_controller_component.h"
#include "predicted_character_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gameplay_input.h"
#include "pawn.h"
#include "vector3.h"
#include "memory_manager.h"
#include "network_prediction.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"
#include "character_3d.h"
#include "character_prediction.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class ReplicatedEntityTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystemA;
    Events::EventSystem eventSystemB;
    EntityManager entityManagerA{memoryManager.GetDefaultAllocator(), eventSystemA};
    EntityManager entityManagerB{memoryManager.GetDefaultAllocator(), eventSystemB};
    SceneGraph sceneGraphA;
    SceneGraph sceneGraphB;
    ReplicatedEntityMap mapA;
    ReplicatedEntityMap mapB;

    [[nodiscard]] Entity MakeDoll(IEntityManager& entityManager,
                                        SceneGraph& sceneGraph,
                                        const Math::Vector3 position) {
        // A proxy's whole body: the transform triple, nothing simulated.
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        return entity;
    }

    void MoveDoll(IEntityManager& entityManager, SceneGraph& sceneGraph,
                     const Entity entity, const Math::Vector3 position) {
        TransformComponent transform =
            entityManager.GetComponent<TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    [[nodiscard]] Math::Vector3 WorldPosition(IEntityManager& entityManager,
                                                    const Entity entity) {
        return entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    }

    [[nodiscard]] Math::Vector3 LocalPosition(IEntityManager& entityManager,
                                                    const Entity entity) {
        return entityManager.GetComponent<TransformComponent>(entity).localPosition;
    }
};

struct ReplicatedSides final {
    Network::LoopbackTransportPair link;
    Network::NetworkReplicator a;
    Network::NetworkReplicator b;

    void UpdateBoth(const std::uint64_t nowMs) {
        a.Update(nowMs);
        b.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return a.Session().State() == Network::SessionState::Established &&
               b.Session().State() == Network::SessionState::Established;
    }
};

[[nodiscard]] ReplicatedSides MakeSides() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession sessionA(Network::SessionRole::Client,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession sessionB(Network::SessionRole::Server,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkReplicator a(std::move(sessionA));
    Network::NetworkReplicator b(std::move(sessionB));
    return ReplicatedSides{std::move(pair), std::move(a), std::move(b)};
}

struct TransferEvent final {
    Network::NetworkEntityId netId = 0U;
    Network::OwnershipTransferEvent event = Network::OwnershipTransferEvent::Requested;

    [[nodiscard]] bool operator==(const TransferEvent&) const = default;
};

TEST_F(ReplicatedEntityTest, SamplerHoldsLastKnownPose) {
    const Entity doll = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Network::PositionSampler sampler =
        MakeReplicationSampler(entityManagerA, doll);
    ASSERT_TRUE(sampler);
    EXPECT_EQ(sampler(), (Math::Vector3{1.0F, 2.0F, 3.0F}));

    // A factory-time failure is an empty sampler, never a poisoned one.
    EXPECT_FALSE(
        MakeReplicationSampler(entityManagerA, entityManagerA.CreateEntity()));

    // Death holds the last pose: sampled still, never zeroed, never crashing.
    entityManagerA.DestroyEntity(doll);
    EXPECT_EQ(sampler(), (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_FALSE(MakeReplicationSampler(entityManagerA, doll));
}

TEST_F(ReplicatedEntityTest, ApplierWritesThroughGraph) {
    const Entity doll = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Network::PositionApplier applier =
        MakeReplicationApplier(entityManagerA, sceneGraphA, doll);
    applier(Math::Vector3{4.0F, 5.0F, 6.0F});
    EXPECT_EQ(WorldPosition(entityManagerA, doll), (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(LocalPosition(entityManagerA, doll), (Math::Vector3{4.0F, 5.0F, 6.0F}));

    // A dead entity: skipped silently, never crashing.
    entityManagerA.DestroyEntity(doll);
    applier(Math::Vector3{7.0F, 8.0F, 9.0F});

    // A parented body: skipped, local pose untouched...
    const Entity parent = MakeDoll(entityManagerA, sceneGraphA, {10.0F, 0.0F, 0.0F});
    const Entity child = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    sceneGraphA.SetParent(entityManagerA, child, parent);
    sceneGraphA.Update(entityManagerA);
    const Math::Vector3 localBefore = LocalPosition(entityManagerA, child);
    MakeReplicationApplier(entityManagerA, sceneGraphA, child)(
        Math::Vector3{9.0F, 9.0F, 9.0F});
    EXPECT_EQ(LocalPosition(entityManagerA, child), localBefore);

    // ...and poison in never lands.
    const Entity sound = MakeDoll(entityManagerA, sceneGraphA, {7.0F, 8.0F, 9.0F});
    const Network::PositionApplier soundApplier =
        MakeReplicationApplier(entityManagerA, sceneGraphA, sound);
    soundApplier(
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    EXPECT_EQ(WorldPosition(entityManagerA, sound), (Math::Vector3{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityTest, RegisterUnregisterRoundTrip) {
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity doll = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});

    EXPECT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                            7U, Network::ReplicationAuthority::Shared));
    EXPECT_EQ(FindReplicatedEntity(mapA, 7U), doll);
    EXPECT_EQ(FindReplicatedNetId(mapA, doll),
              (std::optional<Network::NetworkEntityId>{7U}));
    EXPECT_EQ(FindReplicatedEntity(mapA, 8U), kInvalidEntity);
    EXPECT_FALSE(
        FindReplicatedNetId(mapA, entityManagerA.CreateEntity()).has_value());

    // Duplicates refuse on both axes: the id and the entity.
    const Entity other = MakeDoll(entityManagerA, sceneGraphA, {});
    EXPECT_FALSE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, other,
                                             7U, Network::ReplicationAuthority::Shared));
    EXPECT_FALSE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                             8U, Network::ReplicationAuthority::Shared));
    // The dead and the unsampleable refuse too.
    EXPECT_FALSE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA,
                                             entityManagerA.CreateEntity(), 9U,
                                             Network::ReplicationAuthority::Shared));

    // Unregister forgets both sides of the mapping: the core reports never-registered after.
    EXPECT_TRUE(UnregisterReplicatedEntity(sides.a, mapA, doll));
    EXPECT_TRUE(mapA.empty());
    EXPECT_FALSE(sides.a.UnregisterEntity(7U));
    EXPECT_FALSE(UnregisterReplicatedEntity(sides.a, mapA, doll));
    EXPECT_FALSE(UnregisterReplicatedEntityById(sides.a, mapA, 7U));

    // By id, for the transfer answerer.
    EXPECT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, other,
                                            7U, Network::ReplicationAuthority::Proxy));
    EXPECT_TRUE(UnregisterReplicatedEntityById(sides.a, mapA, 7U));
    EXPECT_TRUE(mapA.empty());
}

TEST_F(ReplicatedEntityTest, OwnerProxyLiveExchange) {
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity owner = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Entity proxy = MakeDoll(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, owner,
                                            7U, Network::ReplicationAuthority::Owner));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, proxy,
                                            7U, Network::ReplicationAuthority::Proxy));

    // The owner's pose arrives...
    MoveDoll(entityManagerA, sceneGraphA, owner, {4.0F, 5.0F, 6.0F});
    sides.UpdateBoth(1100U);
    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerB, proxy), (Math::Vector3{4.0F, 5.0F, 6.0F}));

    // ...and the proxy's never leaves: the owner holds while the local move stands.
    MoveDoll(entityManagerB, sceneGraphB, proxy, {7.0F, 8.0F, 9.0F});
    sides.UpdateBoth(1200U);
    sides.UpdateBoth(1200U);
    EXPECT_EQ(WorldPosition(entityManagerA, owner), (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, proxy), (Math::Vector3{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityTest, SharedSequentialExchange) {
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity aDoll = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Entity bDoll = MakeDoll(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, aDoll,
                                            7U, Network::ReplicationAuthority::Shared));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, bDoll,
                                            7U, Network::ReplicationAuthority::Shared));

    // A moves first: whoever's snapshot lands first wins the tick, so both adopt A's pose.
    MoveDoll(entityManagerA, sceneGraphA, aDoll, {4.0F, 5.0F, 6.0F});
    sides.UpdateBoth(1100U);
    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerA, aDoll), (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, bDoll), (Math::Vector3{4.0F, 5.0F, 6.0F}));

    // Then B moves alone: A adopts in its turn.
    MoveDoll(entityManagerB, sceneGraphB, bDoll, {7.0F, 8.0F, 9.0F});
    sides.UpdateBoth(1200U);
    sides.UpdateBoth(1200U);
    EXPECT_EQ(WorldPosition(entityManagerA, aDoll), (Math::Vector3{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, bDoll), (Math::Vector3{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityTest, RelevancySkipsSend) {
    bool relevant = false;
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity owner = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Entity proxy = MakeDoll(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(
        entityManagerA, sceneGraphA, sides.a, mapA, owner, 7U,
        Network::ReplicationAuthority::Owner, [&relevant]() { return relevant; }));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, proxy,
                                            7U, Network::ReplicationAuthority::Proxy));

    // Irrelevant: neither sampled nor sent, counted out loud.
    MoveDoll(entityManagerA, sceneGraphA, owner, {4.0F, 5.0F, 6.0F});
    sides.UpdateBoth(1100U);
    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerB, proxy), (Math::Vector3{9.0F, 9.0F, 9.0F}));
    EXPECT_GT(sides.a.Stats().entriesSkippedIrrelevant, 0U);

    // Interest returns: a first sighting with no baseline, shipped unconditionally.
    relevant = true;
    sides.UpdateBoth(1200U);
    sides.UpdateBoth(1200U);
    EXPECT_EQ(WorldPosition(entityManagerB, proxy), (Math::Vector3{4.0F, 5.0F, 6.0F}));
}

TEST_F(ReplicatedEntityTest, PruneDropsDeadAndMutilated) {
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity survivor = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 0.0F, 0.0F});
    const Entity doomed = MakeDoll(entityManagerA, sceneGraphA, {2.0F, 0.0F, 0.0F});
    const Entity stripped = MakeDoll(entityManagerA, sceneGraphA, {3.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA,
                                            survivor, 1U,
                                            Network::ReplicationAuthority::Shared));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, doomed,
                                            2U, Network::ReplicationAuthority::Shared));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, stripped,
                                            3U, Network::ReplicationAuthority::Shared));
    EXPECT_EQ(PruneReplicatedEntities(entityManagerA, sides.a, mapA), 0U);

    entityManagerA.DestroyEntity(doomed);
    entityManagerA.RemoveComponent<WorldTransformComponent>(stripped);
    EXPECT_EQ(PruneReplicatedEntities(entityManagerA, sides.a, mapA), 2U);
    EXPECT_EQ(mapA.size(), 1U);
    EXPECT_EQ(FindReplicatedEntity(mapA, 1U), survivor);
    // The core forgot both: its own unregister now reports never-registered.
    EXPECT_FALSE(sides.a.UnregisterEntity(2U));
    EXPECT_FALSE(sides.a.UnregisterEntity(3U));
}

TEST_F(ReplicatedEntityTest, TransferHandoffMovesAuthority) {
    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity aDoll = MakeDoll(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Entity bDoll = MakeDoll(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, aDoll,
                                            7U, Network::ReplicationAuthority::Owner));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, bDoll,
                                            7U, Network::ReplicationAuthority::Proxy));

    // Sanity first: the owner's pose arrives before the handoff.
    MoveDoll(entityManagerA, sceneGraphA, aDoll, {4.0F, 5.0F, 6.0F});
    sides.UpdateBoth(1100U);
    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerB, bDoll), (Math::Vector3{4.0F, 5.0F, 6.0F}));

    // B asks; A grants through the handler, finding its entity by net id as a game would.
    std::vector<TransferEvent> eventsA;
    std::vector<TransferEvent> eventsB;
    sides.a.SetOwnershipTransferHandler(
        [&](const Network::NetworkEntityId netId,
            const Network::OwnershipTransferEvent event) {
            eventsA.push_back(TransferEvent{netId, event});
            if (event == Network::OwnershipTransferEvent::Requested) {
                EXPECT_EQ(FindReplicatedEntity(mapA, netId), aDoll);
                EXPECT_TRUE(sides.a.AcceptTransfer(netId));
            }
        });
    sides.b.SetOwnershipTransferHandler(
        [&](const Network::NetworkEntityId netId,
            const Network::OwnershipTransferEvent event) {
            eventsB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(sides.b.RequestOwnership(7U, 1200U));
    sides.UpdateBoth(1200U);
    sides.UpdateBoth(1200U);
    const TransferEvent completed{7U, Network::OwnershipTransferEvent::Completed};
    EXPECT_NE(std::find(eventsA.begin(), eventsA.end(), completed), eventsA.end());
    EXPECT_NE(std::find(eventsB.begin(), eventsB.end(), completed), eventsB.end());

    // After the handoff the direction reverses: B drives, A follows...
    MoveDoll(entityManagerB, sceneGraphB, bDoll, {7.0F, 8.0F, 9.0F});
    sides.UpdateBoth(1300U);
    sides.UpdateBoth(1300U);
    EXPECT_EQ(WorldPosition(entityManagerA, aDoll), (Math::Vector3{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, bDoll), (Math::Vector3{7.0F, 8.0F, 9.0F}));

    // ...and A's proxy-side move never leaves: B holds while the local move stands.
    MoveDoll(entityManagerA, sceneGraphA, aDoll, {1.0F, 1.0F, 1.0F});
    sides.UpdateBoth(1400U);
    sides.UpdateBoth(1400U);
    EXPECT_EQ(WorldPosition(entityManagerB, bDoll), (Math::Vector3{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPosition(entityManagerA, aDoll), (Math::Vector3{1.0F, 1.0F, 1.0F}));
}

TEST_F(ReplicatedEntityTest, PredictedPawnRidesReplicationAsOwner) {
    // The documented pairing: the client predicts its pawn and owns its replication entry;
    // the server holds a dumb doll as proxy. Two loopback pairs - prediction and
    // replication ride separate sessions, like separate channels would.
    const Entity pawn = entityManagerA.CreateEntity();
    TransformComponent spawnTransform;
    sceneGraphA.AttachTransform(entityManagerA, pawn, spawnTransform);
    ApplyCharacter3DObjectDefinition(entityManagerA, pawn,
                                        Character3DObjectDefinition::WithDefaultCollider());
    sceneGraphA.Update(entityManagerA);
    const Entity driver = entityManagerA.CreateEntity();
    ControllerComponent controller;
    controller.kind = ControllerKind::Player;
    entityManagerA.AddComponent<ControllerComponent>(driver, controller);
    ASSERT_TRUE(PossessController(entityManagerA, driver, pawn));
    Gameplay::GameplayInput push;
    push.move = {1.0F, 0.0F, 0.0F};
    entityManagerA.GetComponent<PawnComponent>(pawn).input = push;
    entityManagerA.AddComponent<PredictedCharacterComponent>(pawn,
                                                                  PredictedCharacterComponent{});

    const Entity doll = MakeDoll(entityManagerB, sceneGraphB, {0.0F, 0.0F, 0.0F});

    const std::optional<Network::PredictedState> spawn =
        PackPredictedState(entityManagerA, pawn);
    ASSERT_TRUE(spawn.has_value());

    Network::LoopbackTransportPair predictionLink =
        Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession predictionSessionA(
        Network::SessionRole::Client,
        std::make_unique<Network::LoopbackEndpoint>(predictionLink.a));
    Network::NetworkSession predictionSessionB(
        Network::SessionRole::Server,
        std::make_unique<Network::LoopbackEndpoint>(predictionLink.b));
    Network::NetworkPrediction predictorA(
        std::move(predictionSessionA), Network::SessionRole::Client,
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, -9.81F), *spawn);
    Network::NetworkPrediction predictorB(
        std::move(predictionSessionB), Network::SessionRole::Server,
        MakeCharacterPredictionStepper(CharacterControllerComponent{}, -9.81F), *spawn);
    ASSERT_TRUE(predictorA.Session().Connect(1000U));
    for (int i = 0; i < 2; ++i) {
        predictorA.Update(1000U);
        predictorB.Update(1000U);
    }
    ASSERT_EQ(predictorA.Session().State(), Network::SessionState::Established);
    ASSERT_EQ(predictorB.Session().State(), Network::SessionState::Established);

    ReplicatedSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, pawn,
                                            7U, Network::ReplicationAuthority::Owner));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, doll,
                                            7U, Network::ReplicationAuthority::Proxy));

    // Five fixed steps: predict, pump, sync, replicate - the doll tracks the pawn exactly.
    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        const std::uint64_t nowMs = 1000U + n * 100U;
        EXPECT_EQ(TickPredictedPawn(entityManagerA, sceneGraphA, pawn, predictorA), n);
        predictorA.Update(nowMs);
        predictorB.Update(nowMs);
        predictorA.Update(nowMs);
        predictorB.Update(nowMs);
        EXPECT_TRUE(SyncPredictedPawn(entityManagerA, sceneGraphA, pawn, predictorA));
        sides.UpdateBoth(nowMs);
        sides.UpdateBoth(nowMs);
        EXPECT_EQ(WorldPosition(entityManagerB, doll),
                  WorldPosition(entityManagerA, pawn));
    }
    EXPECT_EQ(predictorA.Stats().reconciles, 0U);
    EXPECT_EQ(predictorA.LastAckedTick(), 5U);
    EXPECT_EQ(predictorB.LastAppliedTick(), 5U);
    EXPECT_GT(WorldPosition(entityManagerB, doll).x, 0.0F);

    // The reverse never flows: a teleported doll is a local move on a proxy, and the pawn
    // it mirrors never hears about it.
    MoveDoll(entityManagerB, sceneGraphB, doll, {50.0F, 50.0F, 50.0F});
    sides.UpdateBoth(1600U);
    sides.UpdateBoth(1600U);
    EXPECT_EQ(WorldPosition(entityManagerB, doll),
              (Math::Vector3{50.0F, 50.0F, 50.0F}));
    EXPECT_NE(WorldPosition(entityManagerA, pawn),
              (Math::Vector3{50.0F, 50.0F, 50.0F}));
}

} // namespace
} // namespace UVE::Scene::Tests
