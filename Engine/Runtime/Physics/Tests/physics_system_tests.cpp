// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_system.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "platform.h"
#include "collision_system.h"
#include "collider_component.h"
#include "physics_object_component.h"
#include "process_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

class PhysicsSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeBodyEntity(Math::Vector3 position, Scene::Rigid3DComponent rigidBody,
                                        std::optional<Math::Vector3> colliderHalfExtents = std::nullopt) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::Rigid3DComponent>(entity, rigidBody);
        if (colliderHalfExtents.has_value()) {
            entityManager.AddComponent<Scene::ColliderComponent>(entity,
                                                                        Scene::ColliderComponent{*colliderHalfExtents});
        }
        return entity;
    }

    Scene::Entity MakeStaticColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(entity, Scene::ColliderComponent{halfExtents});
        return entity;
    }

    [[nodiscard]] Math::Vector3 GetWorldPosition(Scene::Entity entity) {
        return entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    }

    static void RunSteps(IPhysicsSystem& physicsSystem, Scene::IEntityManager& entityManager,
                            Scene::ISceneGraph& sceneGraph, int steps, float fixedDeltaTimeSeconds) {
        for (int i = 0; i < steps; ++i) {
            physicsSystem.Step(entityManager, sceneGraph, fixedDeltaTimeSeconds);
        }
    }
};

TEST(Rigid3DComponentTest, IsRigid3DComponentValid_RejectsUnsafeValuesAndPreservesZeroMass) {
    EXPECT_TRUE(Scene::IsRigid3DComponentValid(Scene::Rigid3DComponent{}));

    Scene::Rigid3DComponent zeroMass = {};
    zeroMass.mass = 0.0F;
    EXPECT_TRUE(Scene::IsRigid3DComponentValid(zeroMass));

    Scene::Rigid3DComponent invalid = {};
    invalid.mass = -1.0F;
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.velocity.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.drag = -0.1F;
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.gravityScale = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.gravityScale = -1.0F;
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.angularVelocity.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.torque.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
    invalid = {};
    invalid.inverseInertia.z = -0.1F;
    EXPECT_FALSE(Scene::IsRigid3DComponentValid(invalid));
}

TEST_F(PhysicsSystemTest, Step_AngularStateIntegratesTorqueAndAdvancesLocalRotation) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    Scene::Rigid3DComponent rigidBody;
    rigidBody.angularVelocity = Math::Vector3{0.0F, 1.0F, 0.0F};
    rigidBody.torque = Math::Vector3{0.0F, 1.0F, 0.0F};
    rigidBody.inverseInertia = Math::Vector3{0.0F, 0.5F, 0.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{}, rigidBody);

    physicsSystem.Step(entityManager, sceneGraph, 0.5F);

    const Scene::Rigid3DComponent& updated =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_NEAR(updated.angularVelocity.y, 1.25F, kEpsilon);
    const float angle = 0.625F;
    const Math::Quaternion rotation =
        entityManager.GetComponent<Scene::TransformComponent>(body).localRotation;
    EXPECT_NEAR(rotation.x, 0.0F, kEpsilon);
    EXPECT_NEAR(rotation.y, std::sin(angle * 0.5F), kEpsilon);
    EXPECT_NEAR(rotation.z, 0.0F, kEpsilon);
    EXPECT_NEAR(rotation.w, std::cos(angle * 0.5F), kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_AppliesGyroscopicTorqueToAngularVelocity) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    Scene::Rigid3DComponent rigidBody;
    rigidBody.angularVelocity = Math::Vector3{1.0F, 2.0F, 0.0F};
    rigidBody.inverseInertia = Math::Vector3{2.0F, 3.0F, 4.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{}, rigidBody);

    physicsSystem.Step(entityManager, sceneGraph, 0.1F);

    const Scene::Rigid3DComponent& updated =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_NEAR(updated.angularVelocity.x, 1.0F, kEpsilon);
    EXPECT_NEAR(updated.angularVelocity.y, 2.0F, kEpsilon);
    EXPECT_NEAR(updated.angularVelocity.z, 0.13333333F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_ZeroAngularDefaultsPreserveIdentityRotation) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    const Scene::Entity body = MakeBodyEntity(
        Math::Vector3{}, Scene::Rigid3DComponent{});

    RunSteps(physicsSystem, entityManager, sceneGraph, 3, 0.1F);

    EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(body).localRotation,
              Math::Quaternion{});
}

TEST_F(PhysicsSystemTest, Step_DynamicBodyUnderGravity_MatchesHandComputedSemiImplicitEuler) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Scene::Rigid3DComponent{});

    RunSteps(physicsSystem, entityManager, sceneGraph, 3, 0.1F);

    // Semi-implicit Euler, v0=0, g=-10, dt=0.1: v_n = g*dt*n; pos_n = g*dt^2*n(n+1)/2.
    // n=3: pos = -10 * 0.01 * 6 = -0.6.
    EXPECT_NEAR(GetWorldPosition(body).y, -0.6F, kEpsilon);
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.y, -3.0F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_InvalidGravityOrDeltaPreservesBodyState) {
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{1.0F, 2.0F, 3.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{4.0F, 5.0F, 6.0F}, rigidBody);
    const Scene::TransformComponent originalTransform =
        entityManager.GetComponent<Scene::TransformComponent>(body);
    const Math::Vector3 originalVelocity =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity;

    PhysicsSystem nonFiniteGravitySystem(
        collisionSystem, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    nonFiniteGravitySystem.Step(entityManager, sceneGraph, 0.1F);
    const Scene::TransformComponent& afterNonFiniteGravity =
        entityManager.GetComponent<Scene::TransformComponent>(body);
    const Scene::Rigid3DComponent& bodyAfterNonFiniteGravity =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_FLOAT_EQ(afterNonFiniteGravity.localPosition.x, originalTransform.localPosition.x);
    EXPECT_FLOAT_EQ(afterNonFiniteGravity.localPosition.y, originalTransform.localPosition.y);
    EXPECT_FLOAT_EQ(afterNonFiniteGravity.localPosition.z, originalTransform.localPosition.z);
    EXPECT_FLOAT_EQ(bodyAfterNonFiniteGravity.velocity.x, originalVelocity.x);
    EXPECT_FLOAT_EQ(bodyAfterNonFiniteGravity.velocity.y, originalVelocity.y);
    EXPECT_FLOAT_EQ(bodyAfterNonFiniteGravity.velocity.z, originalVelocity.z);

    PhysicsSystem validGravitySystem(collisionSystem, Math::Vector3{0.0F, -9.81F, 0.0F});
    validGravitySystem.Step(entityManager, sceneGraph, -0.1F);
    const Scene::TransformComponent& afterInvalidDelta =
        entityManager.GetComponent<Scene::TransformComponent>(body);
    const Scene::Rigid3DComponent& bodyAfterInvalidDelta =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_FLOAT_EQ(afterInvalidDelta.localPosition.x, originalTransform.localPosition.x);
    EXPECT_FLOAT_EQ(afterInvalidDelta.localPosition.y, originalTransform.localPosition.y);
    EXPECT_FLOAT_EQ(afterInvalidDelta.localPosition.z, originalTransform.localPosition.z);
    EXPECT_FLOAT_EQ(bodyAfterInvalidDelta.velocity.x, originalVelocity.x);
    EXPECT_FLOAT_EQ(bodyAfterInvalidDelta.velocity.y, originalVelocity.y);
    EXPECT_FLOAT_EQ(bodyAfterInvalidDelta.velocity.z, originalVelocity.z);
}

TEST_F(PhysicsSystemTest, Step_DerivedOverflowFailsClosedWithoutPublishingNonFiniteState) {
    Scene::Rigid3DComponent velocityOverflowBody;
    velocityOverflowBody.velocity = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    const Scene::Entity velocityBody = MakeBodyEntity(Math::Vector3{4.0F, 5.0F, 6.0F}, velocityOverflowBody);
    PhysicsSystem velocityOverflowSystem(collisionSystem, Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F});

    velocityOverflowSystem.Step(entityManager, sceneGraph, 1.0F);

    const Scene::Rigid3DComponent& velocityAfter =
        entityManager.GetComponent<Scene::Rigid3DComponent>(velocityBody);
    EXPECT_FLOAT_EQ(velocityAfter.velocity.x, std::numeric_limits<float>::max());
    EXPECT_EQ(GetWorldPosition(velocityBody), (Math::Vector3{4.0F, 5.0F, 6.0F}));

    Scene::Rigid3DComponent positionOverflowBody;
    positionOverflowBody.velocity = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    const Scene::Entity positionBody = MakeBodyEntity(Math::Vector3{0.0F, 1.0F, 2.0F}, positionOverflowBody);
    PhysicsSystem positionOverflowSystem(collisionSystem, Math::Vector3{});

    positionOverflowSystem.Step(entityManager, sceneGraph, 2.0F);

    const Scene::Rigid3DComponent& positionAfter =
        entityManager.GetComponent<Scene::Rigid3DComponent>(positionBody);
    EXPECT_FLOAT_EQ(positionAfter.velocity.x, std::numeric_limits<float>::max());
    EXPECT_EQ(GetWorldPosition(positionBody), (Math::Vector3{0.0F, 1.0F, 2.0F}));

    Scene::Rigid3DComponent angularOverflowBody;
    angularOverflowBody.angularVelocity = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    angularOverflowBody.torque = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    angularOverflowBody.inverseInertia = Math::Vector3{1.0F, 0.0F, 0.0F};
    const Scene::Entity angularBody = MakeBodyEntity(Math::Vector3{2.0F, 3.0F, 4.0F}, angularOverflowBody);
    PhysicsSystem angularOverflowSystem(collisionSystem, Math::Vector3{});

    angularOverflowSystem.Step(entityManager, sceneGraph, 1.0F);

    const Scene::Rigid3DComponent& angularAfter =
        entityManager.GetComponent<Scene::Rigid3DComponent>(angularBody);
    EXPECT_FLOAT_EQ(angularAfter.angularVelocity.x, std::numeric_limits<float>::max());
    EXPECT_EQ(GetWorldPosition(angularBody), (Math::Vector3{2.0F, 3.0F, 4.0F}));
}

TEST_F(PhysicsSystemTest, Step_FinitePositionCancellation_PreservesRepresentableResult) {
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, 0.0F};
    const Scene::Entity body = MakeBodyEntity(
        Math::Vector3{-std::numeric_limits<float>::max(), 2.0F, 3.0F}, rigidBody);
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});

    physicsSystem.Step(entityManager, sceneGraph, 2.0F);

    const Scene::TransformComponent& transform =
        entityManager.GetComponent<Scene::TransformComponent>(body);
    const Scene::Rigid3DComponent& bodyAfter =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_FLOAT_EQ(transform.localPosition.x, std::numeric_limits<float>::max());
    EXPECT_FLOAT_EQ(transform.localPosition.y, 2.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.z, 3.0F);
    EXPECT_FLOAT_EQ(bodyAfter.velocity.x, std::numeric_limits<float>::max());
}

TEST_F(PhysicsSystemTest, Step_GravityScaleDouble_FallsExactlyTwiceAsFar) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    Scene::Rigid3DComponent normalScale;
    normalScale.gravityScale = 1.0F;
    Scene::Rigid3DComponent doubleScale;
    doubleScale.gravityScale = 2.0F;
    const Scene::Entity normalBody = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, normalScale);
    const Scene::Entity fastBody = MakeBodyEntity(Math::Vector3{100.0F, 0.0F, 0.0F}, doubleScale);

    RunSteps(physicsSystem, entityManager, sceneGraph, 5, 0.05F);

    const float normalDrop = -GetWorldPosition(normalBody).y;
    const float fastDrop = -GetWorldPosition(fastBody).y;
    ASSERT_GT(normalDrop, 0.0F);
    EXPECT_NEAR(fastDrop, normalDrop * 2.0F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_KinematicBody_NeverMoves) {
    PhysicsSystem physicsSystem(collisionSystem);
    Scene::Rigid3DComponent rigidBody;
    rigidBody.isKinematic = true;
    rigidBody.velocity = Math::Vector3{5.0F, 5.0F, 5.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{1.0F, 2.0F, 3.0F}, rigidBody);

    RunSteps(physicsSystem, entityManager, sceneGraph, 10, 0.1F);

    EXPECT_EQ(GetWorldPosition(body), (Math::Vector3{1.0F, 2.0F, 3.0F}));
}

TEST_F(PhysicsSystemTest, Step_Drag_DampsVelocityEachStep) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, 0.0F, 0.0F}); // isolate drag from gravity
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{10.0F, 0.0F, 0.0F};
    rigidBody.drag = 0.5F;
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, rigidBody);

    physicsSystem.Step(entityManager, sceneGraph, 0.1F);

    // velocity *= max(0, 1 - drag*dt) = 1 - 0.05 = 0.95.
    EXPECT_NEAR(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.x, 9.5F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_DynamicBodyFallingOntoStaticGround_StopsAtRestingHeight) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -9.81F, 0.0F});
    MakeStaticColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{5.0F, 0.5F, 5.0F}); // top at y=0.5
    const Scene::Entity body =
        MakeBodyEntity(Math::Vector3{0.0F, 3.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});

    RunSteps(physicsSystem, entityManager, sceneGraph, 300, 1.0F / 60.0F);

    // Resting position: ground top (0.5) + body half-height (0.5) = 1.0.
    EXPECT_NEAR(GetWorldPosition(body).y, 1.0F, 0.05F);
    EXPECT_GE(GetWorldPosition(body).y, 1.0F - kEpsilon); // never sank below resting height
}

TEST_F(PhysicsSystemTest, Step_SlidingIntoWall_PreservesTangentialVelocity) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, 0.0F, 0.0F}); // isolate collision response
    MakeStaticColliderEntity(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 10.0F, 10.0F}); // wall
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{5.0F, 3.0F, 0.0F}; // moving into the wall (+x) and sliding (+y)
    const Scene::Entity body =
        MakeBodyEntity(Math::Vector3{1.6F, 0.0F, 0.0F}, rigidBody, Math::Vector3{0.5F, 0.5F, 0.5F});

    physicsSystem.Step(entityManager, sceneGraph, 0.01F);

    const Math::Vector3 finalVelocity = entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity;
    EXPECT_NEAR(finalVelocity.x, 0.0F, kEpsilon);  // into-wall component removed
    EXPECT_NEAR(finalVelocity.y, 3.0F, kEpsilon);  // tangential (sliding) component untouched
}

TEST_F(PhysicsSystemTest, Step_SameScenarioTwice_ProducesDeterministicResults) {
    const auto RunScenario = []() {
        Memory::MemoryManager localMemoryManager;
        Events::EventSystem localEventSystem;
        Scene::EntityManager localEntityManager(localMemoryManager.GetDefaultAllocator(), localEventSystem);
        Scene::SceneGraph localSceneGraph;
        CollisionSystem localCollisionSystem;
        PhysicsSystem localPhysicsSystem(localCollisionSystem, Math::Vector3{0.0F, -9.81F, 0.0F});

        const Scene::Entity groundEntity = localEntityManager.CreateEntity();
        Scene::TransformComponent groundTransform;
        groundTransform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
        localSceneGraph.AttachTransform(localEntityManager, groundEntity, groundTransform);
        localSceneGraph.Update(localEntityManager);
        localEntityManager.AddComponent<Scene::ColliderComponent>(
            groundEntity, Scene::ColliderComponent{Math::Vector3{5.0F, 0.5F, 5.0F}});

        const Scene::Entity bodyEntity = localEntityManager.CreateEntity();
        Scene::TransformComponent bodyTransform;
        bodyTransform.localPosition = Math::Vector3{0.0F, 3.0F, 0.0F};
        localSceneGraph.AttachTransform(localEntityManager, bodyEntity, bodyTransform);
        localSceneGraph.Update(localEntityManager);
        localEntityManager.AddComponent<Scene::Rigid3DComponent>(bodyEntity, Scene::Rigid3DComponent{});
        localEntityManager.AddComponent<Scene::ColliderComponent>(
            bodyEntity, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});

        for (int i = 0; i < 120; ++i) {
            localPhysicsSystem.Step(localEntityManager, localSceneGraph, 1.0F / 60.0F);
        }
        return localEntityManager.GetComponent<Scene::WorldTransformComponent>(bodyEntity).worldPosition;
    };

    const Math::Vector3 firstRun = RunScenario();
    const Math::Vector3 secondRun = RunScenario();

    EXPECT_EQ(firstRun, secondRun);
}

TEST_F(PhysicsSystemTest, Step_StackedDynamicBoxesOnStaticGround_SettleToFiniteNonOverlappingState) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -9.81F, 0.0F});
    MakeStaticColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{5.0F, 0.5F, 5.0F}); // top at y=0.5
    const Scene::Entity lower =
        MakeBodyEntity(Math::Vector3{0.0F, 2.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Scene::Entity upper =
        MakeBodyEntity(Math::Vector3{0.05F, 4.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});

    RunSteps(physicsSystem, entityManager, sceneGraph, 400, 1.0F / 60.0F);

    const Math::Vector3 lowerPosition = GetWorldPosition(lower);
    const Math::Vector3 upperPosition = GetWorldPosition(upper);

    EXPECT_TRUE(std::isfinite(lowerPosition.y));
    EXPECT_TRUE(std::isfinite(upperPosition.y));
    EXPECT_GE(lowerPosition.y, 1.0F - 0.05F);           // resting on ground (top 0.5 + half-height 0.5), not sunk in
    EXPECT_GE(upperPosition.y, lowerPosition.y + 1.0F - 0.05F); // resting on lower box, not sunk into it
}

TEST_F(PhysicsSystemTest, Step_RestitutionHalf_ReflectsAndScalesNormalVelocity) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, 0.0F, 0.0F}); // isolate collision response
    const Scene::Entity ground = MakeStaticColliderEntity(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 10.0F, 10.0F});
    entityManager.GetComponent<Scene::ColliderComponent>(ground).restitution = 0.5F;
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{5.0F, 0.0F, 0.0F}; // moving straight into the wall (+x)
    const Scene::Entity body =
        MakeBodyEntity(Math::Vector3{1.6F, 0.0F, 0.0F}, rigidBody, Math::Vector3{0.5F, 0.5F, 0.5F});
    entityManager.GetComponent<Scene::ColliderComponent>(body).restitution = 0.5F;

    physicsSystem.Step(entityManager, sceneGraph, 0.01F);

    // Combined restitution = average(0.5, 0.5) = 0.5. New normal velocity = -restitution *
    // intoSurface = -0.5 * 5.0 = -2.5 (reflected, scaled — not simply zeroed).
    const float finalVelocityX = entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity.x;
    EXPECT_NEAR(finalVelocityX, -2.5F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_FrictionOne_FullyDampsTangentialVelocity) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, 0.0F, 0.0F}); // isolate collision response
    const Scene::Entity ground = MakeStaticColliderEntity(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 10.0F, 10.0F});
    entityManager.GetComponent<Scene::ColliderComponent>(ground).friction = 1.0F;
    Scene::Rigid3DComponent rigidBody;
    rigidBody.velocity = Math::Vector3{5.0F, 3.0F, 0.0F}; // moving into the wall (+x) and sliding (+y)
    const Scene::Entity body =
        MakeBodyEntity(Math::Vector3{1.6F, 0.0F, 0.0F}, rigidBody, Math::Vector3{0.5F, 0.5F, 0.5F});
    entityManager.GetComponent<Scene::ColliderComponent>(body).friction = 1.0F;

    physicsSystem.Step(entityManager, sceneGraph, 0.01F);

    // Combined friction = average(1.0, 1.0) = 1.0 -> frictionFactor = 0 -> tangential fully damped.
    const Math::Vector3 finalVelocity = entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity;
    EXPECT_NEAR(finalVelocity.x, 0.0F, kEpsilon);
    EXPECT_NEAR(finalVelocity.y, 0.0F, kEpsilon);
}

TEST_F(PhysicsSystemTest, Step_StaticVsStaticOverlap_NeitherMovesAndNoDivideByZero) {
    PhysicsSystem physicsSystem(collisionSystem);
    const Scene::Entity a = MakeStaticColliderEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Scene::Entity b = MakeStaticColliderEntity(Math::Vector3{0.5F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_NO_FATAL_FAILURE(RunSteps(physicsSystem, entityManager, sceneGraph, 5, 1.0F / 60.0F));

    EXPECT_EQ(GetWorldPosition(a), (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(GetWorldPosition(b), (Math::Vector3{0.5F, 0.0F, 0.0F}));
}

#if UVE_DEBUG
TEST_F(PhysicsSystemTest, Step_InvalidRigid3DParameters_Asserts) {
    Scene::Rigid3DComponent invalid = {};
    invalid.drag = -1.0F;
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{}, invalid);
    PhysicsSystem physicsSystem(collisionSystem);

    ASSERT_NE(body, Scene::kInvalidEntity);
    EXPECT_DEATH({ physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F); }, "");
}
#else
TEST_F(PhysicsSystemTest, Step_InvalidRigid3DParameters_FailsClosedWithoutIntegration) {
    Scene::Rigid3DComponent invalid = {};
    invalid.drag = -1.0F;
    invalid.velocity = Math::Vector3{3.0F, 4.0F, 5.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{}, invalid);
    PhysicsSystem physicsSystem(collisionSystem);

    ASSERT_NE(body, Scene::kInvalidEntity);
    const Scene::TransformComponent beforeTransform =
        entityManager.GetComponent<Scene::TransformComponent>(body);
    const Scene::Rigid3DComponent beforeBody =
        entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F);

    EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(body).localPosition,
              beforeTransform.localPosition);
    EXPECT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(body).velocity,
              beforeBody.velocity);
}
#endif

// =================================================================================================
// What the abstract physics object state does to the simulation: a stopped body is taken out of
// the world, kept as an obstacle, or left simulating, and collision priority decides how much of
// an overlap each side of a contact takes.
// =================================================================================================

TEST_F(PhysicsSystemTest, Step_AStoppedBodyWithRemoveIsNotInTheWorldAtAll) {
    // Gravity, so "nothing moved it" means something.
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    // The floor's top is at y = 0 and the body starts a quarter of a metre *inside* it. A live
    // body in that position would be pushed out on the first step; this one is taken out of the
    // world entirely, so nothing collides with it and the simulation does not move it either.
    const Scene::Entity ground = MakeStaticColliderEntity({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    Scene::Rigid3DComponent rigid;
    const Scene::Entity body =
        MakeBodyEntity({0.0F, 0.25F, 0.0F}, rigid, Math::Vector3{0.5F, 0.5F, 0.5F});

    Scene::ProcessComponent stopped{};
    stopped.mode = Scene::TickMode::Never;
    entityManager.AddComponent<Scene::ProcessComponent>(body, stopped);
    Scene::PhysicsObjectComponent object{};
    object.disableMode = Scene::PhysicsObjectDisableMode::Remove;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(body, object);

    // The same body, the same overlap, still running: this is the control that says the test is
    // about the state and not about bodies in general.
    const Scene::Entity live =
        MakeBodyEntity({0.0F, 0.25F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    sceneGraph.Update(entityManager);

    const float removedStartY = GetWorldPosition(body).y;
    const float liveStartY = GetWorldPosition(live).y;
    RunSteps(physicsSystem, entityManager, sceneGraph, 60, 1.0F / 60.0F);

    // The live one was pushed out of the floor and left standing on it...
    EXPECT_GT(GetWorldPosition(live).y, liveStartY + 0.2F);
    // ...and the removed one is exactly where it was authored, still inside the floor.
    EXPECT_FLOAT_EQ(GetWorldPosition(body).y, removedStartY);
    for (const CollisionPair& pair : collisionSystem.DetectCollisions(entityManager)) {
        EXPECT_NE(pair.first, body);
        EXPECT_NE(pair.second, body);
    }
    static_cast<void>(ground);
}

TEST_F(PhysicsSystemTest, Step_AStoppedBodyWithMakeStaticBecomesAnObstacleNothingMoves) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{0.0F, -10.0F, 0.0F});
    // Stopped mid-air with MakeStatic: it hangs exactly where it is, and a falling body lands on
    // it. This is the middle answer - out of the simulation, still in the world.
    const Scene::Entity shelf =
        MakeBodyEntity({0.0F, 2.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{1.0F, 0.25F, 1.0F});
    Scene::ProcessComponent stopped{};
    stopped.mode = Scene::TickMode::Never;
    entityManager.AddComponent<Scene::ProcessComponent>(shelf, stopped);
    Scene::PhysicsObjectComponent object{};
    object.disableMode = Scene::PhysicsObjectDisableMode::MakeStatic;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(shelf, object);

    const Scene::Entity body =
        MakeBodyEntity({0.0F, 4.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    sceneGraph.Update(entityManager);

    RunSteps(physicsSystem, entityManager, sceneGraph, 240, 1.0F / 60.0F);

    // Not a millimetre of gravity or of contact correction reached the shelf.
    EXPECT_NEAR(GetWorldPosition(shelf).y, 2.0F, 1.0e-4F);
    // The shelf's top is at 2.25 and the body's own half height is 0.5: it rests at 2.75.
    EXPECT_NEAR(GetWorldPosition(body).y, 2.75F, 2.0e-2F);
}

TEST_F(PhysicsSystemTest, Step_CollisionPriorityDecidesHowMuchOfAnOverlapEachSideTakes) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    // Two equal bodies overlapping by 0.2 along x. The masses are equal, so with the default
    // priorities the correction would be half each; giving the left one a priority of 3 makes it
    // yield a quarter of it instead. The total correction is unchanged - only the shares move.
    const Scene::Entity lighter =
        MakeBodyEntity({-0.4F, 10.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Scene::Entity heavier =
        MakeBodyEntity({0.4F, 10.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    Scene::PhysicsObjectComponent lighterObject{};
    lighterObject.collisionPriority = 3.0F;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(lighter, lighterObject);
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(heavier, Scene::PhysicsObjectComponent{});
    sceneGraph.Update(entityManager);

    const float lighterBefore = GetWorldPosition(lighter).x;
    const float heavierBefore = GetWorldPosition(heavier).x;
    physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F);
    const float lighterMoved = std::abs(GetWorldPosition(lighter).x - lighterBefore);
    const float heavierMoved = std::abs(GetWorldPosition(heavier).x - heavierBefore);

    ASSERT_GT(lighterMoved + heavierMoved, 0.15F);
    EXPECT_LT(lighterMoved, heavierMoved);
    EXPECT_NEAR(lighterMoved / (lighterMoved + heavierMoved), 0.25F, 5.0e-2F);
    // A priority is a share, not a multiplier: the two of them together still separate by the
    // overlap they started with.
    EXPECT_NEAR(lighterMoved + heavierMoved, 0.2F, 1.0e-2F);
}

TEST_F(PhysicsSystemTest, Step_APriorityOfZeroIsPinnedInPlaceAndTheOtherSideTakesItAll) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    const Scene::Entity pinned =
        MakeBodyEntity({-0.4F, 20.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Scene::Entity walker =
        MakeBodyEntity({0.4F, 20.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    Scene::PhysicsObjectComponent pinnedObject{};
    pinnedObject.collisionPriority = 0.0F;
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(pinned, pinnedObject);
    entityManager.AddComponent<Scene::PhysicsObjectComponent>(walker, Scene::PhysicsObjectComponent{});
    sceneGraph.Update(entityManager);

    const float pinnedBefore = GetWorldPosition(pinned).x;
    const float walkerBefore = GetWorldPosition(walker).x;
    physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F);

    EXPECT_NEAR(GetWorldPosition(pinned).x, pinnedBefore, 1.0e-4F);
    // The whole 0.2 of overlap is resolved on the body that is willing to move.
    EXPECT_NEAR(std::abs(GetWorldPosition(walker).x - walkerBefore), 0.2F, 1.0e-2F);
}

TEST_F(PhysicsSystemTest, Step_AnObjectThatIsNotAPhysicsObjectStillSplitsByMass) {
    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    // The priority path must not have changed the plain case: two hand-made bodies with no
    // PhysicsObject component at all still separate half each, exactly as before.
    const Scene::Entity first =
        MakeBodyEntity({-0.4F, 30.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Scene::Entity second =
        MakeBodyEntity({0.4F, 30.0F, 0.0F}, Scene::Rigid3DComponent{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    sceneGraph.Update(entityManager);

    const float firstBefore = GetWorldPosition(first).x;
    const float secondBefore = GetWorldPosition(second).x;
    physicsSystem.Step(entityManager, sceneGraph, 1.0F / 60.0F);
    const float firstMoved = std::abs(GetWorldPosition(first).x - firstBefore);
    const float secondMoved = std::abs(GetWorldPosition(second).x - secondBefore);

    EXPECT_NEAR(firstMoved, 0.1F, 1.0e-2F);
    EXPECT_NEAR(secondMoved, 0.1F, 1.0e-2F);
}

// ---------------------------------------------------------------------------------------------
// Continuous collision detection
// ---------------------------------------------------------------------------------------------

namespace {

/// One step's travel at 100 m/s is 10 m, which is far more than the 0.1 m wall it has to cross -
/// the exact ratio at which a discrete integrator cannot see the obstacle at all.
constexpr float kTunnelingVelocity = 100.0F;
constexpr float kTunnelingStepSeconds = 0.1F;
constexpr float kWallCenterX = 2.0F;
constexpr float kWallHalfThickness = 0.05F;

} // namespace

TEST_F(PhysicsSystemTest, Step_WithoutContinuousCollisionAFastBodyPassesThroughAThinWall) {
    Scene::Rigid3DComponent fastBody;
    fastBody.velocity = Math::Vector3{kTunnelingVelocity, 0.0F, 0.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, fastBody,
                                                     Math::Vector3{0.25F, 0.25F, 0.25F});
    MakeStaticColliderEntity(Math::Vector3{kWallCenterX, 0.0F, 0.0F},
                                Math::Vector3{kWallHalfThickness, 2.0F, 2.0F});
    PhysicsSystem physicsSystem(collisionSystem);

    RunSteps(physicsSystem, entityManager, sceneGraph, 1, kTunnelingStepSeconds);

    // The documented failure this feature exists to remove: the body lands ten metres away with
    // the wall never having been in its way, because the wall was never between two samples.
    EXPECT_GT(GetWorldPosition(body).x, kWallCenterX);
}

TEST_F(PhysicsSystemTest, Step_ContinuousCollisionStopsAFastBodyBeforeTheWall) {
    Scene::Rigid3DComponent fastBody;
    fastBody.velocity = Math::Vector3{kTunnelingVelocity, 0.0F, 0.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, fastBody,
                                                     Math::Vector3{0.25F, 0.25F, 0.25F});
    MakeStaticColliderEntity(Math::Vector3{kWallCenterX, 0.0F, 0.0F},
                                Math::Vector3{kWallHalfThickness, 2.0F, 2.0F});
    PhysicsSystem physicsSystem(collisionSystem);
    PhysicsSimulationSettings settings;
    settings.continuousCollision.enabled = true;
    settings.continuousCollision.contactOffset = 0.001F;
    physicsSystem.SetSimulationSettings(settings);

    RunSteps(physicsSystem, entityManager, sceneGraph, 1, kTunnelingStepSeconds);

    const float stopX = GetWorldPosition(body).x;
    // Stopped short of the wall's near face (1.95) by the body's own bounding radius, and far from
    // the 10 m the discrete path would have travelled.
    EXPECT_LT(stopX, kWallCenterX - kWallHalfThickness);
    EXPECT_GT(stopX, 0.5F);
    // The velocity into the wall is gone; the body cannot re-tunnel on the next step.
    const Scene::Rigid3DComponent& after = entityManager.GetComponent<Scene::Rigid3DComponent>(body);
    EXPECT_LE(after.velocity.x, 1.0e-3F);
}

TEST_F(PhysicsSystemTest, Step_ContinuousCollisionLeavesATangentialSlideUntouched) {
    Scene::Rigid3DComponent slider;
    slider.velocity = Math::Vector3{kTunnelingVelocity, 0.0F, 0.0F};
    const Scene::Entity body = MakeBodyEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, slider,
                                                     Math::Vector3{0.25F, 0.25F, 0.25F});
    // A floor the body already rests on, so the very first sweep reports a zero-time contact.
    MakeStaticColliderEntity(Math::Vector3{0.0F, -0.5F, 0.0F},
                                Math::Vector3{50.0F, 0.25F, 50.0F});
    PhysicsSystem physicsSystem(collisionSystem);
    PhysicsSimulationSettings settings;
    settings.continuousCollision.enabled = true;
    physicsSystem.SetSimulationSettings(settings);

    RunSteps(physicsSystem, entityManager, sceneGraph, 1, kTunnelingStepSeconds);

    // A resting contact must not clamp the step to zero: that would freeze every grounded body.
    EXPECT_GT(GetWorldPosition(body).x, 1.0F);
}

TEST_F(PhysicsSystemTest, Step_ContinuousCollisionIsOptInByDefault) {
    PhysicsSimulationSettings settings;
    EXPECT_FALSE(settings.continuousCollision.enabled);
}

// ---------------------------------------------------------------------------------------------
// Deterministic stepping
// ---------------------------------------------------------------------------------------------

namespace {

/// One complete, independent physics world. Two of these stepped with identical inputs model the
/// two peers a rollback verifier compares.
struct DeterministicWorld final {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;
    PhysicsSystem physicsSystem{collisionSystem};

    DeterministicWorld() = default;
    DeterministicWorld(const DeterministicWorld&) = delete;
    DeterministicWorld& operator=(const DeterministicWorld&) = delete;

    /// `attachColliderFirst` changes which archetype the entity lands in, and therefore the order
    /// the collision system discovers it in. It changes nothing about the body itself.
    Scene::Entity AddBody(const Math::Vector3& position, const Math::Vector3& velocity,
                                 const Math::Vector3& halfExtents,
                                 const bool attachColliderFirst) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        Scene::Rigid3DComponent rigidBody;
        rigidBody.velocity = velocity;
        if (attachColliderFirst) {
            entityManager.AddComponent<Scene::ColliderComponent>(entity,
                                                                       Scene::ColliderComponent{halfExtents});
            entityManager.AddComponent<Scene::Rigid3DComponent>(entity, rigidBody);
        } else {
            entityManager.AddComponent<Scene::Rigid3DComponent>(entity, rigidBody);
            entityManager.AddComponent<Scene::ColliderComponent>(entity,
                                                                       Scene::ColliderComponent{halfExtents});
        }
        return entity;
    }

    void EnableDeterminism() {
        PhysicsSimulationSettings settings;
        settings.deterministic.enabled = true;
        physicsSystem.SetSimulationSettings(settings);
    }

    void Step(const int steps, const float deltaSeconds) {
        for (int index = 0; index < steps; ++index) {
            physicsSystem.Step(entityManager, sceneGraph, deltaSeconds);
        }
    }
};

/// The same four-body pile-up in every world: a heavy floor, a box dropped onto it, a box thrown
/// sideways into that one, and a second box resting on the floor.
struct PileBody final {
    Math::Vector3 position;
    Math::Vector3 velocity;
    Math::Vector3 halfExtents;
};

[[nodiscard]] std::array<PileBody, 4> MakePile() {
    return {{
        {Math::Vector3{0.0F, -0.25F, 0.0F}, Math::Vector3{0.0F, 0.0F, 0.0F},
         Math::Vector3{4.0F, 0.25F, 4.0F}},
        {Math::Vector3{0.0F, 3.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 0.0F},
         Math::Vector3{0.75F, 0.75F, 0.75F}},
        {Math::Vector3{-2.5F, 2.0F, 0.0F}, Math::Vector3{4.0F, 0.0F, 0.0F},
         Math::Vector3{0.5F, 0.5F, 0.5F}},
        {Math::Vector3{2.0F, 0.25F, 0.0F}, Math::Vector3{0.0F, 0.0F, 0.0F},
         Math::Vector3{0.5F, 0.5F, 0.5F}},
    }};
}

void BuildPile(DeterministicWorld& world, const bool attachColliderFirst) {
    for (const PileBody& body : MakePile()) {
        world.AddBody(body.position, body.velocity, body.halfExtents, attachColliderFirst);
    }
}

} // namespace

TEST(DeterministicPhysicsTest, ComponentAttachOrderDoesNotChangeTheSimulation) {
    DeterministicWorld first;
    DeterministicWorld second;
    // Same entity ids in the same creation order - which is what a deterministic spawn protocol
    // gives two peers - but the components are attached in the opposite order.
    //
    // Honest scope: this engine currently iterates its archetypes in signature-hash order, not
    // insertion order, so the two worlds are discovered in the same sequence TODAY and this test
    // would pass even without CanonicalizeCollisionPairs. It is here as an invariant lock on a
    // property the solver must not owe to a container's iteration order - the day that container
    // changes, the pair ORDER becomes observable (the solver is sequential), and this test is what
    // notices. The canonicalization's own teeth are in deterministic_physics_uve_tests.cpp, where
    // the sorting and the axis negation are asserted directly.
    BuildPile(first, false);
    BuildPile(second, true);
    first.EnableDeterminism();
    second.EnableDeterminism();

    // 240 steps is four seconds at 60 Hz: long enough for the thrown box to land, the pile to
    // overlap, resolve and come to rest, so the comparison covers contact resolution and not just
    // free fall.
    for (int step = 0; step < 240; ++step) {
        first.Step(1, 1.0F / 60.0F);
        second.Step(1, 1.0F / 60.0F);
        ASSERT_EQ(first.physicsSystem.GetLastStepDigest(),
                  second.physicsSystem.GetLastStepDigest())
            << "diverged at step " << step;
    }
}

TEST(DeterministicPhysicsTest, ReplayingTheSameWorldTwiceProducesTheSameDigests) {
    DeterministicWorld original;
    BuildPile(original, false);
    original.EnableDeterminism();

    std::vector<std::uint64_t> firstRun;
    for (int step = 0; step < 120; ++step) {
        original.Step(1, 1.0F / 60.0F);
        firstRun.push_back(original.physicsSystem.GetLastStepDigest());
    }

    DeterministicWorld replay;
    BuildPile(replay, false);
    replay.EnableDeterminism();
    for (int step = 0; step < 120; ++step) {
        replay.Step(1, 1.0F / 60.0F);
        ASSERT_EQ(firstRun[static_cast<std::size_t>(step)],
                  replay.physicsSystem.GetLastStepDigest())
            << "diverged at step " << step;
    }
}

TEST(DeterministicPhysicsTest, DigestIsNonZeroOnlyWhenDeterminismIsEnabled) {
    DeterministicWorld world;
    BuildPile(world, false);

    world.Step(1, 1.0F / 60.0F);
    EXPECT_EQ(world.physicsSystem.GetLastStepDigest(), 0U);

    world.EnableDeterminism();
    world.Step(1, 1.0F / 60.0F);
    EXPECT_NE(world.physicsSystem.GetLastStepDigest(), 0U);
}

TEST(DeterministicPhysicsTest, DigestTracksRealDivergence) {
    DeterministicWorld first;
    DeterministicWorld second;
    BuildPile(first, false);
    BuildPile(second, false);
    first.EnableDeterminism();
    second.EnableDeterminism();

    // One peer is nudged after the scene is built. A digest that kept matching would be reporting
    // agreement it never checked.
    second.entityManager.ForEach<Scene::Rigid3DComponent>(
        [](Scene::Entity, Scene::Rigid3DComponent& rigidBody) {
            rigidBody.velocity.x += 3.0F;
        });

    first.Step(20, 1.0F / 60.0F);
    second.Step(20, 1.0F / 60.0F);
    EXPECT_NE(first.physicsSystem.GetLastStepDigest(),
              second.physicsSystem.GetLastStepDigest());
}

} // namespace
namespace {

constexpr float kParallelFixedDelta = 1.0F / 60.0F;

/// One body's state as the equivalence tests compare it. Compared with exact equality, not a
/// tolerance: the entire promise of island solving is that the arithmetic - and therefore the
/// bits - are unchanged. A tolerance would hide exactly the drift these tests exist to catch.
struct BodySnapshot final {
    Math::Vector3 position{};
    Math::Vector3 velocity{};
};

/// Two columns of four overlapping boxes, 20 m apart: two islands (3 pairs each), which is the
/// smallest scene that can prove anything about parallelism. The columns are far enough apart that
/// no pair can ever connect them, so the two islands are genuinely independent.
void BuildTwoIslandScene(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                            std::vector<Scene::Entity>& bodies) {
    for (std::uint32_t column = 0U; column < 2U; ++column) {
        for (std::uint32_t level = 0U; level < 4U; ++level) {
            Scene::Rigid3DComponent rigid;
            rigid.velocity = Math::Vector3{0.02F * static_cast<float>(column), 0.0F, 0.0F};
            const Scene::Entity entity = entityManager.CreateEntity();
            Scene::TransformComponent local;
            local.localPosition = Math::Vector3{static_cast<float>(column) * 20.0F,
                                                   static_cast<float>(level) * 0.9F, 0.0F};
            sceneGraph.AttachTransform(entityManager, entity, local);
            entityManager.AddComponent<Scene::Rigid3DComponent>(entity, rigid);
            entityManager.AddComponent<Scene::ColliderComponent>(
                entity, Scene::ColliderComponent{Math::Vector3{0.5F, 0.5F, 0.5F}});
            bodies.push_back(entity);
        }
    }
    sceneGraph.Update(entityManager);
}

[[nodiscard]] std::vector<BodySnapshot> CaptureBodies(Scene::IEntityManager& entityManager,
                                                            const std::vector<Scene::Entity>& bodies) {
    std::vector<BodySnapshot> snapshots;
    snapshots.reserve(bodies.size());
    for (const Scene::Entity entity : bodies) {
        BodySnapshot snapshot;
        snapshot.position = entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition;
        snapshot.velocity = entityManager.GetComponent<Scene::Rigid3DComponent>(entity).velocity;
        snapshots.push_back(snapshot);
    }
    return snapshots;
}

void RestoreBodies(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                      const std::vector<Scene::Entity>& bodies,
                      const std::vector<BodySnapshot>& snapshots) {
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(bodies[index]);
        transform.localPosition = snapshots[index].position;
        sceneGraph.SetLocalTransform(entityManager, bodies[index], transform);
        entityManager.GetComponent<Scene::Rigid3DComponent>(bodies[index]).velocity =
            snapshots[index].velocity;
    }
    sceneGraph.Update(entityManager);
}

} // namespace

TEST_F(PhysicsSystemTest, Step_TwoSeparatedStacksReportTwoIslands) {
    std::vector<Scene::Entity> bodies;
    BuildTwoIslandScene(entityManager, sceneGraph, bodies);

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.Step(entityManager, sceneGraph, kParallelFixedDelta);

    EXPECT_EQ(physicsSystem.GetLastStepIslandCount(), 2U);
    // Nothing was threaded: no pool, even though the setting could have allowed it.
    EXPECT_EQ(physicsSystem.GetLastStepParallelIslandCount(), 0U);
}

TEST_F(PhysicsSystemTest, Step_IslandParallelResolutionMatchesSequentialExactly) {
    std::vector<Scene::Entity> bodies;
    BuildTwoIslandScene(entityManager, sceneGraph, bodies);
    const std::vector<BodySnapshot> initial = CaptureBodies(entityManager, bodies);

    PhysicsSimulationSettings settings;
    settings.deterministic.enabled = true;
    // The scene is tiny on purpose; lowering the break-even point is what lets six pairs exercise
    // the parallel path at all.
    settings.parallelStepping.minimumPairCount = 1U;

    PhysicsSystem sequential(collisionSystem, Math::Vector3{});
    sequential.SetSimulationSettings(settings);
    std::vector<std::uint64_t> sequentialDigests;
    sequentialDigests.reserve(30U);
    for (int step = 0; step < 30; ++step) {
        sequential.Step(entityManager, sceneGraph, kParallelFixedDelta);
        sequentialDigests.push_back(sequential.GetLastStepDigest());
    }
    ASSERT_GT(sequential.GetLastStepIslandCount(), 1U);
    EXPECT_EQ(sequential.GetLastStepParallelIslandCount(), 0U);
    const std::vector<BodySnapshot> sequentialState = CaptureBodies(entityManager, bodies);

    RestoreBodies(entityManager, sceneGraph, bodies, initial);

    Threading::ThreadPool pool(2U);
    PhysicsSystem parallel(collisionSystem, Math::Vector3{});
    parallel.SetThreadPool(&pool);
    settings.parallelStepping.enabled = true;
    parallel.SetSimulationSettings(settings);

    for (int step = 0; step < 30; ++step) {
        parallel.Step(entityManager, sceneGraph, kParallelFixedDelta);
        ASSERT_EQ(parallel.GetLastStepDigest(), sequentialDigests[static_cast<std::size_t>(step)])
            << "digest diverged at step " << step;
        ASSERT_EQ(parallel.GetLastStepParallelIslandCount(), parallel.GetLastStepIslandCount())
            << "every island should have been handed to the pool at step " << step;
    }

    const std::vector<BodySnapshot> parallelState = CaptureBodies(entityManager, bodies);
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        EXPECT_EQ(parallelState[index].position.x, sequentialState[index].position.x) << "body " << index;
        EXPECT_EQ(parallelState[index].position.y, sequentialState[index].position.y) << "body " << index;
        EXPECT_EQ(parallelState[index].position.z, sequentialState[index].position.z) << "body " << index;
        EXPECT_EQ(parallelState[index].velocity.x, sequentialState[index].velocity.x) << "body " << index;
        EXPECT_EQ(parallelState[index].velocity.y, sequentialState[index].velocity.y) << "body " << index;
        EXPECT_EQ(parallelState[index].velocity.z, sequentialState[index].velocity.z) << "body " << index;
    }
}

TEST_F(PhysicsSystemTest, Step_ParallelRequestedWithoutAPoolStillResolves) {
    std::vector<Scene::Entity> bodies;
    BuildTwoIslandScene(entityManager, sceneGraph, bodies);

    PhysicsSimulationSettings settings;
    settings.parallelStepping.enabled = true;
    settings.parallelStepping.minimumPairCount = 1U;

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.SetSimulationSettings(settings);
    physicsSystem.Step(entityManager, sceneGraph, kParallelFixedDelta);

    // A setting is a request, not a requirement: with no pool the step must still run, and report
    // that it did so on the calling thread.
    EXPECT_EQ(physicsSystem.GetLastStepParallelIslandCount(), 0U);
    EXPECT_EQ(physicsSystem.GetLastStepIslandCount(), 2U);
}

TEST_F(PhysicsSystemTest, Step_OneIslandIsNeverSplitAcrossWorkers) {
    // A single column is one island: splitting it would put two writers on the same bodies. The
    // guard has to hold even when the caller asks for parallelism and lowers the break-even point.
    std::vector<Scene::Entity> bodies;
    for (std::uint32_t level = 0U; level < 4U; ++level) {
        bodies.push_back(MakeBodyEntity(
            Math::Vector3{0.0F, static_cast<float>(level) * 0.9F, 0.0F}, Scene::Rigid3DComponent{},
            Math::Vector3{0.5F, 0.5F, 0.5F}));
    }
    sceneGraph.Update(entityManager);

    Threading::ThreadPool pool(2U);
    PhysicsSimulationSettings settings;
    settings.parallelStepping.enabled = true;
    settings.parallelStepping.minimumPairCount = 1U;

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.SetThreadPool(&pool);
    physicsSystem.SetSimulationSettings(settings);
    physicsSystem.Step(entityManager, sceneGraph, kParallelFixedDelta);

    EXPECT_EQ(physicsSystem.GetLastStepIslandCount(), 1U);
    EXPECT_EQ(physicsSystem.GetLastStepParallelIslandCount(), 0U);
}

TEST_F(PhysicsSystemTest, Step_AHandfulOfPairsStaysOnTheCallingThreadByDefault) {
    std::vector<Scene::Entity> bodies;
    BuildTwoIslandScene(entityManager, sceneGraph, bodies);

    Threading::ThreadPool pool(2U);
    PhysicsSimulationSettings settings;
    settings.parallelStepping.enabled = true; // break-even point left at its default of 64 pairs

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.SetThreadPool(&pool);
    physicsSystem.SetSimulationSettings(settings);
    physicsSystem.Step(entityManager, sceneGraph, kParallelFixedDelta);

    EXPECT_EQ(physicsSystem.GetLastStepParallelIslandCount(), 0U);
    EXPECT_EQ(physicsSystem.GetLastStepIslandCount(), 2U);
}

} // namespace UVE::Physics::Tests
