// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_controller.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "collision_system.h"
#include "collider_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

class FixedCollisionSystem final : public ICollisionSystem {
public:
    std::vector<CollisionPair> pairs;

    [[nodiscard]] std::vector<CollisionPair> DetectCollisions(
        Scene::IEntityManager&) const override {
        return pairs;
    }
};

class CharacterControllerTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CollisionSystem collisionSystem;

    Scene::Entity MakeColliderEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                            std::uint32_t layer = 1U,
                                            std::uint32_t mask = 0xFFFFFFFFU) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider{halfExtents};
        collider.collisionLayer = layer;
        collider.collisionMask = mask;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    Scene::Entity MakeControllerEntity(Math::Vector3 position, Math::Vector3 halfExtents,
                                              std::uint32_t layer = 1U,
                                              std::uint32_t mask = 0xFFFFFFFFU) {
        const Scene::Entity entity = MakeColliderEntity(position, halfExtents, layer, mask);
        entityManager.AddComponent<Scene::Rigid3DComponent>(
            entity, Scene::Rigid3DComponent{1.0F, true});
        return entity;
    }

    [[nodiscard]] Math::Vector3 GetWorldPosition(Scene::Entity entity) {
        return entityManager.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
    }
};

TEST(CharacterControllerCcdPolicyTest, ClassifiesFiniteDisplacementAgainstSweepThreshold) {
    EXPECT_EQ(EvaluateCcdEligibility(Math::Vector3{0.1F, 0.0F, 0.0F}, 0.25F),
              CcdEligibilityStatus::Disabled);
    EXPECT_EQ(EvaluateCcdEligibility(Math::Vector3{0.25F, 0.0F, 0.0F}, 0.25F),
              CcdEligibilityStatus::Enabled);
    EXPECT_EQ(EvaluateCcdEligibility(Math::Vector3{0.0F, 0.0F, 0.0F}, 0.25F),
              CcdEligibilityStatus::Disabled);
}

TEST(CharacterControllerCcdPolicyTest, RejectsNonFiniteDisplacementAndInvalidThreshold) {
    EXPECT_EQ(EvaluateCcdEligibility(
                  Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 0.25F),
              CcdEligibilityStatus::Invalid);
    EXPECT_EQ(EvaluateCcdEligibility(Math::Vector3{1.0F, 0.0F, 0.0F}, 0.0F),
              CcdEligibilityStatus::Invalid);
}

TEST(CharacterControllerCcdPolicyTest, RejectsFiniteDisplacementWhoseLengthOverflows) {
    const float maximumFloat = std::numeric_limits<float>::max();
    EXPECT_EQ(EvaluateCcdEligibility(Math::Vector3{maximumFloat, maximumFloat, 0.0F}, 0.25F),
              CcdEligibilityStatus::Invalid);
}

TEST_F(CharacterControllerTest, Move_RejectsFiniteDisplacementWhoseLengthOverflowsWithoutMutation) {
    const Scene::Entity controller = MakeControllerEntity({1.0F, 2.0F, 3.0F}, {0.5F, 0.5F, 0.5F});
    const Math::Vector3 positionBefore = GetWorldPosition(controller);
    const float maximumFloat = std::numeric_limits<float>::max();

    const CharacterControllerMoveResult moveResult = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {maximumFloat, maximumFloat, 0.0F}, 8U, 0.25F});

    EXPECT_EQ(moveResult.code, CharacterControllerMoveCode::InvalidInput);
    EXPECT_EQ(moveResult.substeps, 0U);
    EXPECT_EQ(GetWorldPosition(controller), positionBefore);
}

TEST_F(CharacterControllerTest, Move_RejectsFiniteDerivedCorrectionOverflowWithoutMutation) {
    const float maximumFloat = std::numeric_limits<float>::max();
    const Scene::Entity controller =
        MakeControllerEntity({maximumFloat, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity target = MakeColliderEntity({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    FixedCollisionSystem overflowCollisionSystem;
    overflowCollisionSystem.pairs.push_back(
        CollisionPair{target, controller, {1.0F, 0.0F, 0.0F}, maximumFloat});
    const Math::Vector3 positionBefore = GetWorldPosition(controller);

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, overflowCollisionSystem,
        CharacterControllerInput{controller, {1.0F, 0.0F, 0.0F}, 8U, 0.25F});

    EXPECT_EQ(result.code, CharacterControllerMoveCode::InvalidInput);
    EXPECT_EQ(result.substeps, 1U);
    EXPECT_EQ(GetWorldPosition(controller), positionBefore);
    EXPECT_TRUE(std::isfinite(GetWorldPosition(controller).x));
    EXPECT_TRUE(std::isfinite(GetWorldPosition(controller).y));
    EXPECT_TRUE(std::isfinite(GetWorldPosition(controller).z));
}

TEST_F(CharacterControllerTest, Move_SkipsNonUnitFiniteCollisionAxisWithoutNonFiniteMutation) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity target = MakeColliderEntity({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    FixedCollisionSystem malformedCollisionSystem;
    malformedCollisionSystem.pairs.push_back(
        CollisionPair{controller, target, {std::numeric_limits<float>::max(), 0.0F, 0.0F}, 1.0F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, malformedCollisionSystem,
        CharacterControllerInput{controller, {1.0F, 0.0F, 0.0F}, 8U, 0.25F});

    EXPECT_EQ(result.code, CharacterControllerMoveCode::Moved);
    EXPECT_FALSE(result.blocked);
    EXPECT_EQ(result.contactCount, 0U);
    const Math::Vector3 position = GetWorldPosition(controller);
    EXPECT_TRUE(std::isfinite(position.x));
    EXPECT_TRUE(std::isfinite(position.y));
    EXPECT_TRUE(std::isfinite(position.z));
    EXPECT_NEAR(position.x, 1.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_FreeSpaceAppliesRequestedDisplacementDeterministically) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {1.0F, 0.0F, 0.0F}, 8U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.blocked);
    EXPECT_EQ(result.substeps, 4U);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_NEAR(GetWorldPosition(controller).x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(result.remainingDisplacement.x, 0.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_WallContactStopsAtMinimumTranslationDistance) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 2.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 32U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_FALSE(result.grounded);
    EXPECT_GT(result.contactCount, 0U);
    EXPECT_NEAR(GetWorldPosition(controller).x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(GetWorldPosition(controller).y, 0.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_GroundContactReportsSupportNormal) {
    const Scene::Entity controller = MakeControllerEntity({0.0F, 2.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({0.0F, 0.0F, 0.0F}, {2.0F, 0.5F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {0.0F, -2.0F, 0.0F}, 16U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.grounded);
    EXPECT_NEAR(result.groundNormal.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(result.groundNormal.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(result.groundNormal.z, 0.0F, 1.0e-5F);
    EXPECT_NEAR(GetWorldPosition(controller).y, 1.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_WallContactPreservesTangentialDisplacement) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 2.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 2.0F, 0.0F}, 32U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_NEAR(GetWorldPosition(controller).x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(GetWorldPosition(controller).y, 2.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_IncompatibleLayerMaskPassesThroughCollider) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F}, 1U, 1U);
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 2.0F, 2.0F}, 2U, 0xFFFFFFFFU);

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 32U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.blocked);
    EXPECT_EQ(result.contactCount, 0U);
    EXPECT_NEAR(GetWorldPosition(controller).x, 3.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, MoveWithToI_StopsBeforeThinWallWithoutTunneling) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.05F, 2.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {10.0F, 0.0F, 0.0F}, 1U, 100.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.toiUsed);
    EXPECT_EQ(result.contactCount, 1U);
    EXPECT_NEAR(result.earliestImpactTime, 0.145F, 1.0e-4F);
    EXPECT_NEAR(GetWorldPosition(controller).x, 1.449F, 2.0e-3F);
    EXPECT_LT(GetWorldPosition(controller).x, 1.5F);
}

TEST_F(CharacterControllerTest, MoveWithToI_StepHeightOptInTraversesLowObstacle) {
    const Scene::Entity controller = MakeControllerEntity({0.0F, 1.0F, 0.0F}, {0.5F, 1.0F, 0.5F});
    MakeColliderEntity({1.0F, 0.25F, 0.0F}, {0.5F, 0.25F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 8U, 100.0F, 45.0F, 0.75F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.toiUsed);
    EXPECT_TRUE(result.stepUpUsed);
    EXPECT_NEAR(GetWorldPosition(controller).x, 3.0F, 1.0e-4F);
    EXPECT_NEAR(GetWorldPosition(controller).y, 1.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, MoveWithToI_StepHeightTooLowPreservesWallBlock) {
    const Scene::Entity controller = MakeControllerEntity({0.0F, 1.0F, 0.0F}, {0.5F, 1.0F, 0.5F});
    MakeColliderEntity({1.0F, 1.0F, 0.0F}, {0.5F, 1.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 8U, 100.0F, 45.0F, 0.75F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.toiUsed);
    EXPECT_FALSE(result.stepUpUsed);
    EXPECT_LT(GetWorldPosition(controller).x, 0.5F);
}

TEST_F(CharacterControllerTest, MoveWithToI_PreservesTangentialDisplacementAfterImpact) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 2.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {10.0F, 3.0F, 0.0F}, 8U, 100.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.toiUsed);
    EXPECT_NEAR(GetWorldPosition(controller).x, 0.999F, 2.0e-3F);
    EXPECT_NEAR(GetWorldPosition(controller).y, 3.0F, 2.0e-3F);
}

TEST_F(CharacterControllerTest, MoveWithToI_IncompatibleLayerMaskPassesThrough) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F}, 1U, 1U);
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.05F, 2.0F, 2.0F}, 2U, 0xFFFFFFFFU);

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {10.0F, 0.0F, 0.0F}, 1U, 100.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_FALSE(result.blocked);
    EXPECT_FALSE(result.toiUsed);
    EXPECT_NEAR(GetWorldPosition(controller).x, 10.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_ClampsInvalidGroundSlopeWithoutRejectingInput) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {0.1F, 0.0F, 0.0F}, 1U, 1.0F, -5.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.inputClamped);
    EXPECT_FALSE(result.grounded);
    EXPECT_NEAR(GetWorldPosition(controller).x, 0.1F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_OptInPushesDynamicTargetWithBoundedNormalSpeed) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity target = MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(target, Scene::Rigid3DComponent{});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 32U, 0.25F, 45.0F, 0.0F,
                                    true, 1.0F, 5.0F, 1.0F / 60.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_EQ(result.pushedBodyCount, 1U);
    EXPECT_FALSE(result.dynamicBodyPushClamped);
    const Scene::Rigid3DComponent& pushedBody =
        entityManager.GetComponent<Scene::Rigid3DComponent>(target);
    EXPECT_NEAR(pushedBody.velocity.x, 5.0F, 1.0e-4F);
    EXPECT_NEAR(pushedBody.velocity.y, 0.0F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_DefaultPushPolicyLeavesDynamicTargetUnchanged) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity target = MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(target, Scene::Rigid3DComponent{});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 32U, 0.25F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_EQ(result.pushedBodyCount, 0U);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::Rigid3DComponent>(target).velocity.x, 0.0F);
}

TEST_F(CharacterControllerTest, Move_DoesNotPushKinematicTargetAndClampsInvalidPushPolicy) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity target = MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(target, Scene::Rigid3DComponent{1.0F, true});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {3.0F, 0.0F, 0.0F}, 32U, 0.25F, 45.0F, 0.0F,
                                    true, -1.0F, 0.0F, -1.0F});

    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.dynamicBodyPushClamped);
    EXPECT_EQ(result.pushedBodyCount, 0U);
    EXPECT_TRUE(entityManager.GetComponent<Scene::Rigid3DComponent>(target).isKinematic);
}

TEST_F(CharacterControllerTest, Move_RejectsDynamicBodyAndClampsInvalidBudget) {
    const Scene::Entity dynamic = MakeColliderEntity({5.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    entityManager.AddComponent<Scene::Rigid3DComponent>(dynamic, Scene::Rigid3DComponent{});
    const CharacterControllerMoveResult rejected = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{dynamic, {1.0F, 0.0F, 0.0F}, 8U, 0.25F});
    EXPECT_EQ(rejected.code, CharacterControllerMoveCode::NonKinematicBody);

    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const CharacterControllerMoveResult clamped = CharacterController::Move(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {0.1F, 0.0F, 0.0F}, 0U, -1.0F});
    ASSERT_TRUE(clamped.IsAccepted());
    EXPECT_TRUE(clamped.inputClamped);
    EXPECT_NEAR(GetWorldPosition(controller).x, 0.1F, 1.0e-4F);
}

TEST_F(CharacterControllerTest, Move_IgnoresUnrelatedWorldPairsWhenCountingContacts) {
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity wall = MakeColliderEntity({0.5F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity dummyA = MakeColliderEntity({100.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    const Scene::Entity dummyB = MakeColliderEntity({100.1F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});

    FixedCollisionSystem crowdedWorld;
    for (std::size_t i = 0U; i < CharacterController::kMaximumContacts + 10U; ++i) {
        crowdedWorld.pairs.push_back(CollisionPair{dummyA, dummyB, {1.0F, 0.0F, 0.0F}, 0.1F});
    }
    crowdedWorld.pairs.push_back(CollisionPair{controller, wall, {1.0F, 0.0F, 0.0F}, 0.25F});

    const CharacterControllerMoveResult result = CharacterController::Move(
        entityManager, sceneGraph, crowdedWorld,
        CharacterControllerInput{controller, {0.25F, 0.0F, 0.0F}, 1U, 1.0F});
    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_FALSE(result.contactsTruncated);
    EXPECT_EQ(result.contactCount, 1U);
}

TEST_F(CharacterControllerTest, MoveWithToI_FindsWallBeyond256NonIntersectingWorldColliders) {
    for (std::size_t i = 0U; i < CharacterController::kMaximumToITargets + 16U; ++i) {
        MakeColliderEntity({-50.0F - static_cast<float>(i) * 2.0F, 50.0F, 0.0F}, {0.25F, 0.25F, 0.25F});
    }
    const Scene::Entity controller = MakeControllerEntity({}, {0.5F, 0.5F, 0.5F});
    MakeColliderEntity({2.0F, 0.0F, 0.0F}, {0.05F, 2.0F, 2.0F});

    const CharacterControllerMoveResult result = CharacterController::MoveWithToI(
        entityManager, sceneGraph, collisionSystem,
        CharacterControllerInput{controller, {10.0F, 0.0F, 0.0F}, 1U, 100.0F});
    ASSERT_TRUE(result.IsAccepted());
    EXPECT_TRUE(result.blocked);
    EXPECT_TRUE(result.toiUsed);
    EXPECT_FALSE(result.contactsTruncated);
    EXPECT_LT(GetWorldPosition(controller).x, 1.5F);
}

} // namespace
} // namespace UVE::Physics::Tests
