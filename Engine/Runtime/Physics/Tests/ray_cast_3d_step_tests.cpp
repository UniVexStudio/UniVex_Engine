// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "ray_cast_3d.h"
#include "ray_cast_3d_step.h"
#include "raycast_system.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Entity;
using Scene::RayCast3DComponent;

constexpr float kEpsilon = 1e-3F;

class RayCast3DStepTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    RaycastSystem raycastSystem;

    [[nodiscard]] Entity MakeRay(const Math::Vector3& position, const RayCast3DComponent& component) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<RayCast3DComponent>(entity, component);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeCollider(const Math::Vector3& position,
                                            const Math::Vector3& halfExtents = {0.5F, 0.5F, 0.5F},
                                            const std::uint32_t layer = 1U) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = layer;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] RayCast3DStepResult Step(const Entity entity) {
        return StepRayCast3D(entityManager, raycastSystem, entity);
    }

    [[nodiscard]] RayCast3DComponent Component(const Entity entity) {
        return entityManager.GetComponent<RayCast3DComponent>(entity);
    }
};

TEST_F(RayCast3DStepTest, HitsTheClosestColliderAlongTheLocalAxis) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity caster = MakeRay({0.0F, 5.0F, 0.0F}, rayCast);
    const Entity ground = MakeCollider({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F});

    const RayCast3DStepResult result = Step(caster);

    EXPECT_TRUE(result.Hit());
    EXPECT_TRUE(result.hasHit);
    EXPECT_EQ(result.hitEntity, ground);
    EXPECT_NEAR(result.hitPosition.y, 0.5F, kEpsilon);
    EXPECT_NEAR(result.hitNormal.y, 1.0F, kEpsilon);
    EXPECT_TRUE(Component(caster).hit);
    EXPECT_EQ(Component(caster).hitEntity, ground);
}

TEST_F(RayCast3DStepTest, MissesWhenTheLengthFallsShortAndClearsAStaleHit) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity caster = MakeRay({0.0F, 5.0F, 0.0F}, rayCast);
    static_cast<void>(MakeCollider({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F}));
    EXPECT_TRUE(Step(caster).Hit());

    entityManager.GetComponent<RayCast3DComponent>(caster).length = 1.0F;
    const RayCast3DStepResult miss = Step(caster);
    EXPECT_TRUE(miss.Missed());
    EXPECT_FALSE(miss.hasHit);
    EXPECT_FALSE(Component(caster).hit);
    EXPECT_EQ(Component(caster).hitEntity, Scene::kInvalidEntity);
}

TEST_F(RayCast3DStepTest, NeverHitsTheEntityItIsAttachedTo) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity caster = MakeRay({0.0F, 5.0F, 0.0F}, rayCast);
    entityManager.AddComponent<Scene::ColliderComponent>(caster, Scene::ColliderComponent{});
    sceneGraph.Update(entityManager);
    const Entity ground = MakeCollider({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F});

    const RayCast3DStepResult result = Step(caster);
    EXPECT_TRUE(result.Hit());
    EXPECT_EQ(result.hitEntity, ground);
    EXPECT_NE(result.hitEntity, caster);
}

TEST_F(RayCast3DStepTest, ExclusionsSkipNamedColliders) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity nearer = MakeCollider({0.0F, 4.0F, 0.0F}, {10.0F, 0.5F, 10.0F});
    const Entity farther = MakeCollider({0.0F, 3.0F, 0.0F}, {10.0F, 0.5F, 10.0F});
    const Entity caster = MakeRay({0.0F, 5.0F, 0.0F}, rayCast);

    EXPECT_EQ(Step(caster).hitEntity, nearer);

    entityManager.GetComponent<RayCast3DComponent>(caster).exclusions[0] = nearer;
    const RayCast3DStepResult skipped = Step(caster);
    EXPECT_TRUE(skipped.Hit());
    EXPECT_EQ(skipped.hitEntity, farther);
    EXPECT_NEAR(skipped.hitPosition.y, 3.5F, kEpsilon);
}

TEST_F(RayCast3DStepTest, DisabledAndInvalidListsClearTheWholeResult) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity caster = MakeRay({0.0F, 5.0F, 0.0F}, rayCast);
    static_cast<void>(MakeCollider({0.0F, 0.0F, 0.0F}, {10.0F, 0.5F, 10.0F}));
    EXPECT_TRUE(Step(caster).Hit());

    entityManager.GetComponent<RayCast3DComponent>(caster).enabled = false;
    const RayCast3DStepResult disabled = Step(caster);
    EXPECT_TRUE(disabled.IsDisabled());
    EXPECT_FALSE(Component(caster).hit);
    EXPECT_EQ(Component(caster).hitEntity, Scene::kInvalidEntity);

    RayCast3DComponent& live = entityManager.GetComponent<RayCast3DComponent>(caster);
    live.enabled = true;
    live.exclusions[1] = Entity{9U, 1U};
    const RayCast3DStepResult invalid = Step(caster);
    EXPECT_EQ(invalid.code, RayCast3DStepCode::InvalidComponent);
    EXPECT_FALSE(Component(caster).hit);
    EXPECT_EQ(Component(caster).hitPosition, Math::Vector3{});
}

TEST_F(RayCast3DStepTest, TheCastRunsAlongTheObjectsOwnRotation) {
    RayCast3DComponent rayCast;
    rayCast.direction = Math::Vector3{1.0F, 0.0F, 0.0F};
    rayCast.length = 10.0F;
    const Entity caster = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    transform.localRotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    sceneGraph.AttachTransform(entityManager, caster, transform);
    entityManager.AddComponent<RayCast3DComponent>(caster, rayCast);
    sceneGraph.Update(entityManager);
    const Entity wall = MakeCollider({0.0F, 0.0F, -1.0F});

    const RayCast3DStepResult result = Step(caster);
    EXPECT_TRUE(result.Hit());
    EXPECT_EQ(result.hitEntity, wall);
}

TEST_F(RayCast3DStepTest, RefusesUnknownAndNonRayEntitiesWithoutWriting) {
    const RayCast3DStepResult unknown = Step(Entity{9999U, 0U});
    EXPECT_EQ(unknown.code, RayCast3DStepCode::UnknownEntity);
    EXPECT_TRUE(unknown.IsRefused());

    const Entity plain = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, plain, Scene::TransformComponent{});
    sceneGraph.Update(entityManager);
    EXPECT_EQ(Step(plain).code, RayCast3DStepCode::NotARayCast);

    const Entity untransformed = entityManager.CreateEntity();
    entityManager.AddComponent<RayCast3DComponent>(untransformed, RayCast3DComponent{});
    EXPECT_EQ(Step(untransformed).code, RayCast3DStepCode::MissingTransform);
    EXPECT_FALSE(Component(untransformed).hit);
}

} // namespace
} // namespace UVE::Physics::Tests
