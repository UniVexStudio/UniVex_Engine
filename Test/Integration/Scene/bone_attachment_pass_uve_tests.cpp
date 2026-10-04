// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/bone_attachment_pass_uve.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

class BoneAttachmentPassUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    /// A root at the skeleton's origin and a hand half a metre along its X.
    [[nodiscard]] EntityUVE MakeSkeletonUVE(const EntityUVE parent) {
        const EntityUVE skeleton = entityManager.CreateEntityUVE();
        Skeleton3DComponentUVE component;
        component.skeletonAssetPath = "assets/character.uvskel";
        component.bones = {SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                           SkeletonBoneUVE{"hand", 0, {0.5F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeleton, component);
        sceneGraph.AttachTransformUVE(entityManager, skeleton, TransformComponentUVE{});
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, skeleton, parent);
        }
        return skeleton;
    }

    [[nodiscard]] EntityUVE MakeAttachmentUVE(const EntityUVE parent, const EntityUVE skeleton,
                                              BoneAttachment3DComponentUVE component) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        component.skeleton = skeleton;
        entityManager.AddComponentUVE<BoneAttachment3DComponentUVE>(entity, component);
        sceneGraph.AttachTransformUVE(entityManager, entity, TransformComponentUVE{});
        if (parent != kInvalidEntityUVE) {
            sceneGraph.SetParentUVE(entityManager, entity, parent);
        }
        return entity;
    }
};

TEST_F(BoneAttachmentPassUVETest, PutsAnAttachmentOnItsBoneInItsParentsSpace) {
    // Character at x=10, skeleton inside it, attachment sitting 10 cm up the hand bone. The object is
    // a real child of the character, so what the pass writes is the PARENT-RELATIVE transform that
    // the scene graph will compose back onto the bone's world frame - not the world frame itself,
    // which propagation would then move a second time.
    const EntityUVE character = entityManager.CreateEntityUVE();
    TransformComponentUVE characterTransform;
    characterTransform.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, character, characterTransform);
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    BoneAttachment3DComponentUVE attachment;
    attachment.boneName = "hand";
    attachment.localPosition = Math::Vector3UVE{0.0F, 0.1F, 0.0F};
    const EntityUVE entity = MakeAttachmentUVE(character, skeleton, attachment);

    const BoneAttachmentPassReportUVE report = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.bound, 1U);
    EXPECT_EQ(report.unbound, 0U);

    const BoneAttachment3DComponentUVE& resolved =
        entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity);
    EXPECT_TRUE(resolved.bound);
    EXPECT_EQ(resolved.resolvedBoneIndex, 1U) << "the pass records which bone it actually bound";

    const TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
    EXPECT_NEAR(local.localPosition.x, 0.5F, kEpsilon) << "the hand's half metre, relative to the character";
    EXPECT_NEAR(local.localPosition.y, 0.1F, kEpsilon);
    EXPECT_NEAR(local.localPosition.z, 0.0F, kEpsilon);
    EXPECT_TRUE(entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty)
        << "the world transform is marked for the propagation pass rather than written here";

    sceneGraph.UpdateUVE(entityManager);
    const Math::Vector3UVE world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    EXPECT_NEAR(world.x, 10.5F, kEpsilon);
    EXPECT_NEAR(world.y, 0.1F, kEpsilon);
    EXPECT_NEAR(world.z, 0.0F, kEpsilon);
}

TEST_F(BoneAttachmentPassUVETest, FollowsTheRuntimePoseWhenTheSkeletonHasOne) {
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    BoneAttachment3DComponentUVE attachment;
    attachment.boneName = "hand";
    const EntityUVE entity = MakeAttachmentUVE(character, skeleton, attachment);

    // Something raised the root a metre. The hand's own transform is relative to the ROOT bone, so it
    // goes with it - the pose is read per bone, and the bones the pose does not cover keep their rest
    // transform.
    Skeleton3DComponentUVE& posed = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton);
    posed.pose = {SkeletonBonePoseUVE{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};

    static_cast<void>(SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph));
    sceneGraph.UpdateUVE(entityManager);
    const Math::Vector3UVE world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    EXPECT_NEAR(world.x, 0.5F, kEpsilon);
    EXPECT_NEAR(world.y, 1.0F, kEpsilon);
}

TEST_F(BoneAttachmentPassUVETest, ResolvesNestedAttachmentsParentsFirst) {
    // A rifle in the hand, a scope on the rifle. The scope has to be composed from the rifle's
    // transform as this pass wrote it, not from the transform it was authored with - which is why the
    // pass resolves by hierarchy depth rather than in storage order.
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    BoneAttachment3DComponentUVE rifleComponent;
    rifleComponent.boneName = "hand";
    const EntityUVE rifle = MakeAttachmentUVE(character, skeleton, rifleComponent);
    BoneAttachment3DComponentUVE scopeComponent;
    scopeComponent.boneName = "root";
    scopeComponent.localPosition = Math::Vector3UVE{0.0F, 0.2F, 0.0F};
    const EntityUVE scope = MakeAttachmentUVE(rifle, skeleton, scopeComponent);

    const BoneAttachmentPassReportUVE report = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(report.bound, 2U);

    // The scope rides the root bone at the character's origin, 20 cm up: relative to the RIFLE, which
    // this pass already put half a metre along +X.
    const TransformComponentUVE& scopeLocal = entityManager.GetComponentUVE<TransformComponentUVE>(scope);
    EXPECT_NEAR(scopeLocal.localPosition.x, -0.5F, kEpsilon);
    EXPECT_NEAR(scopeLocal.localPosition.y, 0.2F, kEpsilon);
    sceneGraph.UpdateUVE(entityManager);
    const Math::Vector3UVE scopeWorld = entityManager.GetComponentUVE<WorldTransformComponentUVE>(scope).worldPosition;
    EXPECT_NEAR(scopeWorld.x, 0.0F, kEpsilon);
    EXPECT_NEAR(scopeWorld.y, 0.2F, kEpsilon);
}

TEST_F(BoneAttachmentPassUVETest, LeavesAnAttachmentAloneWhenItCannotResolveAndSaysSo) {
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const EntityUVE skeleton = MakeSkeletonUVE(character);

    // Four unbound reasons, each of which keeps the authored transform: a bone the skeleton does not
    // have, an attachment switched off, a skeleton switched off, and a skeleton reference that names
    // no live entity at all.
    BoneAttachment3DComponentUVE missingBone;
    missingBone.boneName = "tail";
    const EntityUVE tail = MakeAttachmentUVE(character, skeleton, missingBone);
    BoneAttachment3DComponentUVE switchedOff;
    switchedOff.boneName = "hand";
    switchedOff.enabled = false;
    const EntityUVE off = MakeAttachmentUVE(character, skeleton, switchedOff);
    BoneAttachment3DComponentUVE onDisabledSkeleton;
    onDisabledSkeleton.boneName = "hand";
    const EntityUVE orphan = MakeAttachmentUVE(character, skeleton, onDisabledSkeleton);
    BoneAttachment3DComponentUVE dangling;
    dangling.boneName = "hand";
    const EntityUVE danglingEntity = MakeAttachmentUVE(character, kInvalidEntityUVE, dangling);

    entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton).enabled = false;
    const BoneAttachmentPassReportUVE report = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 4U);
    EXPECT_EQ(report.bound, 0U);
    EXPECT_EQ(report.unbound, 4U);

    for (const EntityUVE entity : {tail, off, orphan, danglingEntity}) {
        const BoneAttachment3DComponentUVE& component =
            entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity);
        EXPECT_FALSE(component.bound);
        EXPECT_EQ(component.resolvedBoneIndex, kInvalidSkeletonBoneIndexUVE);
        const Math::Vector3UVE position = entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
        EXPECT_NEAR(position.x, 0.0F, kEpsilon) << "an unresolved attachment keeps what it was authored with";
        EXPECT_NEAR(position.y, 0.0F, kEpsilon);
        EXPECT_NEAR(position.z, 0.0F, kEpsilon);
    }
}

TEST_F(BoneAttachmentPassUVETest, LetsABoneIndexBeatTheNameWhenItNamesARealBone) {
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    BoneAttachment3DComponentUVE component;
    component.boneName = "hand";
    component.boneIndex = 0U; // the root, which wins over the name by the pair's rule
    const EntityUVE entity = MakeAttachmentUVE(character, skeleton, component);

    static_cast<void>(SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph));
    EXPECT_EQ(entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity).resolvedBoneIndex, 0U);
    sceneGraph.UpdateUVE(entityManager);
    const Math::Vector3UVE world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).worldPosition;
    EXPECT_NEAR(world.x, 0.0F, kEpsilon) << "the root bone is at the skeleton, not half a metre along it";

    // An index that names nothing falls back to the name - that is what makes a re-exported rig with
    // its bones in a different order keep working when the attachment carries both.
    // Read back what is on the entity: the reference lives there, and a locally built component that
    // never had it set would clear the skeleton rather than re-point the bone.
    BoneAttachment3DComponentUVE updated =
        entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity);
    updated.boneIndex = 9U;
    updated.boneName = "hand";
    entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity) = updated;
    static_cast<void>(SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph));
    EXPECT_EQ(entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity).resolvedBoneIndex, 1U);
}

TEST_F(BoneAttachmentPassUVETest, SkipsAttachmentsThatDoNotTick) {
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const EntityUVE skeleton = MakeSkeletonUVE(character);
    BoneAttachment3DComponentUVE component;
    component.boneName = "hand";
    const EntityUVE entity = MakeAttachmentUVE(character, skeleton, component);
    ProcessComponentUVE process;
    process.mode = TickModeUVE::Never;
    entityManager.AddComponentUVE<ProcessComponentUVE>(entity, process);
    sceneGraph.UpdateUVE(entityManager);

    const BoneAttachmentPassReportUVE report = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 0U) << "an entity that does not run is not looked at, like every other system";
    EXPECT_FALSE(entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity).bound);

    // Switching it back on resolves it on the very next pass, with no other state to reset.
    entityManager.GetComponentUVE<ProcessComponentUVE>(entity).mode = TickModeUVE::Running;
    sceneGraph.UpdateUVE(entityManager);
    const BoneAttachmentPassReportUVE again = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(again.considered, 1U);
    EXPECT_EQ(again.bound, 1U);
}

TEST_F(BoneAttachmentPassUVETest, DoesNothingInASceneWithNoAttachments) {
    const EntityUVE character = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, character, TransformComponentUVE{});
    const BoneAttachmentPassReportUVE report = SyncBoneAttachment3DObjectsUVE(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 0U);
    EXPECT_EQ(report.bound, 0U);
    EXPECT_EQ(report.unbound, 0U);
}

} // namespace
} // namespace UVE::Scene::Tests
