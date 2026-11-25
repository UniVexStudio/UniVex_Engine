// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_constraint_system.h"

#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "collision_system.h"
#include "physics_system.h"
#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kEpsilon = 1.0e-3F;

class PhysicsConstraintSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;
    PhysicsConstraintSystem constraintSystem;

    Scene::Entity MakeBody(Math::Vector3 position, float mass = 1.0F) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::Rigid3DComponent>(entity,
                                                                     Scene::Rigid3DComponent{mass, false});
        return entity;
    }

    Scene::Entity MakeStatic(Math::Vector3 position) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::ColliderComponent>(entity,
                                                                    Scene::ColliderComponent{{0.5F, 0.5F, 0.5F}});
        return entity;
    }

    [[nodiscard]] Math::Vector3 WorldPosition(Scene::Entity entity) const {
        return entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    }
};

TEST_F(PhysicsConstraintSystemTest, BuildConstraintIslandPlan_GroupsConnectedEdgesDeterministically) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});
    const Scene::Entity third = MakeBody({4.0F, 0.0F, 0.0F});
    const Scene::Entity fourth = MakeBody({8.0F, 0.0F, 0.0F});
    const Scene::Entity fifth = MakeBody({10.0F, 0.0F, 0.0F});
    const std::array<ConstraintIslandEdge, 3U> edges{
        ConstraintIslandEdge{first, second}, ConstraintIslandEdge{second, third},
        ConstraintIslandEdge{fourth, fifth}};

    ConstraintIslandPlan plan;
    ASSERT_TRUE(BuildConstraintIslandPlan(edges, plan));
    ASSERT_EQ(plan.entityCount, 5U);
    ASSERT_EQ(plan.islandCount, 2U);
    EXPECT_EQ(plan.entities[0U], first);
    EXPECT_EQ(plan.entities[1U], second);
    EXPECT_EQ(plan.entities[2U], third);
    EXPECT_EQ(plan.entities[3U], fourth);
    EXPECT_EQ(plan.entities[4U], fifth);
    EXPECT_EQ(plan.islandIndices[0U], 0U);
    EXPECT_EQ(plan.islandIndices[1U], 0U);
    EXPECT_EQ(plan.islandIndices[2U], 0U);
    EXPECT_EQ(plan.islandIndices[3U], 1U);
    EXPECT_EQ(plan.islandIndices[4U], 1U);
}

TEST_F(PhysicsConstraintSystemTest, BuildConstraintIslandPlan_RejectsInvalidEdgeAtomically) {
    const Scene::Entity first = MakeBody({});
    ConstraintIslandPlan plan;
    plan.entityCount = 7U;
    plan.islandCount = 3U;
    const std::array<ConstraintIslandEdge, 1U> invalidEdges{
        ConstraintIslandEdge{first, Scene::kInvalidEntity}};

    EXPECT_FALSE(BuildConstraintIslandPlan(invalidEdges, plan));
    EXPECT_EQ(plan.entityCount, 7U);
    EXPECT_EQ(plan.islandCount, 3U);
}

TEST_F(PhysicsConstraintSystemTest, AddDistanceAndSolve_MassWeightedCorrectionPreservesRestLength) {
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F}, 1.0F);
    const Scene::Entity second = MakeBody({10.0F, 0.0F, 0.0F}, 1.0F);

    const PhysicsConstraintMutationResult added = constraintSystem.AddDistanceConstraint(
        DistanceConstraint{first, second, {}, {}, 4.0F});
    ASSERT_TRUE(added.IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);

    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.islandCount, 1U);
    EXPECT_GT(solved.solvedConstraintCount, 0U);
    EXPECT_NEAR(WorldPosition(first).x, 3.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(second).x, 7.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, Solve_SkipsOverflowedFiniteConstraintGeometryAtomically) {
    const float maximum = std::numeric_limits<float>::max();
    const Scene::Entity first = MakeBody({maximum, 0.0F, 0.0F});
    const Scene::Entity second = MakeBody({0.0F, maximum, 0.0F});
    ASSERT_TRUE(constraintSystem.AddDistanceConstraint(
                    DistanceConstraint{first, second, {}, {}, 1.0F})
                    .IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);

    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.solvedConstraintCount, 0U);
    EXPECT_EQ(solved.skippedConstraintCount, 1U);
    EXPECT_FLOAT_EQ(WorldPosition(first).x, maximum);
    EXPECT_FLOAT_EQ(WorldPosition(second).y, maximum);
}

TEST_F(PhysicsConstraintSystemTest, Solve_SkipsSubnormalMassInverseOverflowAtomically) {
    const float subnormalMass = std::numeric_limits<float>::denorm_min();
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F}, subnormalMass);
    const Scene::Entity second = MakeBody({4.0F, 0.0F, 0.0F}, 1.0F);
    ASSERT_TRUE(constraintSystem.AddDistanceConstraint(
                    DistanceConstraint{first, second, {}, {}, 1.0F})
                    .IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);

    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.solvedConstraintCount, 0U);
    EXPECT_EQ(solved.skippedConstraintCount, 1U);
    EXPECT_FLOAT_EQ(WorldPosition(first).x, 0.0F);
    EXPECT_FLOAT_EQ(WorldPosition(second).x, 4.0F);
}

TEST_F(PhysicsConstraintSystemTest, AddHingeConstraint_RejectsOverflowedFiniteAxisAtomically) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});
    const float maximum = std::numeric_limits<float>::max();
    const PhysicsConstraintMutationResult added = constraintSystem.AddHingeConstraint(
        HingeConstraint{first, second, {}, {}, Math::Vector3{maximum, 0.0F, 0.0F}});

    EXPECT_EQ(added.code, PhysicsConstraintCode::InvalidConstraint);
    EXPECT_EQ(constraintSystem.GetConstraintCount(), 0U);
}

TEST_F(PhysicsConstraintSystemTest, SolveHinge_CoincidesAnchorsAndLeavesStaticBodyUnmoved) {
    const Scene::Entity dynamicBody = MakeBody({0.0F, 0.0F, 0.0F}, 1.0F);
    const Scene::Entity staticBody = MakeStatic({6.0F, 0.0F, 0.0F});
    const Math::Vector3 axis{0.0F, 1.0F, 0.0F};

    const PhysicsConstraintMutationResult added = constraintSystem.AddHingeConstraint(
        HingeConstraint{dynamicBody, staticBody, {}, {}, axis});
    ASSERT_TRUE(added.IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);

    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.islandCount, 1U);
    EXPECT_GT(solved.solvedConstraintCount, 0U);
    EXPECT_NEAR(WorldPosition(dynamicBody).x, 6.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(staticBody).x, 6.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, Solve_ProcessesDisconnectedConstraintIslandsDeterministically) {
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F});
    const Scene::Entity second = MakeBody({10.0F, 0.0F, 0.0F});
    const Scene::Entity third = MakeBody({20.0F, 0.0F, 0.0F});
    const Scene::Entity fourth = MakeBody({30.0F, 0.0F, 0.0F});
    ASSERT_TRUE(constraintSystem.AddDistanceConstraint(
                    DistanceConstraint{first, second, {}, {}, 4.0F})
                    .IsAccepted());
    ASSERT_TRUE(constraintSystem.AddDistanceConstraint(
                    DistanceConstraint{third, fourth, {}, {}, 4.0F})
                    .IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);

    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.islandCount, 2U);
    EXPECT_EQ(solved.solvedConstraintCount, 2U);
    EXPECT_NEAR(WorldPosition(first).x, 3.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(second).x, 7.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(third).x, 23.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(fourth).x, 27.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, RemoveConstraint_RejectsStaleGenerationAfterSlotReuse) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});
    const DistanceConstraint descriptor{first, second, {}, {}, 1.0F};

    const PhysicsConstraintMutationResult firstAdd = constraintSystem.AddDistanceConstraint(descriptor);
    ASSERT_TRUE(firstAdd.IsAccepted());
    ASSERT_TRUE(constraintSystem.RemoveConstraint(firstAdd.handle).IsAccepted());
    const PhysicsConstraintMutationResult secondAdd = constraintSystem.AddDistanceConstraint(descriptor);
    ASSERT_TRUE(secondAdd.IsAccepted());
    EXPECT_EQ(secondAdd.handle.index, firstAdd.handle.index);
    EXPECT_NE(secondAdd.handle.generation, firstAdd.handle.generation);

    EXPECT_EQ(constraintSystem.RemoveConstraint(firstAdd.handle).code,
              PhysicsConstraintCode::StaleGeneration);
}

TEST_F(PhysicsConstraintSystemTest, AddDistanceConstraint_EnforcesBoundedCapacity) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});
    const DistanceConstraint descriptor{first, second, {}, {}, 1.0F};

    for (std::size_t index = 0U; index < PhysicsConstraintSystem::kMaximumConstraints; ++index) {
        EXPECT_TRUE(constraintSystem.AddDistanceConstraint(descriptor).IsAccepted());
    }
    EXPECT_EQ(constraintSystem.GetConstraintCount(), PhysicsConstraintSystem::kMaximumConstraints);
    EXPECT_EQ(constraintSystem.AddDistanceConstraint(descriptor).code,
              PhysicsConstraintCode::CapacityExceeded);
}

TEST_F(PhysicsConstraintSystemTest, PhysicsSystemStep_InvokesAttachedConstraintSolver) {
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F});
    const Scene::Entity second = MakeBody({4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(constraintSystem.AddDistanceConstraint(
                    DistanceConstraint{first, second, {}, {}, 2.0F})
                    .IsAccepted());

    PhysicsSystem physicsSystem(collisionSystem, Math::Vector3{});
    physicsSystem.SetConstraintSystem(&constraintSystem);
    physicsSystem.Step(entityManager, sceneGraph, 0.0F);

    EXPECT_NEAR(WorldPosition(first).x, 1.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(second).x, 3.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, AddBallSocketAndSolve_MassWeightedCorrectionKeepsAnchorsCoincident) {
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F}, 1.0F);
    const Scene::Entity second = MakeBody({6.0F, 0.0F, 0.0F}, 1.0F);

    const PhysicsConstraintMutationResult added = constraintSystem.AddBallSocketConstraint(
        BallSocketConstraint{first, second, {}, {}});
    ASSERT_TRUE(added.IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);
    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.islandCount, 1U);
    EXPECT_GT(solved.solvedConstraintCount, 0U);
    EXPECT_NEAR(WorldPosition(first).x, 3.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(second).x, 3.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, AddFixedAndSolve_MassWeightedCorrectionKeepsAnchorsCoincident) {
    const Scene::Entity first = MakeBody({0.0F, 0.0F, 0.0F}, 1.0F);
    const Scene::Entity second = MakeBody({8.0F, 0.0F, 0.0F}, 3.0F);

    const PhysicsConstraintMutationResult added = constraintSystem.AddFixedConstraint(
        FixedConstraint{first, second, {}, {}});
    ASSERT_TRUE(added.IsAccepted());

    const PhysicsConstraintSolveResult solved = constraintSystem.Solve(entityManager, sceneGraph);
    EXPECT_TRUE(solved.islandPlanValid);
    EXPECT_EQ(solved.islandCount, 1U);
    EXPECT_GT(solved.solvedConstraintCount, 0U);
    // first (mass 1, invMass 1) takes 3/4 of the 8.0 delta = moves +6.0 to 6.0
    // second (mass 3, invMass 1/3) takes 1/4 of the 8.0 delta = moves -2.0 to 6.0
    EXPECT_NEAR(WorldPosition(first).x, 6.0F, kEpsilon);
    EXPECT_NEAR(WorldPosition(second).x, 6.0F, kEpsilon);
}

TEST_F(PhysicsConstraintSystemTest, AddBallSocketConstraint_RejectsDegenerateOrNonFiniteParameters) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});

    EXPECT_FALSE(constraintSystem.AddBallSocketConstraint(
        BallSocketConstraint{Scene::kInvalidEntity, second, {}, {}}).IsAccepted());
    EXPECT_FALSE(constraintSystem.AddBallSocketConstraint(
        BallSocketConstraint{first, Scene::kInvalidEntity, {}, {}}).IsAccepted());
    EXPECT_FALSE(constraintSystem.AddBallSocketConstraint(
        BallSocketConstraint{first, first, {}, {}}).IsAccepted());

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(constraintSystem.AddBallSocketConstraint(
        BallSocketConstraint{first, second, {inf, 0.0F, 0.0F}, {}}).IsAccepted());
}

TEST_F(PhysicsConstraintSystemTest, AddFixedConstraint_RejectsDegenerateOrNonFiniteParameters) {
    const Scene::Entity first = MakeBody({});
    const Scene::Entity second = MakeBody({2.0F, 0.0F, 0.0F});

    EXPECT_FALSE(constraintSystem.AddFixedConstraint(
        FixedConstraint{Scene::kInvalidEntity, second, {}, {}}).IsAccepted());
    EXPECT_FALSE(constraintSystem.AddFixedConstraint(
        FixedConstraint{first, Scene::kInvalidEntity, {}, {}}).IsAccepted());
    EXPECT_FALSE(constraintSystem.AddFixedConstraint(
        FixedConstraint{first, first, {}, {}}).IsAccepted());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(constraintSystem.AddFixedConstraint(
        FixedConstraint{first, second, {}, {0.0F, nan, 0.0F}}).IsAccepted());
}

} // namespace
} // namespace UVE::Physics::Tests
