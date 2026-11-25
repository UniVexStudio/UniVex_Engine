// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_overlap_system.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "area_component.h"
#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

class AreaOverlapSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;

    Scene::Entity MakeAreaEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                       std::uint32_t collisionLayer = 1U,
                                       std::uint32_t collisionMask = 0xFFFFFFFFU) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::AreaComponent>(
            entity, Scene::AreaComponent{halfExtents, collisionLayer, collisionMask});
        return entity;
    }

    Scene::Entity MakeColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                           std::uint32_t collisionLayer = 1U,
                                           std::uint32_t collisionMask = 0xFFFFFFFFU) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{halfExtents, collisionLayer, collisionMask});
        return entity;
    }

    Scene::Entity MakeTypedColliderEntity(Math::Vector3 position,
                                                 const Scene::ColliderComponent& collider,
                                                 Math::Quaternion rotation = {}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }
};

TEST_F(AreaOverlapSystemTest, Query_ExactShapeRoutingRejectsDiagonalAabbFalsePositives) {
    MakeAreaEntity({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    Scene::ColliderComponent sphere;
    sphere.shapeType = Scene::ColliderShapeType::Sphere;
    sphere.radius = 0.5F;
    MakeTypedColliderEntity({0.9F, 0.9F, 0.0F}, sphere);
    ASSERT_EQ(AreaOverlapSystem::Query(entityManager).overlaps.size(), 0U);

    Scene::ColliderComponent capsule;
    capsule.shapeType = Scene::ColliderShapeType::Capsule;
    capsule.radius = 0.5F;
    capsule.height = 4.0F;
    MakeTypedColliderEntity({0.9F, 0.0F, 0.9F}, capsule);
    ASSERT_EQ(AreaOverlapSystem::Query(entityManager).overlaps.size(), 0U);

    Scene::ColliderComponent box;
    box.shapeType = Scene::ColliderShapeType::Box;
    box.halfExtents = {1.0F, 0.2F, 0.2F};
    MakeTypedColliderEntity({0.7F, 0.0F, 0.7F}, box,
                               Math::Quaternion{0.0F, 0.3826834324F, 0.0F, 0.9238795325F});

    EXPECT_TRUE(AreaOverlapSystem::Query(entityManager).overlaps.empty());
}

TEST_F(AreaOverlapSystemTest, Query_CompatibleOverlapReturnsCopiedPair) {
    const Scene::Entity area = MakeAreaEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 2U);
    const Scene::Entity collider = MakeColliderEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 1U);

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    ASSERT_EQ(result.inspectedAreas, 1U);
    ASSERT_EQ(result.inspectedColliders, 1U);
    ASSERT_FALSE(result.truncated);
    ASSERT_EQ(result.overlaps.size(), 1U);
    EXPECT_EQ(result.overlaps.front().area, area);
    EXPECT_EQ(result.overlaps.front().other, collider);
    EXPECT_GT(result.overlaps.front().penetrationDepth, 0.0F);
}

TEST_F(AreaOverlapSystemTest, Query_SkipsFiniteAreaBoundsThatOverflowPublication) {
    const Scene::Entity validArea =
        MakeAreaEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    MakeAreaEntity(Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F},
                      Math::Vector3{1.0e38F, 1.0F, 1.0F});
    const Scene::Entity collider =
        MakeColliderEntity(Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    ASSERT_EQ(result.inspectedAreas, 1U);
    ASSERT_EQ(result.inspectedColliders, 1U);
    ASSERT_EQ(result.overlaps.size(), 1U);
    EXPECT_EQ(result.overlaps.front().area, validArea);
    EXPECT_EQ(result.overlaps.front().other, collider);
    EXPECT_FALSE(result.truncated);
}

TEST_F(AreaOverlapSystemTest, Query_SkipsOverflowedFinitePenetrationDepth) {
    const float maximumFloat = std::numeric_limits<float>::max();
    MakeAreaEntity(Math::Vector3{0.0F, 0.0F, 0.0F},
                      Math::Vector3{maximumFloat, maximumFloat, maximumFloat});
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F},
                          Math::Vector3{maximumFloat, maximumFloat, maximumFloat});

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    EXPECT_EQ(result.inspectedAreas, 1U);
    EXPECT_EQ(result.inspectedColliders, 1U);
    EXPECT_TRUE(result.overlaps.empty());
    EXPECT_FALSE(result.truncated);
}

TEST_F(AreaOverlapSystemTest, Query_IncompatibleOrOneSidedMaskDoesNotReportOverlap) {
    MakeAreaEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 1U, 1U);
    MakeColliderEntity(Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}, 2U, 1U);

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    EXPECT_TRUE(result.overlaps.empty());
    EXPECT_FALSE(result.truncated);
}

TEST_F(AreaOverlapSystemTest, Query_MonitoringAndMonitorableFlagsControlAreaPairs) {
    const Scene::Entity firstArea = MakeAreaEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity secondArea = MakeAreaEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    Scene::AreaComponent& first = entityManager.GetComponent<Scene::AreaComponent>(firstArea);
    Scene::AreaComponent& second = entityManager.GetComponent<Scene::AreaComponent>(secondArea);
    first.collisionMask = 1U;
    second.collisionLayer = 1U;
    first.monitorable = false;
    second.monitorable = false;

    EXPECT_TRUE(AreaOverlapSystem::Query(entityManager).overlaps.empty());

    first.monitorable = true;
    second.monitorable = true;
    const AreaOverlapQueryResult bidirectional = AreaOverlapSystem::Query(entityManager);
    ASSERT_EQ(bidirectional.overlaps.size(), 2U);
    EXPECT_EQ(bidirectional.overlaps[0].area, firstArea);
    EXPECT_EQ(bidirectional.overlaps[0].other, secondArea);
    EXPECT_EQ(bidirectional.overlaps[1].area, secondArea);
    EXPECT_EQ(bidirectional.overlaps[1].other, firstArea);

    first.monitoring = false;
    EXPECT_TRUE(AreaOverlapSystem::Query(entityManager).overlaps.empty());
}

TEST_F(AreaOverlapSystemTest, Query_HardCapReportsTruncationAndStableFirstPair) {
    const Scene::Entity area = MakeAreaEntity(
        Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{2.0F, 2.0F, 2.0F});
    const Scene::Entity first = MakeColliderEntity(
        Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    MakeColliderEntity(Math::Vector3{-0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager, 1U);

    ASSERT_EQ(result.overlaps.size(), 1U);
    EXPECT_TRUE(result.truncated);
    EXPECT_EQ(result.overlaps.front().area, area);
    EXPECT_EQ(result.overlaps.front().other, first);
}

TEST_F(AreaOverlapSystemTest, Query_AreaCacheCapReportsTruncationAndInspectsBoundedPrefix) {
    for (std::size_t index = 0U; index < kMaximumAreaOverlapQueryAreas + 1U; ++index) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = Math::Vector3{static_cast<float>(index) * 4.0F, 0.0F, 0.0F};
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Scene::AreaComponent>(
            entity, Scene::AreaComponent{Math::Vector3{0.25F, 0.25F, 0.25F}, 1U, 0xFFFFFFFFU});
    }
    sceneGraph.Update(entityManager);

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    EXPECT_EQ(result.inspectedAreas, kMaximumAreaOverlapQueryAreas);
    EXPECT_TRUE(result.truncated);
    EXPECT_TRUE(result.overlaps.empty());
}

TEST_F(AreaOverlapSystemTest, Query_NoAreaEntitiesDoesNotTreatColliderPairsAsAreaOverlaps) {
    MakeColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    MakeColliderEntity(Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    const AreaOverlapQueryResult result = AreaOverlapSystem::Query(entityManager);

    EXPECT_EQ(result.inspectedAreas, 0U);
    EXPECT_EQ(result.inspectedColliders, 2U);
    EXPECT_TRUE(result.overlaps.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
