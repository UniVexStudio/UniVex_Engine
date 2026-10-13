// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/camera_follow_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/camera_follow_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

class CameraFollowUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeCameraUVE(const EntityUVE target, const Math::Vector3UVE offset, const bool flagged) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE pose;
        pose.localPosition = {100.0F, 100.0F, 100.0F};
        pose.localScale = {2.0F, 2.0F, 2.0F};
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, pose);
        WorldTransformComponentUVE world;
        world.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, world);
        entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, HierarchyComponentUVE{});
        CameraFollowComponentUVE follow;
        follow.target = target;
        follow.offset = offset;
        follow.followPossessedPawn = flagged;
        entityManager.AddComponentUVE<CameraFollowComponentUVE>(entity, follow);
        return entity;
    }

    EntityUVE MakeTargetUVE(const Math::Vector3UVE worldPosition) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        WorldTransformComponentUVE world;
        world.worldPosition = worldPosition; // propagation's output, set directly: no graph here
        world.dirty = false;
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, world);
        entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, HierarchyComponentUVE{});
        return entity;
    }

    EntityUVE MakeControllerUVE(const ControllerKindUVE kind) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ControllerComponentUVE controller;
        controller.kind = kind;
        entityManager.AddComponentUVE<ControllerComponentUVE>(entity, controller);
        return entity;
    }
};

TEST_F(CameraFollowUVETest, FollowComponent_ValidatorAcceptsAnyTargetWithFiniteOffset) {
    CameraFollowComponentUVE follow;
    follow.target = entityManager.CreateEntityUVE();
    EXPECT_TRUE(IsCameraFollowComponentValidUVE(follow));
    follow.offset = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsCameraFollowComponentValidUVE(follow));
}

TEST_F(CameraFollowUVETest, Follow_SnapsRootCameraToTargetPlusOffset) {
    const EntityUVE target = MakeTargetUVE({1.0F, 2.0F, 3.0F});
    const EntityUVE camera = MakeCameraUVE(target, {0.0F, 5.0F, 0.0F}, false);

    UpdateCameraFollowUVE(entityManager);

    const TransformComponentUVE pose = entityManager.GetComponentUVE<TransformComponentUVE>(camera);
    EXPECT_EQ(pose.localPosition, (Math::Vector3UVE{1.0F, 7.0F, 3.0F}));
    EXPECT_EQ(pose.localScale, (Math::Vector3UVE{2.0F, 2.0F, 2.0F}));
    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty);
}

TEST_F(CameraFollowUVETest, Follow_SkipsParentedCamera) {
    const EntityUVE rig = MakeTargetUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeTargetUVE({1.0F, 2.0F, 3.0F});
    const EntityUVE camera = MakeCameraUVE(target, {}, false);
    entityManager.GetComponentUVE<HierarchyComponentUVE>(camera).parent = rig;

    UpdateCameraFollowUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(camera).localPosition,
              (Math::Vector3UVE{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty);
}

TEST_F(CameraFollowUVETest, Follow_SkipsDeadAndTransformlessTargets) {
    const EntityUVE dead = MakeTargetUVE({1.0F, 2.0F, 3.0F});
    const EntityUVE bare = entityManager.CreateEntityUVE();
    const EntityUVE firstCamera = MakeCameraUVE(dead, {}, false);
    const EntityUVE secondCamera = MakeCameraUVE(bare, {}, false);
    entityManager.DestroyEntityUVE(dead);

    UpdateCameraFollowUVE(entityManager);

    for (const EntityUVE camera : {firstCamera, secondCamera}) {
        EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(camera).localPosition,
                  (Math::Vector3UVE{100.0F, 100.0F, 100.0F}));
        EXPECT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty);
    }
}

TEST_F(CameraFollowUVETest, Follow_ParkedCameraHoldsLastPose) {
    const EntityUVE camera = MakeCameraUVE(kInvalidEntityUVE, {0.0F, 5.0F, 0.0F}, false);

    UpdateCameraFollowUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(camera).localPosition,
              (Math::Vector3UVE{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty);
}

TEST_F(CameraFollowUVETest, Follow_RefusesPoisonedPoses) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const EntityUVE target = MakeTargetUVE({nan, nan, nan});
    const EntityUVE camera = MakeCameraUVE(target, {}, false);

    UpdateCameraFollowUVE(entityManager);

    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(camera).localPosition,
              (Math::Vector3UVE{100.0F, 100.0F, 100.0F}));
    EXPECT_FALSE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(camera).dirty);
}

TEST_F(CameraFollowUVETest, Hook_RetargetsFlaggedFollowersOnPlayerPossessOnly) {
    const EntityUVE player = MakeControllerUVE(ControllerKindUVE::Player);
    const EntityUVE ai = MakeControllerUVE(ControllerKindUVE::AI);
    const EntityUVE pawn = MakeTargetUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE aiPawn = MakeTargetUVE({9.0F, 9.0F, 9.0F});
    entityManager.AddComponentUVE<PawnComponentUVE>(pawn, PawnComponentUVE{});
    entityManager.AddComponentUVE<PawnComponentUVE>(aiPawn, PawnComponentUVE{});
    const EntityUVE manualTarget = MakeTargetUVE({5.0F, 5.0F, 5.0F});
    const EntityUVE flagged = MakeCameraUVE(manualTarget, {}, true);
    const EntityUVE manual = MakeCameraUVE(manualTarget, {}, false);

    ASSERT_TRUE(PossessControllerUVE(entityManager, player, pawn));
    RetargetPossessionFollowersUVE(entityManager, player, pawn);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(flagged).target, pawn);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(manual).target, manualTarget);

    // An AI stealing a pawn must not yank the player's camera.
    ASSERT_TRUE(PossessControllerUVE(entityManager, ai, aiPawn));
    RetargetPossessionFollowersUVE(entityManager, ai, aiPawn);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(flagged).target, pawn);
}

TEST_F(CameraFollowUVETest, Hook_ReleasesFlaggedFollowersOnPlayerUnpossessOnly) {
    const EntityUVE player = MakeControllerUVE(ControllerKindUVE::Player);
    const EntityUVE ai = MakeControllerUVE(ControllerKindUVE::AI);
    const EntityUVE pawn = MakeTargetUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE aiPawn = MakeTargetUVE({9.0F, 9.0F, 9.0F});
    const EntityUVE other = MakeTargetUVE({5.0F, 5.0F, 5.0F});
    entityManager.AddComponentUVE<PawnComponentUVE>(pawn, PawnComponentUVE{});
    entityManager.AddComponentUVE<PawnComponentUVE>(aiPawn, PawnComponentUVE{});
    const EntityUVE tracking = MakeCameraUVE(pawn, {}, true);
    const EntityUVE elsewhere = MakeCameraUVE(other, {}, true);
    const EntityUVE manual = MakeCameraUVE(pawn, {}, false);
    const EntityUVE aiTracking = MakeCameraUVE(aiPawn, {}, true);
    ASSERT_TRUE(PossessControllerUVE(entityManager, player, pawn));
    ASSERT_TRUE(PossessControllerUVE(entityManager, ai, aiPawn));

    ASSERT_TRUE(UnpossessControllerUVE(entityManager, player));
    ReleasePossessionFollowersUVE(entityManager, player, pawn);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(tracking).target, kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(elsewhere).target, other);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(manual).target, pawn);

    ASSERT_TRUE(UnpossessControllerUVE(entityManager, ai));
    ReleasePossessionFollowersUVE(entityManager, ai, aiPawn);
    EXPECT_EQ(entityManager.GetComponentUVE<CameraFollowComponentUVE>(aiTracking).target, aiPawn);
}

} // namespace
} // namespace UVE::Scene::Tests
