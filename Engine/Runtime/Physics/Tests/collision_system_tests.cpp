// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "collision_system.h"
#include "collider_world_aabb_cache.h"

#include <algorithm>
#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

class CollisionSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                           std::uint32_t collisionLayer = 1U,
                                           std::uint32_t collisionMask = 0xFFFFFFFFU) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = collisionLayer;
        collider.collisionMask = collisionMask;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] static bool ContainsPair(const std::vector<CollisionPair>& pairs, Scene::Entity a,
                                               Scene::Entity b) {
        return std::any_of(pairs.begin(), pairs.end(), [a, b](const CollisionPair& pair) {
            return (pair.first == a && pair.second == b) || (pair.first == b && pair.second == a);
        });
    }
};

TEST_F(CollisionSystemTest, DetectCollisions_OverlappingEntities_ReturnsOnePair) {
    const Scene::Entity a = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity b = MakeColliderEntity(Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);

    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_TRUE(ContainsPair(pairs, a, b));
}

TEST_F(CollisionSystemTest, DetectCollisions_DisjointEntities_ReturnsNoPairs) {
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    MakeColliderEntity(Math::Vector3{100.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(CollisionSystemTest, DetectCollisions_EntityMissingColliderComponent_IsExcluded) {
    // A plain scene-graph entity with a transform but no ColliderComponent.
    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    sceneGraph.Update(entityManager);
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(CollisionSystemTest, BuildColliderWorldAabbCache_SkipsFiniteBoundsThatOverflowPublication) {
    const Scene::Entity validEntity =
        MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity overflowedEntity = MakeColliderEntity(
        Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F},
        Math::Vector3{1.0e38F, 1.0F, 1.0F});

    const std::vector<Detail::ColliderWorldAabb> cache = Detail::BuildColliderWorldAabbCache(entityManager);

    ASSERT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.front().entity, validEntity);
    EXPECT_NE(cache.front().entity, overflowedEntity);
    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
}

TEST_F(CollisionSystemTest, BuildColliderWorldAabbCache_CapsCopiedProxiesAtBvhLimit) {
    for (std::size_t index = 0U; index < Detail::DynamicAabbBvh::kMaximumProxies + 1U; ++index) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = Math::Vector3{static_cast<float>(index) * 3.0F, 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, local);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});
    }
    sceneGraph.Update(entityManager);

    const std::vector<Detail::ColliderWorldAabb> cache = Detail::BuildColliderWorldAabbCache(entityManager);
    ASSERT_EQ(cache.size(), Detail::DynamicAabbBvh::kMaximumProxies);
    EXPECT_TRUE(Detail::DynamicAabbBvh(cache).IsValid());
}

TEST_F(CollisionSystemTest, DetectCollisions_MultipleSimultaneousOverlaps_AllReported) {
    const Scene::Entity a = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity b = MakeColliderEntity(Math::Vector3{1.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity c = MakeColliderEntity(Math::Vector3{3.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);

    ASSERT_EQ(pairs.size(), 2U);
    EXPECT_TRUE(ContainsPair(pairs, a, b));
    EXPECT_TRUE(ContainsPair(pairs, b, c));
    EXPECT_FALSE(ContainsPair(pairs, a, c));
}

TEST_F(CollisionSystemTest, DetectCollisions_CompatibleLayerMasks_ReportOverlap) {
    const Scene::Entity a = MakeColliderEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 2U);
    const Scene::Entity b = MakeColliderEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 1U);

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);

    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_TRUE(ContainsPair(pairs, a, b));
}

TEST_F(CollisionSystemTest, DetectCollisions_IncompatibleLayerMask_RejectsOverlap) {
    const Scene::Entity a = MakeColliderEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 1U);
    const Scene::Entity b = MakeColliderEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 1U);

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
    EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(a).collisionMask, 1U);
    EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(b).collisionMask, 1U);
}

TEST_F(CollisionSystemTest, DetectCollisions_OneSidedMaskAcceptance_StillRejectsOverlap) {
    const Scene::Entity a = MakeColliderEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 2U);
    const Scene::Entity b = MakeColliderEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 4U);

    EXPECT_TRUE(collisionSystem.DetectCollisions(entityManager).empty());
    EXPECT_TRUE(entityManager.GetComponent<Scene::ColliderComponent>(a).collisionMask & 2U);
    EXPECT_FALSE(entityManager.GetComponent<Scene::ColliderComponent>(b).collisionMask & 1U);
}

TEST_F(CollisionSystemTest, DetectCollisions_BvhPreservesLegacyPairOrderAcrossSpatialClusters) {
    const Scene::Entity a = MakeColliderEntity({0.0F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});
    const Scene::Entity b = MakeColliderEntity({10.0F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});
    const Scene::Entity c = MakeColliderEntity({0.5F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});
    const Scene::Entity d = MakeColliderEntity({10.5F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});
    const Scene::Entity e = MakeColliderEntity({1.0F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});
    MakeColliderEntity({100.0F, 0.0F, 0.0F}, {0.75F, 0.75F, 0.75F});

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);

    ASSERT_EQ(pairs.size(), 4U);
    EXPECT_EQ(pairs[0].first, a);
    EXPECT_EQ(pairs[0].second, c);
    EXPECT_EQ(pairs[1].first, a);
    EXPECT_EQ(pairs[1].second, e);
    EXPECT_EQ(pairs[2].first, b);
    EXPECT_EQ(pairs[2].second, d);
    EXPECT_EQ(pairs[3].first, c);
    EXPECT_EQ(pairs[3].second, e);
}

TEST_F(CollisionSystemTest, DetectCollisions_StaticVsStaticOverlap_IsStillReported) {
    // Both entities have ColliderComponent but neither has Rigid3DComponent — pure
    // static world geometry. Detection doesn't care; resolution (PhysicsSystem) does.
    const Scene::Entity a = MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity b = MakeColliderEntity(Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const std::vector<CollisionPair> pairs = collisionSystem.DetectCollisions(entityManager);

    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_TRUE(ContainsPair(pairs, a, b));
}

TEST(DynamicAabbBvhTest, UpdateAabb_RefitsMovedProxyAndPreservesCacheIndexOrder) {
    const Scene::Entity firstEntity{1U, 1U};
    const Scene::Entity secondEntity{2U, 1U};
    Detail::DynamicAabbBvh bvh(std::vector<Detail::ColliderWorldAabb>{
        {firstEntity, Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}), 1U, 0xFFFFFFFFU},
        {secondEntity, Math::Aabb::FromCenterExtents({100.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}), 1U, 0xFFFFFFFFU},
    });
    ASSERT_TRUE(bvh.IsValid());

    const Math::Aabb farBounds =
        Math::Aabb::FromCenterExtents({1000.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});
    std::vector<std::size_t> candidates;
    ASSERT_TRUE(bvh.Query(farBounds, candidates));
    EXPECT_TRUE(candidates.empty());

    const Math::Aabb movedAabb =
        Math::Aabb::FromCenterExtents({1000.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const auto update = bvh.UpdateAabb(firstEntity, movedAabb);
    ASSERT_TRUE(update.IsApplied());
    ASSERT_TRUE(bvh.Query(farBounds, candidates));
    ASSERT_FALSE(candidates.empty());
    EXPECT_TRUE(std::is_sorted(candidates.begin(), candidates.end()));
    EXPECT_NE(std::find(candidates.begin(), candidates.end(), 0U), candidates.end());
    EXPECT_EQ(bvh.GetColliders()[0].entity, firstEntity);
    EXPECT_FLOAT_EQ(bvh.GetColliders()[0].worldAabb.min.x, 999.0F);

    ASSERT_TRUE(bvh.UpdateAabb(
                    firstEntity, Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}))
                    .IsApplied());
    ASSERT_TRUE(bvh.Query(farBounds, candidates));
    EXPECT_TRUE(candidates.empty());
}

TEST(DynamicAabbBvhTest, UpdateAabb_RejectsInvalidAndStaleProxyIdentities) {
    const Scene::Entity entity{7U, 4U};
    Detail::DynamicAabbBvh bvh(std::vector<Detail::ColliderWorldAabb>{
        {entity, Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}), 1U, 0xFFFFFFFFU},
    });
    const Math::Aabb validAabb = Math::Aabb::FromCenterExtents({1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});

    EXPECT_EQ(bvh.UpdateAabb(Scene::kInvalidEntity, validAabb).code,
              Detail::DynamicColliderWorldAabbUpdateCode::InvalidEntity);
    EXPECT_EQ(bvh.UpdateAabb({7U, 3U}, validAabb).code,
              Detail::DynamicColliderWorldAabbUpdateCode::StaleGeneration);
    EXPECT_EQ(bvh.UpdateAabb({8U, 1U}, validAabb).code,
              Detail::DynamicColliderWorldAabbUpdateCode::UnknownProxy);
    const Math::Aabb invalidAabb{
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    EXPECT_EQ(bvh.UpdateAabb(entity, invalidAabb).code,
              Detail::DynamicColliderWorldAabbUpdateCode::InvalidAabb);
}

} // namespace
} // namespace UVE::Physics::Tests
