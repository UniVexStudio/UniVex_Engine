// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/replicated_entity_uve.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/predicted_character_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/gameplay_input_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/objects/3d/character_prediction_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class ReplicatedEntityUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystemA;
    Events::EventSystemUVE eventSystemB;
    EntityManagerUVE entityManagerA{memoryManager.GetDefaultAllocatorUVE(), eventSystemA};
    EntityManagerUVE entityManagerB{memoryManager.GetDefaultAllocatorUVE(), eventSystemB};
    SceneGraphUVE sceneGraphA;
    SceneGraphUVE sceneGraphB;
    ReplicatedEntityMapUVE mapA;
    ReplicatedEntityMapUVE mapB;

    [[nodiscard]] EntityUVE MakeDollUVE(IEntityManagerUVE& entityManager,
                                        SceneGraphUVE& sceneGraph,
                                        const Math::Vector3UVE position) {
        // A proxy's whole body: the transform triple, nothing simulated.
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    void MoveDollUVE(IEntityManagerUVE& entityManager, SceneGraphUVE& sceneGraph,
                     const EntityUVE entity, const Math::Vector3UVE position) {
        TransformComponentUVE transform =
            entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    [[nodiscard]] Math::Vector3UVE WorldPositionUVE(IEntityManagerUVE& entityManager,
                                                    const EntityUVE entity) {
        return entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    }

    [[nodiscard]] Math::Vector3UVE LocalPositionUVE(IEntityManagerUVE& entityManager,
                                                    const EntityUVE entity) {
        return entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
    }
};

struct ReplicatedSidesUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkReplicatorUVE a;
    Network::NetworkReplicatorUVE b;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return a.SessionUVE().StateUVE() == Network::SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] ReplicatedSidesUVE MakeSidesUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE sessionA(Network::SessionRoleUVE::Client,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE sessionB(Network::SessionRoleUVE::Server,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkReplicatorUVE a(std::move(sessionA));
    Network::NetworkReplicatorUVE b(std::move(sessionB));
    return ReplicatedSidesUVE{std::move(pair), std::move(a), std::move(b)};
}

struct TransferEventUVE final {
    Network::NetworkEntityIdUVE netId = 0U;
    Network::OwnershipTransferEventUVE event = Network::OwnershipTransferEventUVE::Requested;

    [[nodiscard]] bool operator==(const TransferEventUVE&) const = default;
};

TEST_F(ReplicatedEntityUVETest, SamplerHoldsLastKnownPose) {
    const EntityUVE doll = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Network::PositionSamplerUVE sampler =
        MakeReplicationSamplerUVE(entityManagerA, doll);
    ASSERT_TRUE(sampler);
    EXPECT_EQ(sampler(), (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));

    // A factory-time failure is an empty sampler, never a poisoned one.
    EXPECT_FALSE(
        MakeReplicationSamplerUVE(entityManagerA, entityManagerA.CreateEntityUVE()));

    // Death holds the last pose: sampled still, never zeroed, never crashing.
    entityManagerA.DestroyEntityUVE(doll);
    EXPECT_EQ(sampler(), (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_FALSE(MakeReplicationSamplerUVE(entityManagerA, doll));
}

TEST_F(ReplicatedEntityUVETest, ApplierWritesThroughGraph) {
    const EntityUVE doll = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const Network::PositionApplierUVE applier =
        MakeReplicationApplierUVE(entityManagerA, sceneGraphA, doll);
    applier(Math::Vector3UVE{4.0F, 5.0F, 6.0F});
    EXPECT_EQ(WorldPositionUVE(entityManagerA, doll), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(LocalPositionUVE(entityManagerA, doll), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));

    // A dead entity: skipped silently, never crashing.
    entityManagerA.DestroyEntityUVE(doll);
    applier(Math::Vector3UVE{7.0F, 8.0F, 9.0F});

    // A parented body: skipped, local pose untouched...
    const EntityUVE parent = MakeDollUVE(entityManagerA, sceneGraphA, {10.0F, 0.0F, 0.0F});
    const EntityUVE child = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    sceneGraphA.SetParentUVE(entityManagerA, child, parent);
    sceneGraphA.UpdateUVE(entityManagerA);
    const Math::Vector3UVE localBefore = LocalPositionUVE(entityManagerA, child);
    MakeReplicationApplierUVE(entityManagerA, sceneGraphA, child)(
        Math::Vector3UVE{9.0F, 9.0F, 9.0F});
    EXPECT_EQ(LocalPositionUVE(entityManagerA, child), localBefore);

    // ...and poison in never lands.
    const EntityUVE sound = MakeDollUVE(entityManagerA, sceneGraphA, {7.0F, 8.0F, 9.0F});
    const Network::PositionApplierUVE soundApplier =
        MakeReplicationApplierUVE(entityManagerA, sceneGraphA, sound);
    soundApplier(
        Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    EXPECT_EQ(WorldPositionUVE(entityManagerA, sound), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityUVETest, RegisterUnregisterRoundTrip) {
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE doll = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});

    EXPECT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                            7U, Network::ReplicationAuthorityUVE::Shared));
    EXPECT_EQ(FindReplicatedEntityUVE(mapA, 7U), doll);
    EXPECT_EQ(FindReplicatedNetIdUVE(mapA, doll),
              (std::optional<Network::NetworkEntityIdUVE>{7U}));
    EXPECT_EQ(FindReplicatedEntityUVE(mapA, 8U), kInvalidEntityUVE);
    EXPECT_FALSE(
        FindReplicatedNetIdUVE(mapA, entityManagerA.CreateEntityUVE()).has_value());

    // Duplicates refuse on both axes: the id and the entity.
    const EntityUVE other = MakeDollUVE(entityManagerA, sceneGraphA, {});
    EXPECT_FALSE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, other,
                                             7U, Network::ReplicationAuthorityUVE::Shared));
    EXPECT_FALSE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                             8U, Network::ReplicationAuthorityUVE::Shared));
    // The dead and the unsampleable refuse too.
    EXPECT_FALSE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA,
                                             entityManagerA.CreateEntityUVE(), 9U,
                                             Network::ReplicationAuthorityUVE::Shared));

    // Unregister forgets both sides of the mapping: the core reports never-registered after.
    EXPECT_TRUE(UnregisterReplicatedEntityUVE(sides.a, mapA, doll));
    EXPECT_TRUE(mapA.empty());
    EXPECT_FALSE(sides.a.UnregisterEntityUVE(7U));
    EXPECT_FALSE(UnregisterReplicatedEntityUVE(sides.a, mapA, doll));
    EXPECT_FALSE(UnregisterReplicatedEntityByIdUVE(sides.a, mapA, 7U));

    // By id, for the transfer answerer.
    EXPECT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, other,
                                            7U, Network::ReplicationAuthorityUVE::Proxy));
    EXPECT_TRUE(UnregisterReplicatedEntityByIdUVE(sides.a, mapA, 7U));
    EXPECT_TRUE(mapA.empty());
}

TEST_F(ReplicatedEntityUVETest, OwnerProxyLiveExchange) {
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE owner = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const EntityUVE proxy = MakeDollUVE(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, owner,
                                            7U, Network::ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, proxy,
                                            7U, Network::ReplicationAuthorityUVE::Proxy));

    // The owner's pose arrives...
    MoveDollUVE(entityManagerA, sceneGraphA, owner, {4.0F, 5.0F, 6.0F});
    sides.UpdateBothUVE(1100U);
    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, proxy), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));

    // ...and the proxy's never leaves: the owner holds while the local move stands.
    MoveDollUVE(entityManagerB, sceneGraphB, proxy, {7.0F, 8.0F, 9.0F});
    sides.UpdateBothUVE(1200U);
    sides.UpdateBothUVE(1200U);
    EXPECT_EQ(WorldPositionUVE(entityManagerA, owner), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, proxy), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityUVETest, SharedSequentialExchange) {
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE aDoll = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const EntityUVE bDoll = MakeDollUVE(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, aDoll,
                                            7U, Network::ReplicationAuthorityUVE::Shared));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, bDoll,
                                            7U, Network::ReplicationAuthorityUVE::Shared));

    // A moves first: whoever's snapshot lands first wins the tick, so both adopt A's pose.
    MoveDollUVE(entityManagerA, sceneGraphA, aDoll, {4.0F, 5.0F, 6.0F});
    sides.UpdateBothUVE(1100U);
    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerA, aDoll), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, bDoll), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));

    // Then B moves alone: A adopts in its turn.
    MoveDollUVE(entityManagerB, sceneGraphB, bDoll, {7.0F, 8.0F, 9.0F});
    sides.UpdateBothUVE(1200U);
    sides.UpdateBothUVE(1200U);
    EXPECT_EQ(WorldPositionUVE(entityManagerA, aDoll), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, bDoll), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
}

TEST_F(ReplicatedEntityUVETest, RelevancySkipsSend) {
    bool relevant = false;
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE owner = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const EntityUVE proxy = MakeDollUVE(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(
        entityManagerA, sceneGraphA, sides.a, mapA, owner, 7U,
        Network::ReplicationAuthorityUVE::Owner, [&relevant]() { return relevant; }));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, proxy,
                                            7U, Network::ReplicationAuthorityUVE::Proxy));

    // Irrelevant: neither sampled nor sent, counted out loud.
    MoveDollUVE(entityManagerA, sceneGraphA, owner, {4.0F, 5.0F, 6.0F});
    sides.UpdateBothUVE(1100U);
    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, proxy), (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    EXPECT_GT(sides.a.StatsUVE().entriesSkippedIrrelevant, 0U);

    // Interest returns: a first sighting with no baseline, shipped unconditionally.
    relevant = true;
    sides.UpdateBothUVE(1200U);
    sides.UpdateBothUVE(1200U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, proxy), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
}

TEST_F(ReplicatedEntityUVETest, PruneDropsDeadAndMutilated) {
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE survivor = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 0.0F, 0.0F});
    const EntityUVE doomed = MakeDollUVE(entityManagerA, sceneGraphA, {2.0F, 0.0F, 0.0F});
    const EntityUVE stripped = MakeDollUVE(entityManagerA, sceneGraphA, {3.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA,
                                            survivor, 1U,
                                            Network::ReplicationAuthorityUVE::Shared));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, doomed,
                                            2U, Network::ReplicationAuthorityUVE::Shared));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, stripped,
                                            3U, Network::ReplicationAuthorityUVE::Shared));
    EXPECT_EQ(PruneReplicatedEntitiesUVE(entityManagerA, sides.a, mapA), 0U);

    entityManagerA.DestroyEntityUVE(doomed);
    entityManagerA.RemoveComponentUVE<WorldTransformComponentUVE>(stripped);
    EXPECT_EQ(PruneReplicatedEntitiesUVE(entityManagerA, sides.a, mapA), 2U);
    EXPECT_EQ(mapA.size(), 1U);
    EXPECT_EQ(FindReplicatedEntityUVE(mapA, 1U), survivor);
    // The core forgot both: its own unregister now reports never-registered.
    EXPECT_FALSE(sides.a.UnregisterEntityUVE(2U));
    EXPECT_FALSE(sides.a.UnregisterEntityUVE(3U));
}

TEST_F(ReplicatedEntityUVETest, TransferHandoffMovesAuthority) {
    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE aDoll = MakeDollUVE(entityManagerA, sceneGraphA, {1.0F, 2.0F, 3.0F});
    const EntityUVE bDoll = MakeDollUVE(entityManagerB, sceneGraphB, {9.0F, 9.0F, 9.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, aDoll,
                                            7U, Network::ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, bDoll,
                                            7U, Network::ReplicationAuthorityUVE::Proxy));

    // Sanity first: the owner's pose arrives before the handoff.
    MoveDollUVE(entityManagerA, sceneGraphA, aDoll, {4.0F, 5.0F, 6.0F});
    sides.UpdateBothUVE(1100U);
    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, bDoll), (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));

    // B asks; A grants through the handler, finding its entity by net id as a game would.
    std::vector<TransferEventUVE> eventsA;
    std::vector<TransferEventUVE> eventsB;
    sides.a.SetOwnershipTransferHandlerUVE(
        [&](const Network::NetworkEntityIdUVE netId,
            const Network::OwnershipTransferEventUVE event) {
            eventsA.push_back(TransferEventUVE{netId, event});
            if (event == Network::OwnershipTransferEventUVE::Requested) {
                EXPECT_EQ(FindReplicatedEntityUVE(mapA, netId), aDoll);
                EXPECT_TRUE(sides.a.AcceptTransferUVE(netId));
            }
        });
    sides.b.SetOwnershipTransferHandlerUVE(
        [&](const Network::NetworkEntityIdUVE netId,
            const Network::OwnershipTransferEventUVE event) {
            eventsB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(sides.b.RequestOwnershipUVE(7U, 1200U));
    sides.UpdateBothUVE(1200U);
    sides.UpdateBothUVE(1200U);
    const TransferEventUVE completed{7U, Network::OwnershipTransferEventUVE::Completed};
    EXPECT_NE(std::find(eventsA.begin(), eventsA.end(), completed), eventsA.end());
    EXPECT_NE(std::find(eventsB.begin(), eventsB.end(), completed), eventsB.end());

    // After the handoff the direction reverses: B drives, A follows...
    MoveDollUVE(entityManagerB, sceneGraphB, bDoll, {7.0F, 8.0F, 9.0F});
    sides.UpdateBothUVE(1300U);
    sides.UpdateBothUVE(1300U);
    EXPECT_EQ(WorldPositionUVE(entityManagerA, aDoll), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, bDoll), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));

    // ...and A's proxy-side move never leaves: B holds while the local move stands.
    MoveDollUVE(entityManagerA, sceneGraphA, aDoll, {1.0F, 1.0F, 1.0F});
    sides.UpdateBothUVE(1400U);
    sides.UpdateBothUVE(1400U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, bDoll), (Math::Vector3UVE{7.0F, 8.0F, 9.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerA, aDoll), (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
}

TEST_F(ReplicatedEntityUVETest, PredictedPawnRidesReplicationAsOwner) {
    // The documented pairing: the client predicts its pawn and owns its replication entry;
    // the server holds a dumb doll as proxy. Two loopback pairs - prediction and
    // replication ride separate sessions, like separate channels would.
    const EntityUVE pawn = entityManagerA.CreateEntityUVE();
    TransformComponentUVE spawnTransform;
    sceneGraphA.AttachTransformUVE(entityManagerA, pawn, spawnTransform);
    ApplyCharacter3DObjectDefinitionUVE(entityManagerA, pawn,
                                        Character3DObjectDefinitionUVE::WithDefaultColliderUVE());
    sceneGraphA.UpdateUVE(entityManagerA);
    const EntityUVE driver = entityManagerA.CreateEntityUVE();
    ControllerComponentUVE controller;
    controller.kind = ControllerKindUVE::Player;
    entityManagerA.AddComponentUVE<ControllerComponentUVE>(driver, controller);
    ASSERT_TRUE(PossessControllerUVE(entityManagerA, driver, pawn));
    Gameplay::GameplayInputUVE push;
    push.move = {1.0F, 0.0F, 0.0F};
    entityManagerA.GetComponentUVE<PawnComponentUVE>(pawn).input = push;
    entityManagerA.AddComponentUVE<PredictedCharacterComponentUVE>(pawn,
                                                                  PredictedCharacterComponentUVE{});

    const EntityUVE doll = MakeDollUVE(entityManagerB, sceneGraphB, {0.0F, 0.0F, 0.0F});

    const std::optional<Network::PredictedStateUVE> spawn =
        PackPredictedStateUVE(entityManagerA, pawn);
    ASSERT_TRUE(spawn.has_value());

    Network::LoopbackTransportPairUVE predictionLink =
        Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE predictionSessionA(
        Network::SessionRoleUVE::Client,
        std::make_unique<Network::LoopbackEndpointUVE>(predictionLink.a));
    Network::NetworkSessionUVE predictionSessionB(
        Network::SessionRoleUVE::Server,
        std::make_unique<Network::LoopbackEndpointUVE>(predictionLink.b));
    Network::NetworkPredictionUVE predictorA(
        std::move(predictionSessionA), Network::SessionRoleUVE::Client,
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, -9.81F), *spawn);
    Network::NetworkPredictionUVE predictorB(
        std::move(predictionSessionB), Network::SessionRoleUVE::Server,
        MakeCharacterPredictionStepperUVE(CharacterControllerComponentUVE{}, -9.81F), *spawn);
    ASSERT_TRUE(predictorA.SessionUVE().ConnectUVE(1000U));
    for (int i = 0; i < 2; ++i) {
        predictorA.UpdateUVE(1000U);
        predictorB.UpdateUVE(1000U);
    }
    ASSERT_EQ(predictorA.SessionUVE().StateUVE(), Network::SessionStateUVE::Established);
    ASSERT_EQ(predictorB.SessionUVE().StateUVE(), Network::SessionStateUVE::Established);

    ReplicatedSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, pawn,
                                            7U, Network::ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, doll,
                                            7U, Network::ReplicationAuthorityUVE::Proxy));

    // Five fixed steps: predict, pump, sync, replicate - the doll tracks the pawn exactly.
    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        const std::uint64_t nowMs = 1000U + n * 100U;
        EXPECT_EQ(TickPredictedPawnUVE(entityManagerA, sceneGraphA, pawn, predictorA), n);
        predictorA.UpdateUVE(nowMs);
        predictorB.UpdateUVE(nowMs);
        predictorA.UpdateUVE(nowMs);
        predictorB.UpdateUVE(nowMs);
        EXPECT_TRUE(SyncPredictedPawnUVE(entityManagerA, sceneGraphA, pawn, predictorA));
        sides.UpdateBothUVE(nowMs);
        sides.UpdateBothUVE(nowMs);
        EXPECT_EQ(WorldPositionUVE(entityManagerB, doll),
                  WorldPositionUVE(entityManagerA, pawn));
    }
    EXPECT_EQ(predictorA.StatsUVE().reconciles, 0U);
    EXPECT_EQ(predictorA.LastAckedTickUVE(), 5U);
    EXPECT_EQ(predictorB.LastAppliedTickUVE(), 5U);
    EXPECT_GT(WorldPositionUVE(entityManagerB, doll).x, 0.0F);

    // The reverse never flows: a teleported doll is a local move on a proxy, and the pawn
    // it mirrors never hears about it.
    MoveDollUVE(entityManagerB, sceneGraphB, doll, {50.0F, 50.0F, 50.0F});
    sides.UpdateBothUVE(1600U);
    sides.UpdateBothUVE(1600U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, doll),
              (Math::Vector3UVE{50.0F, 50.0F, 50.0F}));
    EXPECT_NE(WorldPositionUVE(entityManagerA, pawn),
              (Math::Vector3UVE{50.0F, 50.0F, 50.0F}));
}

} // namespace
} // namespace UVE::Scene::Tests
