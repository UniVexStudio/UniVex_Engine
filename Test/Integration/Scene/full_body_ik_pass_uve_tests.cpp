// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/full_body_ik_pass_uve.h"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/full_body_ik_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/scene/scene_world_frame_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-3F;

class FullBodyIKPassUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    /// A straight chain of four bones one metre apart - `Hips`, `Spine`, `Forearm`, `Hand` - the
    /// pose a driver would have written this pass, every bone local like the rest pose.
    [[nodiscard]] EntityUVE MakeSkeletonUVE(const EntityUVE parent) {
        const EntityUVE skeleton = entityManager.CreateEntityUVE();
        Skeleton3DComponentUVE component;
        component.skeletonAssetPath = "assets/archer.uvskel";
        SkeletonBoneUVE hips;
        hips.name = "Hips";
        SkeletonBoneUVE spine;
        spine.name = "Spine";
        spine.parentIndex = 0;
        spine.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        SkeletonBoneUVE forearm;
        forearm.name = "Forearm";
        forearm.parentIndex = 1;
        forearm.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        SkeletonBoneUVE hand;
        hand.name = "Hand";
        hand.parentIndex = 2;
        hand.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        component.bones = {hips, spine, forearm, hand};
        component.pose = {SkeletonBonePoseUVE{hips.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePoseUVE{spine.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePoseUVE{forearm.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePoseUVE{hand.localPosition, {}, {1.0F, 1.0F, 1.0F}}};
        entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeleton, component);
        sceneGraph.AttachTransformUVE(entityManager, skeleton, TransformComponentUVE{});
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, skeleton, parent);
        }
        return skeleton;
    }

    [[nodiscard]] EntityUVE MakeObjectUVE(const EntityUVE parent, const Math::Vector3UVE& position) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, entity, parent);
        }
        return entity;
    }

    [[nodiscard]] EntityUVE MakeSolveUVE(const EntityUVE parent, const EntityUVE skeleton,
                                         FullBodyIK3DComponentUVE component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        component.skeleton = skeleton;
        entityManager.AddComponentUVE<FullBodyIK3DComponentUVE>(entity, component);
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, entity, parent);
        }
        return entity;
    }

    /// The solve that pulls the four-bone chain's `Hand` by name, rooted at `Hips`.
    [[nodiscard]] static FullBodyIK3DComponentUVE NamedHandSolveUVE() {
        FullBodyIK3DComponentUVE component;
        component.rootBoneName = "Hips";
        FullBodyIKEffectorUVE effector;
        effector.boneName = "Hand";
        component.effectors = {effector};
        return component;
    }

    /// A bone's world position as the pose the pass wrote - never from the solution the pass
    /// reported, so a pass that reports one thing and writes another fails.
    [[nodiscard]] Math::Vector3UVE BoneWorldPositionUVE(const EntityUVE skeletonEntity,
                                                        const std::uint32_t bone) {
        const Skeleton3DComponentUVE& skeleton =
            entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
        ObjectWorldFrameUVE frame{};
        if (!TryResolveSkeletonBoneWorldFrameUVE(
                skeleton, bone, ComposeSceneObjectWorldFrameUVE(entityManager, skeletonEntity), frame)) {
            return Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }
};

TEST_F(FullBodyIKPassUVETest, SolvesOntoATargetObjectAndRecordsWhatItResolved) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.5F, 1.0F, 0.0F});
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].target = target;
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_EQ(report.refused, 0U);

    const FullBodyIK3DComponentUVE& solve =
        entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(solveEntity);
    EXPECT_TRUE(solve.solved);
    EXPECT_TRUE(solve.converged);
    EXPECT_LE(solve.iterationsUsed, solve.maxIterations);
    ASSERT_EQ(solve.effectors.size(), 1U);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, 3U) << "bound by name to Hand";
    EXPECT_TRUE(solve.effectors[0].reached);
    EXPECT_NEAR(solve.effectors[0].distanceMetres, 0.0F, kEpsilon);

    const Math::Vector3UVE hand = BoneWorldPositionUVE(skeleton, 3U);
    EXPECT_NEAR(hand.x, 1.5F, 1e-2);
    EXPECT_NEAR(hand.y, 1.0F, 1e-2);
}

TEST_F(FullBodyIKPassUVETest, RefusesASkeletonNoDriverPosedThisPassAndLeavesThePoseAlone) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].targetPosition = Math::Vector3UVE{1.5F, 1.0F, 0.0F};
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);

    const Skeleton3DComponentUVE& before = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton);
    const std::vector<SkeletonBonePoseUVE> poseBefore = before.pose;

    // A driver posed SOME OTHER skeleton this pass - so the pass runs and sees the solve - but not
    // this one, whose pose is already a frame old and has had its modifiers applied to it.
    const EntityUVE otherSkeleton = MakeSkeletonUVE(character);
    const std::vector<EntityUVE> posed{otherSkeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);

    const FullBodyIK3DComponentUVE& solve =
        entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(solveEntity);
    EXPECT_FALSE(solve.solved);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, kInvalidSkeletonBoneIndexUVE);
    const Skeleton3DComponentUVE& after = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton);
    ASSERT_EQ(after.pose.size(), poseBefore.size());
    for (std::size_t index = 0U; index < after.pose.size(); ++index) {
        EXPECT_FLOAT_EQ(after.pose[index].position.x, poseBefore[index].position.x);
        EXPECT_FLOAT_EQ(after.pose[index].rotation.w, poseBefore[index].rotation.w);
    }
}

TEST_F(FullBodyIKPassUVETest, RefusesAnEffectorWhoseBoneNamesNothing) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].boneName = "Tail"; // no such bone
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);
    const FullBodyIK3DComponentUVE& solve =
        entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(solveEntity);
    EXPECT_FALSE(solve.solved);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, kInvalidSkeletonBoneIndexUVE);
}

TEST_F(FullBodyIKPassUVETest, RefusesWhenATargetObjectHasGoneRatherThanFallingBack) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.5F, 1.0F, 0.0F});
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].target = target;
    component.effectors[0].targetPosition = Math::Vector3UVE{99.0F, 99.0F, 99.0F}; // must NOT be used
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);

    entityManager.DestroyEntityUVE(target);
    target = kInvalidEntityUVE;

    const std::vector<EntityUVE> posed{skeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(solveEntity).solved);
}

TEST_F(FullBodyIKPassUVETest, BlendsTheSolveOverThePoseThroughInfluence) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].targetPosition = Math::Vector3UVE{1.5F, 1.0F, 0.0F};
    BoneModifierComponentUVE modifier;
    modifier.influence = 0.5F;
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);
    entityManager.AddComponentUVE<BoneModifierComponentUVE>(solveEntity, modifier);

    const std::vector<EntityUVE> posed{skeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    ASSERT_EQ(report.solved, 1U);

    // Half the solve: the hand moved off where the animation left it but did not land on the
    // target - the blend is over rotations, so anything between the two ends is correct and only
    // the two ends themselves are wrong.
    const Math::Vector3UVE hand = BoneWorldPositionUVE(skeleton, 3U);
    const float distanceFromPosed = Math::LengthUVE(hand - Math::Vector3UVE{0.0F, 3.0F, 0.0F});
    const float distanceFromTarget = Math::LengthUVE(hand - Math::Vector3UVE{1.5F, 1.0F, 0.0F});
    EXPECT_GT(distanceFromPosed, 0.1F) << "influence 0.5 must actually move the pose";
    EXPECT_GT(distanceFromTarget, 0.1F) << "influence 0.5 must not land the full solve either";
}

TEST_F(FullBodyIKPassUVETest, RootTranslationWalksTheBodyTowardARemoteTarget) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    FullBodyIK3DComponentUVE component = NamedHandSolveUVE();
    component.effectors[0].targetPosition = Math::Vector3UVE{4.0F, 1.0F, 0.0F}; // past the three-metre reach
    component.rootTranslationWeight = 1.0F;
    const EntityUVE solveEntity = MakeSolveUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    const FullBodyIKPassReportUVE report = SyncFullBodyIK3DObjectsUVE(entityManager, sceneGraph, posed);
    ASSERT_EQ(report.solved, 1U);
    const FullBodyIK3DComponentUVE& solve =
        entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(solveEntity);
    EXPECT_TRUE(solve.effectors[0].reached) << "the hips did the walking";
    const Math::Vector3UVE hips = BoneWorldPositionUVE(skeleton, 0U);
    EXPECT_GT(hips.x, 0.5F) << "the root bone actually translated, not just rotated";
}

} // namespace
} // namespace UVE::Scene
