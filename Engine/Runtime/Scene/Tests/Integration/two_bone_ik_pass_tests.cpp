// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "two_bone_ik_pass.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "bone_modifier_component.h"
#include "hierarchy_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "skeleton_3d.h"
#include "two_bone_ik_3d.h"
#include "scene_graph.h"
#include "scene_world_frame.h"

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-3F;

class TwoBoneIKPassTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    /// The chain every test solves, one metre per bone, straight up from the skeleton's origin:
    /// `Hips` at rest, `UpperArm` a metre above it (the shoulder the chain starts at), `Forearm` a
    /// second metre up (the elbow), and `Hand` a third (the wrist whose OWN ORIGIN is the effector).
    [[nodiscard]] Entity MakeSkeleton(const Entity parent) {
        const Entity skeleton = entityManager.CreateEntity();
        Skeleton3DComponent component;
        component.skeletonAssetPath = "assets/archer.uvskel";
        SkeletonBone hips;
        hips.name = "Hips";
        SkeletonBone upperArm;
        upperArm.name = "UpperArm";
        upperArm.parentIndex = 0;
        upperArm.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        SkeletonBone forearm;
        forearm.name = "Forearm";
        forearm.parentIndex = 1;
        forearm.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        SkeletonBone hand;
        hand.name = "Hand";
        hand.parentIndex = 2;
        hand.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
        component.bones = {hips, upperArm, forearm, hand};
        // The pose a driver would have written this pass: every bone, local like the rest pose.
        component.pose = {SkeletonBonePose{hips.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePose{upperArm.localPosition, {}, {1.0F, 1.0F, 1.0F}},
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

    [[nodiscard]] Entity MakeChain(const Entity parent, const Entity skeleton,
                                         TwoBoneIK3DComponent component) {
        const Entity entity = entityManager.CreateEntity();
        component.skeleton = skeleton;
        entityManager.AddComponent<TwoBoneIK3DComponent>(entity, component);
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, entity, parent);
        }
        return entity;
    }

    /// The component that names the three bones of `MakeSkeleton` by name.
    [[nodiscard]] static TwoBoneIK3DComponent NamedChain() {
        TwoBoneIK3DComponent component;
        component.rootBoneName = "UpperArm";
        component.middleBoneName = "Forearm";
        component.endBoneName = "Hand";
        return component;
    }

    /// Where the wrist ends up in the world, composed from the pose the pass wrote - never from the
    /// solution the pass reported, so a pass that reports one thing and writes another fails.
    [[nodiscard]] Math::Vector3 EffectorWorldPosition(const Entity skeletonEntity) {
        const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
        ObjectWorldFrame frame{};
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, 3U,
                                                 ComposeSceneObjectWorldFrame(entityManager, skeletonEntity),
                                                 frame)) {
            return Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }

    /// The elbow, for the tests that pin which side the pole chose.
    [[nodiscard]] Math::Vector3 ElbowWorldPosition(const Entity skeletonEntity) {
        const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
        ObjectWorldFrame frame{};
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, 2U,
                                                 ComposeSceneObjectWorldFrame(entityManager, skeletonEntity),
                                                 frame)) {
            return Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }
};

TEST_F(TwoBoneIKPassTest, SolvesTheChainOntoATargetObjectAndRecordsWhatItResolved) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    // Level with the shoulder, two metres out along +Z, so the bend direction is exactly +Z.
    const Entity pole = MakeObject(kInvalidEntity, {0.0F, 1.0F, 2.0F});
    TwoBoneIK3DComponent component = NamedChain();
    component.target = target;
    component.poleTarget = pole;
    const Entity chainEntity = MakeChain(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_EQ(report.refused, 0U);

    const TwoBoneIK3DComponent& chain = entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity);
    EXPECT_TRUE(chain.solved);
    EXPECT_TRUE(chain.reached) << "the target is a metre from the shoulder and the arm is two long";
    EXPECT_NEAR(chain.endToTargetDistanceMetres, 0.0F, kEpsilon);
    EXPECT_EQ(chain.resolvedRootBoneIndex, 1U) << "bound by name to UpperArm";
    EXPECT_EQ(chain.resolvedMiddleBoneIndex, 2U);
    EXPECT_EQ(chain.resolvedEndBoneIndex, 3U);

    // The shoulder sits at (0, 1, 0), the target is a metre away along +X, and the pole is +Z, so the
    // elbow is the third point of the equilateral triangle the two circles meet at.
    const Math::Vector3 elbow = ElbowWorldPosition(skeleton);
    EXPECT_NEAR(elbow.x, 0.5F, kEpsilon);
    EXPECT_NEAR(elbow.y, 1.0F, kEpsilon);
    EXPECT_NEAR(elbow.z, 0.866F, kEpsilon) << "the pole picked +Z out of the elbow's circle";
    const Math::Vector3 effector = EffectorWorldPosition(skeleton);
    EXPECT_NEAR(effector.x, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.y, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.z, 0.0F, kEpsilon) << "the wrist's own origin is the point that reaches";
}

TEST_F(TwoBoneIKPassTest, RefusesASkeletonNoDriverPosedInThisPass) {
    // A chain's answer is a blend over the pose, so solving a skeleton that was not posed this pass
    // would blend the same solve twice over several frames. The gate is the pass's whole reason for
    // taking the list of skeletons a driver wrote.
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponent component = NamedChain();
    component.target = target;
    const Entity chainEntity = MakeChain(character, skeleton, component);

    const TwoBoneIKPassReport untouched = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, {});
    EXPECT_EQ(untouched.considered, 0U) << "with no posed skeleton the pass has no work to look at";
    EXPECT_EQ(untouched.refused, 0U);
    EXPECT_FALSE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);
    EXPECT_NEAR(EffectorWorldPosition(skeleton).y, 3.0F, kEpsilon) << "the rest pose is untouched";

    // Posing a DIFFERENT skeleton does not open the gate for this one: the check is on the chain's own
    // skeleton, not on the pass having run at all.
    const Entity other = MakeSkeleton(character);
    const std::vector<Entity> posed{other};
    const TwoBoneIKPassReport refused = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(refused.considered, 1U);
    EXPECT_EQ(refused.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);

    const std::vector<Entity> both{other, skeleton};
    EXPECT_EQ(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, both).solved, 1U);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassTest, BlendsTheSolveOverThePoseThroughTheModifiersInfluence) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    // Level with the shoulder again, so the blend test does not depend on a tilted bend plane.
    const Entity pole = MakeObject(kInvalidEntity, {0.0F, 1.0F, 2.0F});
    TwoBoneIK3DComponent component = NamedChain();
    component.target = target;
    component.poleTarget = pole;
    const Entity chainEntity = MakeChain(character, skeleton, component);
    BoneModifierComponent modifier;
    modifier.influence = 0.5F;
    entityManager.AddComponent<BoneModifierComponent>(chainEntity, modifier);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).reached)
        << "reached describes the solve, not how much of it was blended in";

    // Half an influence leaves the limb between the animation's own pose and the target: past the
    // rest pose, short of the target. The rest wrist is at (0, 3, 0), two and a quarter metres from
    // the target; the solved one is on it.
    const Math::Vector3 wrist = EffectorWorldPosition(skeleton);
    const float toTarget = Math::Length(wrist - Math::Vector3{1.0F, 1.0F, 0.0F});
    EXPECT_GT(toTarget, kEpsilon) << "an eased chain has not arrived";
    EXPECT_LT(toTarget, 2.236F) << "but it is on its way, not still at rest";

    // Zero influence is the same as switching the modifier off, and both refuse rather than write an
    // unblended solve: nothing downstream can read an answer the author's own setting says not to use.
    entityManager.GetComponent<BoneModifierComponent>(chainEntity).influence = 0.0F;
    EXPECT_EQ(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed).refused, 1U);
    entityManager.GetComponent<BoneModifierComponent>(chainEntity).influence = 1.0F;
    entityManager.GetComponent<BoneModifierComponent>(chainEntity).active = false;
    EXPECT_EQ(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed).refused, 1U);
    EXPECT_FALSE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassTest, RefusesAndLeavesThePoseAloneForEveryChainThatCannotBind) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity boneLess = MakeObject(character, {}); // a live skeleton entity with no Skeleton3D
    const Entity deadTarget = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    entityManager.DestroyEntity(deadTarget);

    TwoBoneIK3DComponent missingBone = NamedChain();
    missingBone.endBoneName = "Tail";
    const Entity unknownBone = MakeChain(character, skeleton, missingBone);

    TwoBoneIK3DComponent notAChain = NamedChain();
    notAChain.middleBoneName = "Hand";
    notAChain.endBoneName = "Forearm"; // Hand's child is Forearm's parent, so this is not a chain
    const Entity broken = MakeChain(character, skeleton, notAChain);

    TwoBoneIK3DComponent switchedOff = NamedChain();
    switchedOff.enabled = false;
    const Entity off = MakeChain(character, skeleton, switchedOff);

    TwoBoneIK3DComponent dangling = NamedChain();
    dangling.skeleton = kInvalidEntity;
    const Entity noSkeleton = MakeChain(character, kInvalidEntity, dangling);

    TwoBoneIK3DComponent skeletonWithoutBones = NamedChain();
    const Entity noBones = MakeChain(character, boneLess, skeletonWithoutBones);

    TwoBoneIK3DComponent goneTarget = NamedChain();
    goneTarget.target = deadTarget;
    goneTarget.poleTarget = kInvalidEntity;
    const Entity vanished = MakeChain(character, skeleton, goneTarget);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 6U);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 6U);

    for (const Entity entity : {unknownBone, broken, off, noSkeleton, noBones, vanished}) {
        const TwoBoneIK3DComponent& chain = entityManager.GetComponent<TwoBoneIK3DComponent>(entity);
        EXPECT_FALSE(chain.solved);
        EXPECT_FALSE(chain.reached);
        EXPECT_EQ(chain.resolvedRootBoneIndex, kInvalidSkeletonBoneIndex)
            << "a refusal leaves no stale answer behind";
        EXPECT_EQ(chain.endToTargetDistanceMetres, 0.0F);
    }
    const Skeleton3DComponent& posedSkeleton = entityManager.GetComponent<Skeleton3DComponent>(skeleton);
    EXPECT_NEAR(ElbowWorldPosition(skeleton).y, 2.0F, kEpsilon) << "no chain wrote a rotation";
    EXPECT_EQ(posedSkeleton.pose.size(), 4U);
}

TEST_F(TwoBoneIKPassTest, LetsAnIndexBeatANameWhenItNamesARealBone) {
    // The pair's rule, the same one the attachment carries: the index is the reference that cannot go
    // stale, the name the one an author can read. Here the names point at a chain the rig does not
    // have and the indices at the one it does.
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponent component;
    component.rootBoneIndex = 1U;
    component.rootBoneName = "Hand";
    component.middleBoneIndex = 2U;
    component.middleBoneName = "UpperArm";
    component.endBoneIndex = 3U;
    component.endBoneName = "Forearm";
    component.target = target;
    const Entity chainEntity = MakeChain(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    ASSERT_EQ(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed).solved, 1U);
    const TwoBoneIK3DComponent& chain = entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity);
    EXPECT_EQ(chain.resolvedRootBoneIndex, 1U);
    EXPECT_EQ(chain.resolvedMiddleBoneIndex, 2U);
    EXPECT_EQ(chain.resolvedEndBoneIndex, 3U);

    // An index that names nothing falls back to the name, which is what keeps a re-exported rig with
    // its bones in a different order working when the component carries both.
    {
        TwoBoneIK3DComponent& updated = entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity);
        updated.rootBoneIndex = 99U;
        updated.middleBoneIndex = 99U;
        updated.endBoneIndex = 99U;
        updated.rootBoneName = "UpperArm";
        updated.middleBoneName = "Forearm";
        updated.endBoneName = "Hand";
    }
    ASSERT_EQ(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed).solved, 1U)
        << "the names each named a real bone and the chain is a chain, so it binds";
    EXPECT_EQ(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).resolvedRootBoneIndex, 1U);

    // Both references missing the chain: the indices name nothing, and the names now sketch a chain
    // the rig does not have. A refusal leaves the pose as the animation wrote it rather than binding
    // whichever bones happen to be reachable.
    {
        TwoBoneIK3DComponent& updated = entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity);
        updated.rootBoneName = "Hand";
        updated.middleBoneName = "UpperArm";
        updated.endBoneName = "Forearm";
    }
    const TwoBoneIKPassReport refused = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(refused.solved, 0U);
    EXPECT_EQ(refused.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassTest, OrdersTwoChainsOnOneSkeletonByTheirPriority) {
    // Two chains over the same three bones: the second one has to solve against the pose the first
    // left, or the limb would depend on storage order. The authored priority is what decides which is
    // second, and the winner is the chain whose target the wrist actually ends on.
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity pole = MakeObject(kInvalidEntity, {0.0F, 0.0F, 2.0F});
    const Entity forward = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    const Entity backward = MakeObject(kInvalidEntity, {-1.0F, 1.0F, 0.0F});

    TwoBoneIK3DComponent first = NamedChain();
    first.target = forward;
    first.poleTarget = pole;
    const Entity firstEntity = MakeChain(character, skeleton, first);
    TwoBoneIK3DComponent second = NamedChain();
    second.target = backward;
    second.poleTarget = pole;
    const Entity secondEntity = MakeChain(character, skeleton, second);
    ProcessComponent firstProcess;
    firstProcess.physicsPriority = 0;
    entityManager.AddComponent<ProcessComponent>(firstEntity, firstProcess);
    ProcessComponent secondProcess;
    secondProcess.physicsPriority = 10;
    entityManager.AddComponent<ProcessComponent>(secondEntity, secondProcess);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 2U);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(firstEntity).solved);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(secondEntity).solved);
    const Math::Vector3 afterSecond = EffectorWorldPosition(skeleton);
    EXPECT_NEAR(afterSecond.x, -1.0F, kEpsilon) << "the higher priority solved last, so it is the pose";

    // Swapping the priorities swaps the winner: the pass is ordered by what the author wrote, not by
    // the order the entities happen to live in.
    entityManager.GetComponent<ProcessComponent>(firstEntity).physicsPriority = 20;
    static_cast<void>(SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed));
    const Math::Vector3 afterFirst = EffectorWorldPosition(skeleton);
    EXPECT_NEAR(afterFirst.x, 1.0F, kEpsilon);
}

TEST_F(TwoBoneIKPassTest, SkipsChainsThatDoNotTick) {
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    const Entity target = MakeObject(kInvalidEntity, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponent component = NamedChain();
    component.target = target;
    const Entity chainEntity = MakeChain(character, skeleton, component);
    ProcessComponent process;
    process.mode = TickMode::Never;
    entityManager.AddComponent<ProcessComponent>(chainEntity, process);
    sceneGraph.Update(entityManager);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 0U) << "an entity that does not run is not looked at, like every other system";
    EXPECT_FALSE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);

    // Switching it back on solves it on the very next pass, with no other state to reset.
    entityManager.GetComponent<ProcessComponent>(chainEntity).mode = TickMode::Running;
    sceneGraph.Update(entityManager);
    const TwoBoneIKPassReport again = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(again.considered, 1U);
    EXPECT_EQ(again.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassTest, ReadsTheTargetAndPoleInTheSkeletonsOwnSpace) {
    // Foot planting and hand placement are places on the character's own body, so the authored
    // fallbacks are read in the skeleton's space: a character turned a quarter left reaches the point
    // it authored, not the world axis it would be if the values were read raw.
    const Entity character = MakeObject(kInvalidEntity, {});
    const Entity skeleton = MakeSkeleton(character);
    // +90 degrees about Y, which sends the skeleton's +X to the world's -Z and its +Z to +X.
    TransformComponent& skeletonTransform = entityManager.GetComponent<TransformComponent>(skeleton);
    skeletonTransform.localRotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    TwoBoneIK3DComponent component = NamedChain();
    component.targetPosition = Math::Vector3{1.0F, 1.0F, 0.0F};
    component.poleDirection = Math::Vector3{0.0F, 0.0F, 1.0F};
    const Entity chainEntity = MakeChain(character, skeleton, component);

    const std::vector<Entity> posed{skeleton};
    const TwoBoneIKPassReport report = SyncTwoBoneIK3DObjects(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponent<TwoBoneIK3DComponent>(chainEntity).reached);

    const Math::Vector3 effector = EffectorWorldPosition(skeleton);
    EXPECT_NEAR(effector.x, 0.0F, kEpsilon) << "the authored (1, 1, 0) is at the world's (0, 1, -1)";
    EXPECT_NEAR(effector.y, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.z, -1.0F, kEpsilon);
    // The authored pole's +Z became the world's +X, so the elbow bent that way rather than in Z.
    const Math::Vector3 elbow = ElbowWorldPosition(skeleton);
    EXPECT_GT(elbow.x, 0.8F);
    EXPECT_NEAR(elbow.z, -0.5F, kEpsilon);
}

} // namespace
} // namespace UVE::Scene
