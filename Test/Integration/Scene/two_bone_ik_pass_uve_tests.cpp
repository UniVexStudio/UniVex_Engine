// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/two_bone_ik_pass_uve.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/scene/scene_world_frame_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kEpsilon = 1e-3F;

class TwoBoneIKPassUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    /// The chain every test solves, one metre per bone, straight up from the skeleton's origin:
    /// `Hips` at rest, `UpperArm` a metre above it (the shoulder the chain starts at), `Forearm` a
    /// second metre up (the elbow), and `Hand` a third (the wrist whose OWN ORIGIN is the effector).
    [[nodiscard]] EntityUVE MakeSkeletonUVE(const EntityUVE parent) {
        const EntityUVE skeleton = entityManager.CreateEntityUVE();
        Skeleton3DComponentUVE component;
        component.skeletonAssetPath = "assets/archer.uvskel";
        SkeletonBoneUVE hips;
        hips.name = "Hips";
        SkeletonBoneUVE upperArm;
        upperArm.name = "UpperArm";
        upperArm.parentIndex = 0;
        upperArm.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        SkeletonBoneUVE forearm;
        forearm.name = "Forearm";
        forearm.parentIndex = 1;
        forearm.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        SkeletonBoneUVE hand;
        hand.name = "Hand";
        hand.parentIndex = 2;
        hand.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        component.bones = {hips, upperArm, forearm, hand};
        // The pose a driver would have written this pass: every bone, local like the rest pose.
        component.pose = {SkeletonBonePoseUVE{hips.localPosition, {}, {1.0F, 1.0F, 1.0F}},
                          SkeletonBonePoseUVE{upperArm.localPosition, {}, {1.0F, 1.0F, 1.0F}},
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

    [[nodiscard]] EntityUVE MakeChainUVE(const EntityUVE parent, const EntityUVE skeleton,
                                         TwoBoneIK3DComponentUVE component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        component.skeleton = skeleton;
        entityManager.AddComponentUVE<TwoBoneIK3DComponentUVE>(entity, component);
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, entity, parent);
        }
        return entity;
    }

    /// The component that names the three bones of `MakeSkeletonUVE` by name.
    [[nodiscard]] static TwoBoneIK3DComponentUVE NamedChainUVE() {
        TwoBoneIK3DComponentUVE component;
        component.rootBoneName = "UpperArm";
        component.middleBoneName = "Forearm";
        component.endBoneName = "Hand";
        return component;
    }

    /// Where the wrist ends up in the world, composed from the pose the pass wrote - never from the
    /// solution the pass reported, so a pass that reports one thing and writes another fails.
    [[nodiscard]] Math::Vector3UVE EffectorWorldPositionUVE(const EntityUVE skeletonEntity) {
        const Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
        ObjectWorldFrameUVE frame{};
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, 3U,
                                                 ComposeSceneObjectWorldFrameUVE(entityManager, skeletonEntity),
                                                 frame)) {
            return Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }

    /// The elbow, for the tests that pin which side the pole chose.
    [[nodiscard]] Math::Vector3UVE ElbowWorldPositionUVE(const EntityUVE skeletonEntity) {
        const Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
        ObjectWorldFrameUVE frame{};
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, 2U,
                                                 ComposeSceneObjectWorldFrameUVE(entityManager, skeletonEntity),
                                                 frame)) {
            return Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        }
        return frame.position;
    }
};

TEST_F(TwoBoneIKPassUVETest, SolvesTheChainOntoATargetObjectAndRecordsWhatItResolved) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    // Level with the shoulder, two metres out along +Z, so the bend direction is exactly +Z.
    const EntityUVE pole = MakeObjectUVE(kInvalidEntityUVE, {0.0F, 1.0F, 2.0F});
    TwoBoneIK3DComponentUVE component = NamedChainUVE();
    component.target = target;
    component.poleTarget = pole;
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_EQ(report.refused, 0U);

    const TwoBoneIK3DComponentUVE& chain = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity);
    EXPECT_TRUE(chain.solved);
    EXPECT_TRUE(chain.reached) << "the target is a metre from the shoulder and the arm is two long";
    EXPECT_NEAR(chain.endToTargetDistanceMetres, 0.0F, kEpsilon);
    EXPECT_EQ(chain.resolvedRootBoneIndex, 1U) << "bound by name to UpperArm";
    EXPECT_EQ(chain.resolvedMiddleBoneIndex, 2U);
    EXPECT_EQ(chain.resolvedEndBoneIndex, 3U);

    // The shoulder sits at (0, 1, 0), the target is a metre away along +X, and the pole is +Z, so the
    // elbow is the third point of the equilateral triangle the two circles meet at.
    const Math::Vector3UVE elbow = ElbowWorldPositionUVE(skeleton);
    EXPECT_NEAR(elbow.x, 0.5F, kEpsilon);
    EXPECT_NEAR(elbow.y, 1.0F, kEpsilon);
    EXPECT_NEAR(elbow.z, 0.866F, kEpsilon) << "the pole picked +Z out of the elbow's circle";
    const Math::Vector3UVE effector = EffectorWorldPositionUVE(skeleton);
    EXPECT_NEAR(effector.x, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.y, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.z, 0.0F, kEpsilon) << "the wrist's own origin is the point that reaches";
}

TEST_F(TwoBoneIKPassUVETest, RefusesASkeletonNoDriverPosedInThisPass) {
    // A chain's answer is a blend over the pose, so solving a skeleton that was not posed this pass
    // would blend the same solve twice over several frames. The gate is the pass's whole reason for
    // taking the list of skeletons a driver wrote.
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponentUVE component = NamedChainUVE();
    component.target = target;
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);

    const TwoBoneIKPassReportUVE untouched = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, {});
    EXPECT_EQ(untouched.considered, 0U) << "with no posed skeleton the pass has no work to look at";
    EXPECT_EQ(untouched.refused, 0U);
    EXPECT_FALSE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);
    EXPECT_NEAR(EffectorWorldPositionUVE(skeleton).y, 3.0F, kEpsilon) << "the rest pose is untouched";

    // Posing a DIFFERENT skeleton does not open the gate for this one: the check is on the chain's own
    // skeleton, not on the pass having run at all.
    const EntityUVE other = MakeSkeletonUVE(character);
    const std::vector<EntityUVE> posed{other};
    const TwoBoneIKPassReportUVE refused = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(refused.considered, 1U);
    EXPECT_EQ(refused.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);

    const std::vector<EntityUVE> both{other, skeleton};
    EXPECT_EQ(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, both).solved, 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassUVETest, BlendsTheSolveOverThePoseThroughTheModifiersInfluence) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    // Level with the shoulder again, so the blend test does not depend on a tilted bend plane.
    const EntityUVE pole = MakeObjectUVE(kInvalidEntityUVE, {0.0F, 1.0F, 2.0F});
    TwoBoneIK3DComponentUVE component = NamedChainUVE();
    component.target = target;
    component.poleTarget = pole;
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);
    BoneModifierComponentUVE modifier;
    modifier.influence = 0.5F;
    entityManager.AddComponentUVE<BoneModifierComponentUVE>(chainEntity, modifier);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).reached)
        << "reached describes the solve, not how much of it was blended in";

    // Half an influence leaves the limb between the animation's own pose and the target: past the
    // rest pose, short of the target. The rest wrist is at (0, 3, 0), two and a quarter metres from
    // the target; the solved one is on it.
    const Math::Vector3UVE wrist = EffectorWorldPositionUVE(skeleton);
    const float toTarget = Math::LengthUVE(wrist - Math::Vector3UVE{1.0F, 1.0F, 0.0F});
    EXPECT_GT(toTarget, kEpsilon) << "an eased chain has not arrived";
    EXPECT_LT(toTarget, 2.236F) << "but it is on its way, not still at rest";

    // Zero influence is the same as switching the modifier off, and both refuse rather than write an
    // unblended solve: nothing downstream can read an answer the author's own setting says not to use.
    entityManager.GetComponentUVE<BoneModifierComponentUVE>(chainEntity).influence = 0.0F;
    EXPECT_EQ(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed).refused, 1U);
    entityManager.GetComponentUVE<BoneModifierComponentUVE>(chainEntity).influence = 1.0F;
    entityManager.GetComponentUVE<BoneModifierComponentUVE>(chainEntity).active = false;
    EXPECT_EQ(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed).refused, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassUVETest, RefusesAndLeavesThePoseAloneForEveryChainThatCannotBind) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE boneLess = MakeObjectUVE(character, {}); // a live skeleton entity with no Skeleton3D
    const EntityUVE deadTarget = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    entityManager.DestroyEntityUVE(deadTarget);

    TwoBoneIK3DComponentUVE missingBone = NamedChainUVE();
    missingBone.endBoneName = "Tail";
    const EntityUVE unknownBone = MakeChainUVE(character, skeleton, missingBone);

    TwoBoneIK3DComponentUVE notAChain = NamedChainUVE();
    notAChain.middleBoneName = "Hand";
    notAChain.endBoneName = "Forearm"; // Hand's child is Forearm's parent, so this is not a chain
    const EntityUVE broken = MakeChainUVE(character, skeleton, notAChain);

    TwoBoneIK3DComponentUVE switchedOff = NamedChainUVE();
    switchedOff.enabled = false;
    const EntityUVE off = MakeChainUVE(character, skeleton, switchedOff);

    TwoBoneIK3DComponentUVE dangling = NamedChainUVE();
    dangling.skeleton = kInvalidEntityUVE;
    const EntityUVE noSkeleton = MakeChainUVE(character, kInvalidEntityUVE, dangling);

    TwoBoneIK3DComponentUVE skeletonWithoutBones = NamedChainUVE();
    const EntityUVE noBones = MakeChainUVE(character, boneLess, skeletonWithoutBones);

    TwoBoneIK3DComponentUVE goneTarget = NamedChainUVE();
    goneTarget.target = deadTarget;
    goneTarget.poleTarget = kInvalidEntityUVE;
    const EntityUVE vanished = MakeChainUVE(character, skeleton, goneTarget);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 6U);
    EXPECT_EQ(report.solved, 0U);
    EXPECT_EQ(report.refused, 6U);

    for (const EntityUVE entity : {unknownBone, broken, off, noSkeleton, noBones, vanished}) {
        const TwoBoneIK3DComponentUVE& chain = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(entity);
        EXPECT_FALSE(chain.solved);
        EXPECT_FALSE(chain.reached);
        EXPECT_EQ(chain.resolvedRootBoneIndex, kInvalidSkeletonBoneIndexUVE)
            << "a refusal leaves no stale answer behind";
        EXPECT_EQ(chain.endToTargetDistanceMetres, 0.0F);
    }
    const Skeleton3DComponentUVE& posedSkeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton);
    EXPECT_NEAR(ElbowWorldPositionUVE(skeleton).y, 2.0F, kEpsilon) << "no chain wrote a rotation";
    EXPECT_EQ(posedSkeleton.pose.size(), 4U);
}

TEST_F(TwoBoneIKPassUVETest, LetsAnIndexBeatANameWhenItNamesARealBone) {
    // The pair's rule, the same one the attachment carries: the index is the reference that cannot go
    // stale, the name the one an author can read. Here the names point at a chain the rig does not
    // have and the indices at the one it does.
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponentUVE component;
    component.rootBoneIndex = 1U;
    component.rootBoneName = "Hand";
    component.middleBoneIndex = 2U;
    component.middleBoneName = "UpperArm";
    component.endBoneIndex = 3U;
    component.endBoneName = "Forearm";
    component.target = target;
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    ASSERT_EQ(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed).solved, 1U);
    const TwoBoneIK3DComponentUVE& chain = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity);
    EXPECT_EQ(chain.resolvedRootBoneIndex, 1U);
    EXPECT_EQ(chain.resolvedMiddleBoneIndex, 2U);
    EXPECT_EQ(chain.resolvedEndBoneIndex, 3U);

    // An index that names nothing falls back to the name, which is what keeps a re-exported rig with
    // its bones in a different order working when the component carries both.
    {
        TwoBoneIK3DComponentUVE& updated = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity);
        updated.rootBoneIndex = 99U;
        updated.middleBoneIndex = 99U;
        updated.endBoneIndex = 99U;
        updated.rootBoneName = "UpperArm";
        updated.middleBoneName = "Forearm";
        updated.endBoneName = "Hand";
    }
    ASSERT_EQ(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed).solved, 1U)
        << "the names each named a real bone and the chain is a chain, so it binds";
    EXPECT_EQ(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).resolvedRootBoneIndex, 1U);

    // Both references missing the chain: the indices name nothing, and the names now sketch a chain
    // the rig does not have. A refusal leaves the pose as the animation wrote it rather than binding
    // whichever bones happen to be reachable.
    {
        TwoBoneIK3DComponentUVE& updated = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity);
        updated.rootBoneName = "Hand";
        updated.middleBoneName = "UpperArm";
        updated.endBoneName = "Forearm";
    }
    const TwoBoneIKPassReportUVE refused = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(refused.solved, 0U);
    EXPECT_EQ(refused.refused, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassUVETest, OrdersTwoChainsOnOneSkeletonByTheirPriority) {
    // Two chains over the same three bones: the second one has to solve against the pose the first
    // left, or the limb would depend on storage order. The authored priority is what decides which is
    // second, and the winner is the chain whose target the wrist actually ends on.
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE pole = MakeObjectUVE(kInvalidEntityUVE, {0.0F, 0.0F, 2.0F});
    const EntityUVE forward = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    const EntityUVE backward = MakeObjectUVE(kInvalidEntityUVE, {-1.0F, 1.0F, 0.0F});

    TwoBoneIK3DComponentUVE first = NamedChainUVE();
    first.target = forward;
    first.poleTarget = pole;
    const EntityUVE firstEntity = MakeChainUVE(character, skeleton, first);
    TwoBoneIK3DComponentUVE second = NamedChainUVE();
    second.target = backward;
    second.poleTarget = pole;
    const EntityUVE secondEntity = MakeChainUVE(character, skeleton, second);
    ProcessComponentUVE firstProcess;
    firstProcess.physicsPriority = 0;
    entityManager.AddComponentUVE<ProcessComponentUVE>(firstEntity, firstProcess);
    ProcessComponentUVE secondProcess;
    secondProcess.physicsPriority = 10;
    entityManager.AddComponentUVE<ProcessComponentUVE>(secondEntity, secondProcess);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 2U);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(firstEntity).solved);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(secondEntity).solved);
    const Math::Vector3UVE afterSecond = EffectorWorldPositionUVE(skeleton);
    EXPECT_NEAR(afterSecond.x, -1.0F, kEpsilon) << "the higher priority solved last, so it is the pose";

    // Swapping the priorities swaps the winner: the pass is ordered by what the author wrote, not by
    // the order the entities happen to live in.
    entityManager.GetComponentUVE<ProcessComponentUVE>(firstEntity).physicsPriority = 20;
    static_cast<void>(SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed));
    const Math::Vector3UVE afterFirst = EffectorWorldPositionUVE(skeleton);
    EXPECT_NEAR(afterFirst.x, 1.0F, kEpsilon);
}

TEST_F(TwoBoneIKPassUVETest, SkipsChainsThatDoNotTick) {
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    const EntityUVE target = MakeObjectUVE(kInvalidEntityUVE, {1.0F, 1.0F, 0.0F});
    TwoBoneIK3DComponentUVE component = NamedChainUVE();
    component.target = target;
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);
    ProcessComponentUVE process;
    process.mode = TickModeUVE::Never;
    entityManager.AddComponentUVE<ProcessComponentUVE>(chainEntity, process);
    sceneGraph.UpdateUVE(entityManager);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.considered, 0U) << "an entity that does not run is not looked at, like every other system";
    EXPECT_FALSE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);

    // Switching it back on solves it on the very next pass, with no other state to reset.
    entityManager.GetComponentUVE<ProcessComponentUVE>(chainEntity).mode = TickModeUVE::Running;
    sceneGraph.UpdateUVE(entityManager);
    const TwoBoneIKPassReportUVE again = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(again.considered, 1U);
    EXPECT_EQ(again.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).solved);
}

TEST_F(TwoBoneIKPassUVETest, ReadsTheTargetAndPoleInTheSkeletonsOwnSpace) {
    // Foot planting and hand placement are places on the character's own body, so the authored
    // fallbacks are read in the skeleton's space: a character turned a quarter left reaches the point
    // it authored, not the world axis it would be if the values were read raw.
    const EntityUVE character = MakeObjectUVE(kInvalidEntityUVE, {});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    // +90 degrees about Y, which sends the skeleton's +X to the world's -Z and its +Z to +X.
    TransformComponentUVE& skeletonTransform = entityManager.GetComponentUVE<TransformComponentUVE>(skeleton);
    skeletonTransform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    TwoBoneIK3DComponentUVE component = NamedChainUVE();
    component.targetPosition = Math::Vector3UVE{1.0F, 1.0F, 0.0F};
    component.poleDirection = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    const EntityUVE chainEntity = MakeChainUVE(character, skeleton, component);

    const std::vector<EntityUVE> posed{skeleton};
    const TwoBoneIKPassReportUVE report = SyncTwoBoneIK3DObjectsUVE(entityManager, sceneGraph, posed);
    EXPECT_EQ(report.solved, 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity).reached);

    const Math::Vector3UVE effector = EffectorWorldPositionUVE(skeleton);
    EXPECT_NEAR(effector.x, 0.0F, kEpsilon) << "the authored (1, 1, 0) is at the world's (0, 1, -1)";
    EXPECT_NEAR(effector.y, 1.0F, kEpsilon);
    EXPECT_NEAR(effector.z, -1.0F, kEpsilon);
    // The authored pole's +Z became the world's +X, so the elbow bent that way rather than in Z.
    const Math::Vector3UVE elbow = ElbowWorldPositionUVE(skeleton);
    EXPECT_GT(elbow.x, 0.8F);
    EXPECT_NEAR(elbow.z, -0.5F, kEpsilon);
}

} // namespace
} // namespace UVE::Scene
