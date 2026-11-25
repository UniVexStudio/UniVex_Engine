// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "full_body_ik_pass.h"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "bone_modifier_component.h"
#include "hierarchy_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "full_body_ik_3d.h"
#include "skeleton_3d.h"
#include "scene_graph.h"
#include "scene_world_frame.h"

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-3F;

class FullBodyIKPassTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    /// A straight chain of four bones one metre apart - `Hips`, `Spine`, `Forearm`, `Hand` - the
    /// pose a driver would have written this pass, every bone local like the rest pose.
    [[nodiscard]] Entity MakeSkeleton(const Entity parent) {
        const Entity skeleton = entityManager.CreateEntity();
        Skeleton3DComponent component;
        component.skeletonAssetPath = "assets/archer.uvskel";
        SkeletonBone hips;
        hips.name = "Hips";
        SkeletonBone spine;
        spine.name = "Spine";
        spine.parentIndex = 0;
        spine.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        SkeletonBone forearm;
        forearm.name = "Forearm";
        forearm.parentIndex = 1;
        forearm.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        SkeletonBone hand;
        hand.name = "Hand";
        hand.parentIndex = 2;
        hand.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        component.bones = {hips, spine, forearm, hand};
        component.pose = {SkeletonBonePose{hips.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePose{spine.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePose{forearm.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePose{hand.localPosition, {}, {1.0F, 1.0F, 1.0F}}};
        entityManager.AddComponent<Skeleton3DComponent>(skeleton, component);
        sceneGraph.AttachTransform(entityManager, skeleton, TransformComponent{});
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, skeleton, parent);
        }
        return skeleton;
    }

    [[nodiscard]] Entity MakeObject(const Entity parent, const Math::Vector3& position) {
        const Entity entity = entityManager.CreateEntity();
        TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, entity, parent);
        }
        return entity;
    }

    [[nodiscard]] Entity MakeSolve(const Entity parent, const Entity skeleton,
                                         FullBodyIK3DComponent component) {
        const Entity entity = entityManager.CreateEntity();
        component.skeleton = skeleton;
        entityManager.AddComponent<FullBodyIK3DComponent>(entity, component);
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, entity, parent);
        }
        return entity;
    }

    /// The solve that pulls the four-bone chain's `Hand` by name, rooted at `Hips`.
    [[nodiscard]] static FullBodyIK3DComponent NamedHandSolve() {
        FullBodyIK3DComponent component;
        component.rootBoneName = "Hips";
        FullBodyIKEffector effector;
        effector.boneName = "Hand";
        component.effectors = {effector};
        return component;
    }

    /// A bone's world position as the pose the pass wrote - never from the solution the pass
    /// reported, so a pass that reports one thing and writes another fails.
    [[nodiscard]] Math::Vector3 BoneWorldPosition(const Entity skeletonEntity,
                                                        const std::uint32_t bone) {
        const Skeleton3DComponent& skeleton =
            entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
        ObjectWorldFrame frame{};
        if (!TryResolveSkeletonBoneWorldFrame(
                skeleton, bone, ComposeSceneObjectWorldFrame(entityManager, skeletonEntity), frame)) {
            return Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }
};

TEST_F(FullBodyIKPassTest, SolvesOntoATargetObjectAndRecordsWhatItResolved) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.5F, 1.0F, 0.0F});
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].target = target;
    const Entity solveEntity = MakeSolve(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_EQ(report.refused, 0U);

    const FullBodyIK3DComponent& solve =
        entityManager.GetComponent<FullBodyIK3DComponent>(solveEntity);
    EXPECT_TRUE(solve.solved);
    EXPECT_TRUE(solve.converged);
    EXPECT_LE(solve.iterationsUsed, solve.maxIterations);
    ASSERT_EQ(solve.effectors.size(), 1U);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, 3U) << "bound by name to Hand";
    EXPECT_TRUE(solve.effectors[0].reached);
    EXPECT_NEAR(solve.effectors[0].distanceMetres, 0.0F, kEpsilon);

    const Math::Vector3 hand = BoneWorldPosition(skeleton, 3U);
    EXPECT_NEAR(hand.x, 1.5F, 1e-2);
    EXPECT_NEAR(hand.y, 1.0F, 1e-2);
}

TEST_F(FullBodyIKPassTest, RefusesASkeletonNoDriverPosedThisPassAndLeavesThePoseAlone) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].targetPosition = Math::Vector3{1.5F, 1.0F, 0.0F};
    const Entity solveEntity = MakeSolve(character, skeleton, component);

    const Skeleton3DComponent& before = entityManager.GetComponent<Skeleton3DComponent>(skeleton);
    const std::vector<SkeletonBonePose> poseBefore = before.pose;

    // A driver posed SOME OTHER skeleton this pass - so the pass runs and sees the solve - but not
    // this one, whose pose is already a frame old and has had its modifiers applied to it.
    const Entity otherSkeleton = MakeSkeleton(character);
    const std::vector<Entity> posed{otherSkeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);

    const FullBodyIK3DComponent& solve =
        entityManager.GetComponent<FullBodyIK3DComponent>(solveEntity);
    EXPECT_FALSE(solve.solved);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, kInvalidSkeletonBoneIndex);
    const Skeleton3DComponent& after = entityManager.GetComponent<Skeleton3DComponent>(skeleton);
    ASSERT_EQ(after.pose.size(), poseBefore.size());
    for (std::size_t index = 0U; index < after.pose.size(); ++index) {
        EXPECT_FLOAT_EQ(after.pose[index].position.x, poseBefore[index].position.x);
        EXPECT_FLOAT_EQ(after.pose[index].rotation.w, poseBefore[index].rotation.w);
    }
}

TEST_F(FullBodyIKPassTest, RefusesAnEffectorWhoseBoneNamesNothing) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].boneName = "Tail"; // no such bone
    const Entity solveEntity = MakeSolve(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);
    const FullBodyIK3DComponent& solve =
        entityManager.GetComponent<FullBodyIK3DComponent>(solveEntity);
    EXPECT_FALSE(solve.solved);
    EXPECT_EQ(solve.effectors[0].resolvedBoneIndex, kInvalidSkeletonBoneIndex);
}

TEST_F(FullBodyIKPassTest, RefusesWhenATargetObjectHasGoneRatherThanFallingBack) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    Entity target = MakeObject(kInvalidEntity, {1.5F, 1.0F, 0.0F});
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].target = target;
    component.effectors[0].targetPosition = Math::Vector3{99.0F, 99.0F, 99.0F}; // must NOT be used
    const Entity solveEntity = MakeSolve(character, skeleton, component);

    entityManager.DestroyEntity(target);
    target = kInvalidEntity;

    const std::vector<Entity> posed{skeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponent<FullBodyIK3DComponent>(solveEntity).solved);
}

TEST_F(FullBodyIKPassTest, BlendsTheSolveOverThePoseThroughInfluence) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].targetPosition = Math::Vector3{1.5F, 1.0F, 0.0F};
    BoneModifierComponent modifier;
    modifier.influence = 0.5F;
    const Entity solveEntity = MakeSolve(character, skeleton, component);
    entityManager.AddComponent<BoneModifierComponent>(solveEntity, modifier);

    const std::vector<Entity> posed{skeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    ASSERT_EQ(report.solved, 1U);

    // Half the solve: the hand moved off where the animation left it but did not land on the
    // target - the blend is over rotations, so anything between the two ends is correct and only
    // the two ends themselves are wrong.
    const Math::Vector3 hand = BoneWorldPosition(skeleton, 3U);
    const float distanceFromPosed = Math::Length(hand - Math::Vector3{0.0F, 3.0F, 0.0F});
    const float distanceFromTarget = Math::Length(hand - Math::Vector3{1.5F, 1.0F, 0.0F});
    EXPECT_GT(distanceFromPosed, 0.1F) << "influence 0.5 must actually move the pose";
    EXPECT_GT(distanceFromTarget, 0.1F) << "influence 0.5 must not land the full solve either";
}

TEST_F(FullBodyIKPassTest, RootTranslationWalksTheBodyTowardARemoteTarget) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    FullBodyIK3DComponent component = NamedHandSolve();
    component.effectors[0].targetPosition = Math::Vector3{4.0F, 1.0F, 0.0F}; // past the three-metre reach
    component.rootTranslationWeight = 1.0F;
    const Entity solveEntity = MakeSolve(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    const FullBodyIKPassReport report = SyncFullBodyIK3DObjects(entityManager, sceneGraph, posed);
    ASSERT_EQ(report.solved, 1U);
    const FullBodyIK3DComponent& solve =
        entityManager.GetComponent<FullBodyIK3DComponent>(solveEntity);
    EXPECT_TRUE(solve.effectors[0].reached) << "the hips did the walking";
    const Math::Vector3 hips = BoneWorldPosition(skeleton, 0U);
    EXPECT_GT(hips.x, 0.5F) << "the root bone actually translated, not just rotated";
}

} // namespace
} // namespace UVE::Scene
