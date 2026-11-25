// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interest_relevancy.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "vector3.h"
#include "memory_manager.h"
#include "network_interest.h"
#include "network_replication.h"
#include "network_session.h"
#include "network_transport.h"
#include "replicated_entity.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class InterestRelevancyTest : public ::testing::Test {
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
};

struct InterestSides final {
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

[[nodiscard]] InterestSides MakeSides() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession sessionA(Network::SessionRole::Client,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession sessionB(Network::SessionRole::Server,
                                        std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkReplicator a(std::move(sessionA));
    Network::NetworkReplicator b(std::move(sessionB));
    return InterestSides{std::move(pair), std::move(a), std::move(b)};
}

TEST_F(InterestRelevancyTest, Bind_BindsEveryMapEntryToTheViewer) {
    InterestSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity near = MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{2.0F, 0.0F, 0.0F});
    const Entity far =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{50.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, near,
                                             7U, Network::ReplicationAuthority::Owner));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, far, 9U,
                                             Network::ReplicationAuthority::Owner));
    const Entity nearDoll =
        MakeDoll(entityManagerB, sceneGraphB, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Entity farDoll =
        MakeDoll(entityManagerB, sceneGraphB, Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB,
                                             nearDoll, 7U,
                                             Network::ReplicationAuthority::Proxy));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, farDoll,
                                             9U, Network::ReplicationAuthority::Proxy));

    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 2U);
    EXPECT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 2U);
    // Idempotent: rebinding swaps in an equivalent check and still counts both.
    EXPECT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 2U);

    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerB, nearDoll), (Math::Vector3{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, farDoll), (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_GE(sides.a.Stats().entriesSkippedIrrelevant, 1U);
}

TEST_F(InterestRelevancyTest, Bind_ShortCountOnDrift) {
    InterestSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));

    // An id the map holds but the replicator never saw: nothing binds.
    const Entity stray =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    mapA[99U] = stray;
    EXPECT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 0U);

    const Entity real =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, real,
                                             7U, Network::ReplicationAuthority::Owner));
    EXPECT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 1U);
}

TEST_F(InterestRelevancyTest, SyncEntities_UpsertsPositions) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    const Entity doll =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{2.0F, 0.0F, 0.0F});
    mapA[7U] = doll;

    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);
    EXPECT_EQ(interest.EntityCount(), 1U);
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    MoveDoll(entityManagerA, sceneGraphA, doll, Math::Vector3{50.0F, 0.0F, 0.0F});
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
}

TEST_F(InterestRelevancyTest, SyncEntities_SkipsDeadAndMutilated) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    const Entity live =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Entity doomed =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Entity stripped =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    mapA[7U] = live;
    mapA[8U] = doomed;
    mapA[9U] = stripped;
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 3U);

    entityManagerA.DestroyEntity(doomed);
    entityManagerA.RemoveComponent<WorldTransformComponent>(stripped);
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
}

TEST_F(InterestRelevancyTest, SyncEntities_PreservesAlwaysRelevant) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    // The boss pre-added with the flag: the sync moves it but never unflags it.
    ASSERT_TRUE(
        interest.AddEntity(9U, Math::Vector3{50.0F, 0.0F, 0.0F}, true));
    const Entity boss =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{50.0F, 0.0F, 0.0F});
    const Entity mob =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{2.0F, 0.0F, 0.0F});
    mapA[9U] = boss;
    mapA[7U] = mob;

    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 2U);
    EXPECT_TRUE(interest.IsRelevant(1U, 9U));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));

    MoveDoll(entityManagerA, sceneGraphA, mob, Math::Vector3{50.0F, 0.0F, 0.0F});
    MoveDoll(entityManagerA, sceneGraphA, boss, Math::Vector3{60.0F, 0.0F, 0.0F});
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 2U);
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));
    EXPECT_TRUE(interest.IsRelevant(1U, 9U));
}

TEST_F(InterestRelevancyTest, SyncViewer_FollowsEntity) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    const Entity pawn =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Entity cache =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{15.0F, 0.0F, 0.0F});
    mapA[7U] = cache;
    ASSERT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);

    EXPECT_TRUE(SyncInterestViewer(entityManagerA, interest, 1U, pawn));
    EXPECT_FALSE(interest.IsRelevant(1U, 7U));

    MoveDoll(entityManagerA, sceneGraphA, pawn, Math::Vector3{15.0F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncInterestViewer(entityManagerA, interest, 1U, pawn));
    EXPECT_TRUE(interest.IsRelevant(1U, 7U));
}

TEST_F(InterestRelevancyTest, SyncViewer_RefusesBadTargets) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    const Entity pawn =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_FALSE(SyncInterestViewer(entityManagerA, interest, 2U, pawn));
    EXPECT_TRUE(SyncInterestViewer(entityManagerA, interest, 1U, pawn));
    entityManagerA.DestroyEntity(pawn);
    EXPECT_FALSE(SyncInterestViewer(entityManagerA, interest, 1U, pawn));
}

TEST_F(InterestRelevancyTest, MakeInterestRelevancy_FailOpenUnknowns) {
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_TRUE(interest.AddEntity(7U, Math::Vector3{50.0F, 0.0F, 0.0F}));

    const Network::RelevancyCheck known = MakeInterestRelevancy(interest, 1U, 7U);
    EXPECT_FALSE(known());
    const Network::RelevancyCheck unknownViewer = MakeInterestRelevancy(interest, 9U, 7U);
    EXPECT_TRUE(unknownViewer());
    const Network::RelevancyCheck unknownEntity = MakeInterestRelevancy(interest, 1U, 8U);
    EXPECT_TRUE(unknownEntity());
    EXPECT_EQ(interest.Stats().unknownIds, 2U);
}

TEST_F(InterestRelevancyTest, FullLoop_ScenePositionsGateReplication) {
    InterestSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity near =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{2.0F, 0.0F, 0.0F});
    const Entity walker =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{50.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, near,
                                             7U, Network::ReplicationAuthority::Owner));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, walker,
                                             9U, Network::ReplicationAuthority::Owner));
    const Entity nearDoll =
        MakeDoll(entityManagerB, sceneGraphB, Math::Vector3{0.0F, 0.0F, 0.0F});
    const Entity walkerDoll =
        MakeDoll(entityManagerB, sceneGraphB, Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB,
                                             nearDoll, 7U,
                                             Network::ReplicationAuthority::Proxy));
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB,
                                             walkerDoll, 9U,
                                             Network::ReplicationAuthority::Proxy));

    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    ASSERT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 2U);
    ASSERT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 2U);

    sides.UpdateBoth(1100U);
    EXPECT_EQ(WorldPosition(entityManagerB, nearDoll), (Math::Vector3{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(WorldPosition(entityManagerB, walkerDoll), (Math::Vector3{0.0F, 0.0F, 0.0F}));

    // The walker strolls into range through the scene graph: the next pump ships it.
    MoveDoll(entityManagerA, sceneGraphA, walker, Math::Vector3{3.0F, 0.0F, 0.0F});
    ASSERT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 2U);
    sides.UpdateBoth(1200U);
    EXPECT_EQ(WorldPosition(entityManagerB, walkerDoll), (Math::Vector3{3.0F, 0.0F, 0.0F}));
}


TEST_F(InterestRelevancyTest, Tier_LowEntitySkipsMostSendTicks) {
    InterestSides sides = MakeSides();
    ASSERT_TRUE(sides.Establish(1000U));
    const Entity doll =
        MakeDoll(entityManagerA, sceneGraphA, Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerA, sceneGraphA, sides.a, mapA, doll,
                                             8U, Network::ReplicationAuthority::Owner));
    const Entity dollB =
        MakeDoll(entityManagerB, sceneGraphB, Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(RegisterReplicatedEntity(entityManagerB, sceneGraphB, sides.b, mapB, dollB,
                                             8U, Network::ReplicationAuthority::Proxy));
    Network::NetworkInterest interest;
    ASSERT_TRUE(interest.AddViewer(1U, 10.0F));
    EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);
    ASSERT_TRUE(interest.SetEntityTier(8U, Network::InterestTier::Low));
    EXPECT_EQ(BindInterestRelevancy(sides.a, interest, mapA, 1U), 1U);

    const std::uint64_t skippedBase = sides.a.Stats().entriesSkippedIrrelevant;
    for (std::uint64_t i = 0U; i < 8U; ++i) {
        MoveDoll(entityManagerA, sceneGraphA, doll,
                    Math::Vector3{static_cast<float>(i + 1U), 0.0F, 0.0F});
        EXPECT_EQ(SyncInterestEntities(entityManagerA, interest, mapA), 1U);
        sides.UpdateBoth(1100U + i * 100U);
    }
    // Low ships 1 in 8: the other 7 send ticks skipped the entity as irrelevant.
    EXPECT_EQ(sides.a.Stats().entriesSkippedIrrelevant - skippedBase, 7U);
    EXPECT_EQ(interest.Stats().throttled, 7U);
    EXPECT_EQ(interest.Stats().culled, 0U);
}
} // namespace
} // namespace UVE::Scene::Tests
