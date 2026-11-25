// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_follow.h"

#include <limits>

#include <gtest/gtest.h>

#include "camera_follow_component.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "pawn.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

class CameraFollowTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeCamera(const Entity target, const Math::Vector3 offset, const bool flagged) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent pose;
        pose.localPosition = {100.0F, 100.0F, 100.0F};
        pose.localScale = {2.0F, 2.0F, 2.0F};
        entityManager.AddComponent<TransformComponent>(entity, pose);
        WorldTransformComponent world;
        world.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
        entityManager.AddComponent<HierarchyComponent>(entity, HierarchyComponent{});
        CameraFollowComponent follow;
        follow.target = target;
        follow.offset = offset;
        follow.followPossessedPawn = flagged;
        entityManager.AddComponent<CameraFollowComponent>(entity, follow);
        return entity;
    }

    Entity MakeTarget(const Math::Vector3 worldPosition) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        WorldTransformComponent world;
        world.worldPosition = worldPosition; // propagation's output, set directly: no graph here
        world.dirty = false;
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
        entityManager.AddComponent<HierarchyComponent>(entity, HierarchyComponent{});
        return entity;
    }

    Entity MakeController(const ControllerKind kind) {
        const Entity entity = entityManager.CreateEntity();
        ControllerComponent controller;
        controller.kind = kind;
        entityManager.AddComponent<ControllerComponent>(entity, controller);
        return entity;
    }
};

TEST_F(CameraFollowTest, FollowComponent_ValidatorAcceptsAnyTargetWithFiniteOffset) {
    CameraFollowComponent follow;
    follow.target = entityManager.CreateEntity();
    EXPECT_TRUE(IsCameraFollowComponentValid(follow));
    follow.offset = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsCameraFollowComponentValid(follow));
}

TEST_F(CameraFollowTest, Follow_SnapsRootCameraToTargetPlusOffset) {
    const Entity target = MakeTarget({1.0F, 2.0F, 3.0F});
    const Entity camera = MakeCamera(target, {0.0F, 5.0F, 0.0F}, false);

    UpdateCameraFollow(entityManager);

    const TransformComponent pose = entityManager.GetComponent<TransformComponent>(camera);
    EXPECT_EQ(pose.localPosition, (Math::Vector3{1.0F, 7.0F, 3.0F}));
    EXPECT_EQ(pose.localScale, (Math::Vector3{2.0F, 2.0F, 2.0F}));
    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(camera).dirty);
}

TEST_F(CameraFollowTest, Follow_SkipsParentedCamera) {
    const Entity rig = MakeTarget({0.0F, 0.0F, 0.0F});
    const Entity target = MakeTarget({1.0F, 2.0F, 3.0F});
    const Entity camera = MakeCamera(target, {}, false);
    entityManager.GetComponent<HierarchyComponent>(camera).parent = rig;

    UpdateCameraFollow(entityManager);

    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(camera).localPosition,
              (Math::Vector3{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponent<WorldTransformComponent>(camera).dirty);
}

TEST_F(CameraFollowTest, Follow_SkipsDeadAndTransformlessTargets) {
    const Entity dead = MakeTarget({1.0F, 2.0F, 3.0F});
    const Entity bare = entityManager.CreateEntity();
    const Entity firstCamera = MakeCamera(dead, {}, false);
    const Entity secondCamera = MakeCamera(bare, {}, false);
    entityManager.DestroyEntity(dead);

    UpdateCameraFollow(entityManager);

    for (const Entity camera : {firstCamera, secondCamera}) {
        EXPECT_EQ(entityManager.GetComponent<TransformComponent>(camera).localPosition,
                  (Math::Vector3{100.0F, 100.0F, 100.0F}));
        EXPECT_FALSE(entityManager.GetComponent<WorldTransformComponent>(camera).dirty);
    }
}

TEST_F(CameraFollowTest, Follow_ParkedCameraHoldsLastPose) {
    const Entity camera = MakeCamera(kInvalidEntity, {0.0F, 5.0F, 0.0F}, false);

    UpdateCameraFollow(entityManager);

    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(camera).localPosition,
              (Math::Vector3{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponent<WorldTransformComponent>(camera).dirty);
}

TEST_F(CameraFollowTest, Follow_RefusesPoisonedPoses) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Entity target = MakeTarget({nan, nan, nan});
    const Entity camera = MakeCamera(target, {}, false);

    UpdateCameraFollow(entityManager);

    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(camera).localPosition,
              (Math::Vector3{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponent<WorldTransformComponent>(camera).dirty);
}

TEST_F(CameraFollowTest, Hook_RetargetsFlaggedFollowersOnPlayerPossessOnly) {
    const Entity player = MakeController(ControllerKind::Player);
    const Entity ai = MakeController(ControllerKind::AI);
    const Entity pawn = MakeTarget({0.0F, 0.0F, 0.0F});
    const Entity aiPawn = MakeTarget({9.0F, 9.0F, 9.0F});
    entityManager.AddComponent<PawnComponent>(pawn, PawnComponent{});
    entityManager.AddComponent<PawnComponent>(aiPawn, PawnComponent{});
    const Entity manualTarget = MakeTarget({5.0F, 5.0F, 5.0F});
    const Entity flagged = MakeCamera(manualTarget, {}, true);
    const Entity manual = MakeCamera(manualTarget, {}, false);

    ASSERT_TRUE(PossessController(entityManager, player, pawn));
    RetargetPossessionFollowers(entityManager, player, pawn);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(flagged).target, pawn);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(manual).target, manualTarget);

    // An AI stealing a pawn must not yank the player's camera.
    ASSERT_TRUE(PossessController(entityManager, ai, aiPawn));
    RetargetPossessionFollowers(entityManager, ai, aiPawn);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(flagged).target, pawn);
}

TEST_F(CameraFollowTest, Hook_ReleasesFlaggedFollowersOnPlayerUnpossessOnly) {
    const Entity player = MakeController(ControllerKind::Player);
    const Entity ai = MakeController(ControllerKind::AI);
    const Entity pawn = MakeTarget({0.0F, 0.0F, 0.0F});
    const Entity aiPawn = MakeTarget({9.0F, 9.0F, 9.0F});
    const Entity other = MakeTarget({5.0F, 5.0F, 5.0F});
    entityManager.AddComponent<PawnComponent>(pawn, PawnComponent{});
    entityManager.AddComponent<PawnComponent>(aiPawn, PawnComponent{});
    const Entity tracking = MakeCamera(pawn, {}, true);
    const Entity elsewhere = MakeCamera(other, {}, true);
    const Entity manual = MakeCamera(pawn, {}, false);
    const Entity aiTracking = MakeCamera(aiPawn, {}, true);
    ASSERT_TRUE(PossessController(entityManager, player, pawn));
    ASSERT_TRUE(PossessController(entityManager, ai, aiPawn));

    ASSERT_TRUE(UnpossessController(entityManager, player));
    ReleasePossessionFollowers(entityManager, player, pawn);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(tracking).target, kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(elsewhere).target, other);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(manual).target, pawn);

    ASSERT_TRUE(UnpossessController(entityManager, ai));
    ReleasePossessionFollowers(entityManager, ai, aiPawn);
    EXPECT_EQ(entityManager.GetComponent<CameraFollowComponent>(aiTracking).target, aiPawn);
}

} // namespace
} // namespace UVE::Scene::Tests
