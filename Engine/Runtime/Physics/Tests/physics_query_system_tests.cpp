// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "physics_query_system.h"

#include <gtest/gtest.h>

#include "event_system.h"
#include "ray.h"
#include "memory_manager.h"
#include "collision_system.h"
#include "area_component.h"
#include "collider_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

class PhysicsQuerySystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;
    PhysicsQuerySystem querySystem{collisionSystem};

    Scene::Entity MakeCollider(Math::Vector3 position, Math::Vector3 halfExtents) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(
            entity, Scene::ColliderComponent{halfExtents});
        return entity;
    }

    Scene::Entity MakeArea(Math::Vector3 position, Math::Vector3 halfExtents) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::AreaComponent>(
            entity, Scene::AreaComponent{halfExtents, 1U, 0xFFFFFFFFU});
        return entity;
    }

    [[nodiscard]] Math::Vector3 WorldPosition(Scene::Entity entity) const {
        return entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    }
};

TEST_F(PhysicsQuerySystemTest, SphereCastAreaOverlapAndCharacterMove_DelegateThroughOneFacade) {
    const Scene::Entity castTarget = MakeCollider({5.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    const Scene::Entity area = MakeArea({0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F});
    const Scene::Entity overlapTarget = MakeCollider({0.5F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity controller = MakeCollider({-5.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    SphereCastQuery castQuery;
    castQuery.ray = Math::Ray{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    castQuery.radius = 0.25F;
    castQuery.maxDistance = 20.0F;
    castQuery.ignoreEntity = overlapTarget;
    const std::optional<SphereCastHit> hit = querySystem.SphereCast(entityManager, castQuery);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, castTarget);

    const AreaOverlapQueryResult overlaps = querySystem.QueryAreaOverlaps(entityManager, 8U);
    EXPECT_FALSE(overlaps.IsTruncated());
    ASSERT_EQ(overlaps.overlaps.size(), 1U);
    EXPECT_EQ(overlaps.overlaps.front().area, area);
    EXPECT_EQ(overlaps.overlaps.front().other, overlapTarget);

    CharacterControllerInput input;
    input.entity = controller;
    input.desiredDisplacement = {1.0F, 0.0F, 0.0F};
    input.maximumSubstepDistance = 1.0F;
    const CharacterControllerMoveResult moved =
        querySystem.MoveCharacter(entityManager, sceneGraph, input);
    EXPECT_TRUE(moved.IsAccepted());
    EXPECT_FLOAT_EQ(WorldPosition(controller).x, -4.0F);
}

TEST_F(PhysicsQuerySystemTest, InvalidCharacterInput_IsRejectedWithoutMovingAnyEntity) {
    const Scene::Entity entity = MakeCollider({2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    CharacterControllerInput input;
    input.entity = Scene::kInvalidEntity;
    input.desiredDisplacement = {1.0F, 0.0F, 0.0F};

    const CharacterControllerMoveResult moved =
        querySystem.MoveCharacterWithToI(entityManager, sceneGraph, input);
    EXPECT_EQ(moved.code, CharacterControllerMoveCode::InvalidEntity);
    EXPECT_FLOAT_EQ(WorldPosition(entity).x, 2.0F);
}

} // namespace
} // namespace UVE::Physics::Tests
