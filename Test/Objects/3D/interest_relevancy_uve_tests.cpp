// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/interest_relevancy_uve.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_interest_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/objects/3d/replicated_entity_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class InterestRelevancyUVETest : public ::testing::Test {
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
};

struct InterestSidesUVE final {
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

[[nodiscard]] InterestSidesUVE MakeSidesUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE sessionA(Network::SessionRoleUVE::Client,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE sessionB(Network::SessionRoleUVE::Server,
                                        std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkReplicatorUVE a(std::move(sessionA));
    Network::NetworkReplicatorUVE b(std::move(sessionB));
    return InterestSidesUVE{std::move(pair), std::move(a), std::move(b)};
}

TEST_F(InterestRelevancyUVETest, Bind_BindsEveryMapEntryToTheViewer) {
    InterestSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE near = MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    const EntityUVE far =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, near,
                                             7U, Network::ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, far, 9U,
                                             Network::ReplicationAuthorityUVE::Owner));
    const EntityUVE nearDoll =
        MakeDollUVE(entityManagerB, sceneGraphB, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const EntityUVE farDoll =
        MakeDollUVE(entityManagerB, sceneGraphB, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB,
                                             nearDoll, 7U,
                                             Network::ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, farDoll,
                                             9U, Network::ReplicationAuthorityUVE::Proxy));

    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 2U);
    EXPECT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 2U);
    // Idempotent: rebinding swaps in an equivalent check and still counts both.
    EXPECT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 2U);

    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, nearDoll), (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, farDoll), (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_GE(sides.a.StatsUVE().entriesSkippedIrrelevant, 1U);
}

TEST_F(InterestRelevancyUVETest, Bind_ShortCountOnDrift) {
    InterestSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));

    // An id the map holds but the replicator never saw: nothing binds.
    const EntityUVE stray =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    mapA[99U] = stray;
    EXPECT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 0U);

    const EntityUVE real =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, real,
                                             7U, Network::ReplicationAuthorityUVE::Owner));
    EXPECT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 1U);
}

TEST_F(InterestRelevancyUVETest, SyncEntities_UpsertsPositions) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    const EntityUVE doll =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    mapA[7U] = doll;

    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);
    EXPECT_EQ(interest.EntityCountUVE(), 1U);
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    MoveDollUVE(entityManagerA, sceneGraphA, doll, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
}

TEST_F(InterestRelevancyUVETest, SyncEntities_SkipsDeadAndMutilated) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    const EntityUVE live =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const EntityUVE doomed =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const EntityUVE stripped =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    mapA[7U] = live;
    mapA[8U] = doomed;
    mapA[9U] = stripped;
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 3U);

    entityManagerA.DestroyEntityUVE(doomed);
    entityManagerA.RemoveComponentUVE<WorldTransformComponentUVE>(stripped);
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
}

TEST_F(InterestRelevancyUVETest, SyncEntities_PreservesAlwaysRelevant) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    // The boss pre-added with the flag: the sync moves it but never unflags it.
    ASSERT_TRUE(
        interest.AddEntityUVE(9U, Math::Vector3UVE{50.0F, 0.0F, 0.0F}, true));
    const EntityUVE boss =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    const EntityUVE mob =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    mapA[9U] = boss;
    mapA[7U] = mob;

    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 2U);
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 9U));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));

    MoveDollUVE(entityManagerA, sceneGraphA, mob, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    MoveDollUVE(entityManagerA, sceneGraphA, boss, Math::Vector3UVE{60.0F, 0.0F, 0.0F});
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 2U);
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 9U));
}

TEST_F(InterestRelevancyUVETest, SyncViewer_FollowsEntity) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    const EntityUVE pawn =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const EntityUVE cache =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{15.0F, 0.0F, 0.0F});
    mapA[7U] = cache;
    ASSERT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);

    EXPECT_TRUE(SyncInterestViewerUVE(entityManagerA, interest, 1U, pawn));
    EXPECT_FALSE(interest.IsRelevantUVE(1U, 7U));

    MoveDollUVE(entityManagerA, sceneGraphA, pawn, Math::Vector3UVE{15.0F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncInterestViewerUVE(entityManagerA, interest, 1U, pawn));
    EXPECT_TRUE(interest.IsRelevantUVE(1U, 7U));
}

TEST_F(InterestRelevancyUVETest, SyncViewer_RefusesBadTargets) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    const EntityUVE pawn =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_FALSE(SyncInterestViewerUVE(entityManagerA, interest, 2U, pawn));
    EXPECT_TRUE(SyncInterestViewerUVE(entityManagerA, interest, 1U, pawn));
    entityManagerA.DestroyEntityUVE(pawn);
    EXPECT_FALSE(SyncInterestViewerUVE(entityManagerA, interest, 1U, pawn));
}

TEST_F(InterestRelevancyUVETest, MakeInterestRelevancy_FailOpenUnknowns) {
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntityUVE(7U, Math::Vector3UVE{50.0F, 0.0F, 0.0F}));

    const Network::RelevancyCheckUVE known = MakeInterestRelevancyUVE(interest, 1U, 7U);
    EXPECT_FALSE(known());
    const Network::RelevancyCheckUVE unknownViewer = MakeInterestRelevancyUVE(interest, 9U, 7U);
    EXPECT_TRUE(unknownViewer());
    const Network::RelevancyCheckUVE unknownEntity = MakeInterestRelevancyUVE(interest, 1U, 8U);
    EXPECT_TRUE(unknownEntity());
    EXPECT_EQ(interest.StatsUVE().unknownIds, 2U);
}

TEST_F(InterestRelevancyUVETest, FullLoop_ScenePositionsGateReplication) {
    InterestSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE near =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{2.0F, 0.0F, 0.0F});
    const EntityUVE walker =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{50.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, near,
                                             7U, Network::ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, walker,
                                             9U, Network::ReplicationAuthorityUVE::Owner));
    const EntityUVE nearDoll =
        MakeDollUVE(entityManagerB, sceneGraphB, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const EntityUVE walkerDoll =
        MakeDollUVE(entityManagerB, sceneGraphB, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB,
                                             nearDoll, 7U,
                                             Network::ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB,
                                             walkerDoll, 9U,
                                             Network::ReplicationAuthorityUVE::Proxy));

    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    ASSERT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 2U);
    ASSERT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 2U);

    sides.UpdateBothUVE(1100U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, nearDoll), (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(WorldPositionUVE(entityManagerB, walkerDoll), (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));

    // The walker strolls into range through the scene graph: the next pump ships it.
    MoveDollUVE(entityManagerA, sceneGraphA, walker, Math::Vector3UVE{3.0F, 0.0F, 0.0F});
    ASSERT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 2U);
    sides.UpdateBothUVE(1200U);
    EXPECT_EQ(WorldPositionUVE(entityManagerB, walkerDoll), (Math::Vector3UVE{3.0F, 0.0F, 0.0F}));
}


TEST_F(InterestRelevancyUVETest, Tier_LowEntitySkipsMostSendTicks) {
    InterestSidesUVE sides = MakeSidesUVE();
    ASSERT_TRUE(sides.EstablishUVE(1000U));
    const EntityUVE doll =
        MakeDollUVE(entityManagerA, sceneGraphA, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                             8U, Network::ReplicationAuthorityUVE::Owner));
    const EntityUVE dollB =
        MakeDollUVE(entityManagerB, sceneGraphB, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntityUVE(entityManagerB, sceneGraphB, sides.b, mapB, dollB,
                                             8U, Network::ReplicationAuthorityUVE::Proxy));
    Network::NetworkInterestUVE interest;
    ASSERT_TRUE(interest.AddViewerUVE(1U, 10.0F));
    EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);
    ASSERT_TRUE(interest.SetEntityTierUVE(8U, Network::InterestTierUVE::Low));
    EXPECT_EQ(BindInterestRelevancyUVE(sides.a, interest, mapA, 1U), 1U);

    const std::uint64_t skippedBase = sides.a.StatsUVE().entriesSkippedIrrelevant;
    for (std::uint64_t i = 0U; i < 8U; ++i) {
        MoveDollUVE(entityManagerA, sceneGraphA, doll,
                    Math::Vector3UVE{static_cast<float>(i + 1U), 0.0F, 0.0F});
        EXPECT_EQ(SyncInterestEntitiesUVE(entityManagerA, interest, mapA), 1U);
        sides.UpdateBothUVE(1100U + i * 100U);
    }
    // Low ships 1 in 8: the other 7 send ticks skipped the entity as irrelevant.
    EXPECT_EQ(sides.a.StatsUVE().entriesSkippedIrrelevant - skippedBase, 7U);
    EXPECT_EQ(interest.StatsUVE().throttled, 7U);
    EXPECT_EQ(interest.StatsUVE().culled, 0U);
}
} // namespace
} // namespace UVE::Scene::Tests
