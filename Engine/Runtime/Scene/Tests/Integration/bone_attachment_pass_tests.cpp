// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "bone_attachment_pass.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "hierarchy_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "bone_attachment_3d.h"
#include "skeleton_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

class BoneAttachmentPassTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    /// A root at the skeleton's origin and a hand half a metre along its X.
    [[nodiscard]] Entity MakeSkeleton(const Entity parent) {
        const Entity skeleton = entityManager.CreateEntity();
        Skeleton3DComponent component;
        component.skeletonAssetPath = "assets/character.uvskel";
        component.bones = {SkeletonBone{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                           SkeletonBone{"hand", 0, {0.5F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        entityManager.AddComponent<Skeleton3DComponent>(skeleton, component);
        sceneGraph.AttachTransform(entityManager, skeleton, TransformComponent{});
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, skeleton, parent);
        }
        return skeleton;
    }

    [[nodiscard]] Entity MakeAttachment(const Entity parent, const Entity skeleton,
                                              BoneAttachment3DComponent component) {
        const Entity entity = entityManager.CreateEntity();
        component.skeleton = skeleton;
        entityManager.AddComponent<BoneAttachment3DComponent>(entity, component);
        sceneGraph.AttachTransform(entityManager, entity, TransformComponent{});
        if (parent != kInvalidEntity) {
            sceneGraph.SetParent(entityManager, entity, parent);
        }
        return entity;
    }
};

TEST_F(BoneAttachmentPassTest, PutsAnAttachmentOnItsBoneInItsParentsSpace) {
    // Character at x=10, skeleton inside it, attachment sitting 10 cm up the hand bone. The object is
    // a real child of the character, so what the pass writes is the PARENT-RELATIVE transform that
    // the scene graph will compose back onto the bone's world frame - not the world frame itself,
    // which propagation would then move a second time.
    const Entity character = entityManager.CreateEntity();
    TransformComponent characterTransform;
    characterTransform.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, character, characterTransform);
    const Entity skeleton = MakeSkeleton(character);
    BoneAttachment3DComponent attachment;
    attachment.boneName = "hand";
    attachment.localPosition = Math::Vector3{0.0F, 0.1F, 0.0F};
    const Entity entity = MakeAttachment(character, skeleton, attachment);

    const BoneAttachmentPassReport report = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 1U);
    EXPECT_EQ(report.bound, 1U);
    EXPECT_EQ(report.unbound, 0U);

    const BoneAttachment3DComponent& resolved =
        entityManager.GetComponent<BoneAttachment3DComponent>(entity);
    EXPECT_TRUE(resolved.bound);
    EXPECT_EQ(resolved.resolvedBoneIndex, 1U) << "the pass records which bone it actually bound";

    const TransformComponent& local = entityManager.GetComponent<TransformComponent>(entity);
    EXPECT_NEAR(local.localPosition.x, 0.5F, kEpsilon) << "the hand's half metre, relative to the character";
    EXPECT_NEAR(local.localPosition.y, 0.1F, kEpsilon);
    EXPECT_NEAR(local.localPosition.z, 0.0F, kEpsilon);
    EXPECT_TRUE(entityManager.GetComponent<WorldTransformComponent>(entity).dirty)
        << "the world transform is marked for the propagation pass rather than written here";

    sceneGraph.Update(entityManager);
    const Math::Vector3 world = entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    EXPECT_NEAR(world.x, 10.5F, kEpsilon);
    EXPECT_NEAR(world.y, 0.1F, kEpsilon);
    EXPECT_NEAR(world.z, 0.0F, kEpsilon);
}

TEST_F(BoneAttachmentPassTest, FollowsTheRuntimePoseWhenTheSkeletonHasOne) {
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const Entity skeleton = MakeSkeleton(character);
    BoneAttachment3DComponent attachment;
    attachment.boneName = "hand";
    const Entity entity = MakeAttachment(character, skeleton, attachment);

    // Something raised the root a metre. The hand's own transform is relative to the ROOT bone, so it
    // goes with it - the pose is read per bone, and the bones the pose does not cover keep their rest
    // transform.
    Skeleton3DComponent& posed = entityManager.GetComponent<Skeleton3DComponent>(skeleton);
    posed.pose = {SkeletonBonePose{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};

    static_cast<void>(SyncBoneAttachment3DObjects(entityManager, sceneGraph));
    sceneGraph.Update(entityManager);
    const Math::Vector3 world = entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    EXPECT_NEAR(world.x, 0.5F, kEpsilon);
    EXPECT_NEAR(world.y, 1.0F, kEpsilon);
}

TEST_F(BoneAttachmentPassTest, ResolvesNestedAttachmentsParentsFirst) {
    // A rifle in the hand, a scope on the rifle. The scope has to be composed from the rifle's
    // transform as this pass wrote it, not from the transform it was authored with - which is why the
    // pass resolves by hierarchy depth rather than in storage order.
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const Entity skeleton = MakeSkeleton(character);
    BoneAttachment3DComponent rifleComponent;
    rifleComponent.boneName = "hand";
    const Entity rifle = MakeAttachment(character, skeleton, rifleComponent);
    BoneAttachment3DComponent scopeComponent;
    scopeComponent.boneName = "root";
    scopeComponent.localPosition = Math::Vector3{0.0F, 0.2F, 0.0F};
    const Entity scope = MakeAttachment(rifle, skeleton, scopeComponent);

    const BoneAttachmentPassReport report = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(report.bound, 2U);

    // The scope rides the root bone at the character's origin, 20 cm up: relative to the RIFLE, which
    // this pass already put half a metre along +X.
    const TransformComponent& scopeLocal = entityManager.GetComponent<TransformComponent>(scope);
    EXPECT_NEAR(scopeLocal.localPosition.x, -0.5F, kEpsilon);
    EXPECT_NEAR(scopeLocal.localPosition.y, 0.2F, kEpsilon);
    sceneGraph.Update(entityManager);
    const Math::Vector3 scopeWorld = entityManager.GetComponent<WorldTransformComponent>(scope).worldPosition;
    EXPECT_NEAR(scopeWorld.x, 0.0F, kEpsilon);
    EXPECT_NEAR(scopeWorld.y, 0.2F, kEpsilon);
}

TEST_F(BoneAttachmentPassTest, LeavesAnAttachmentAloneWhenItCannotResolveAndSaysSo) {
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const Entity skeleton = MakeSkeleton(character);

    // Four unbound reasons, each of which keeps the authored transform: a bone the skeleton does not
    // have, an attachment switched off, a skeleton switched off, and a skeleton reference that names
    // no live entity at all.
    BoneAttachment3DComponent missingBone;
    missingBone.boneName = "tail";
    const Entity tail = MakeAttachment(character, skeleton, missingBone);
    BoneAttachment3DComponent switchedOff;
    switchedOff.boneName = "hand";
    switchedOff.enabled = false;
    const Entity off = MakeAttachment(character, skeleton, switchedOff);
    BoneAttachment3DComponent onDisabledSkeleton;
    onDisabledSkeleton.boneName = "hand";
    const Entity orphan = MakeAttachment(character, skeleton, onDisabledSkeleton);
    BoneAttachment3DComponent dangling;
    dangling.boneName = "hand";
    const Entity danglingEntity = MakeAttachment(character, kInvalidEntity, dangling);

    entityManager.GetComponent<Skeleton3DComponent>(skeleton).enabled = false;
    const BoneAttachmentPassReport report = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 4U);
    EXPECT_EQ(report.bound, 0U);
    EXPECT_EQ(report.unbound, 4U);

    for (const Entity entity : {tail, off, orphan, danglingEntity}) {
        const BoneAttachment3DComponent& component =
            entityManager.GetComponent<BoneAttachment3DComponent>(entity);
        EXPECT_FALSE(component.bound);
        EXPECT_EQ(component.resolvedBoneIndex, kInvalidSkeletonBoneIndex);
        const Math::Vector3 position = entityManager.GetComponent<TransformComponent>(entity).localPosition;
        EXPECT_NEAR(position.x, 0.0F, kEpsilon) << "an unresolved attachment keeps what it was authored with";
        EXPECT_NEAR(position.y, 0.0F, kEpsilon);
        EXPECT_NEAR(position.z, 0.0F, kEpsilon);
    }
}

TEST_F(BoneAttachmentPassTest, LetsABoneIndexBeatTheNameWhenItNamesARealBone) {
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const Entity skeleton = MakeSkeleton(character);
    BoneAttachment3DComponent component;
    component.boneName = "hand";
    component.boneIndex = 0U; // the root, which wins over the name by the pair's rule
    const Entity entity = MakeAttachment(character, skeleton, component);

    static_cast<void>(SyncBoneAttachment3DObjects(entityManager, sceneGraph));
    EXPECT_EQ(entityManager.GetComponent<BoneAttachment3DComponent>(entity).resolvedBoneIndex, 0U);
    sceneGraph.Update(entityManager);
    const Math::Vector3 world = entityManager.GetComponent<WorldTransformComponent>(entity).worldPosition;
    EXPECT_NEAR(world.x, 0.0F, kEpsilon) << "the root bone is at the skeleton, not half a metre along it";

    // An index that names nothing falls back to the name - that is what makes a re-exported rig with
    // its bones in a different order keep working when the attachment carries both.
    // Read back what is on the entity: the reference lives there, and a locally built component that
    // never had it set would clear the skeleton rather than re-point the bone.
    BoneAttachment3DComponent updated =
        entityManager.GetComponent<BoneAttachment3DComponent>(entity);
    updated.boneIndex = 9U;
    updated.boneName = "hand";
    entityManager.GetComponent<BoneAttachment3DComponent>(entity) = updated;
    static_cast<void>(SyncBoneAttachment3DObjects(entityManager, sceneGraph));
    EXPECT_EQ(entityManager.GetComponent<BoneAttachment3DComponent>(entity).resolvedBoneIndex, 1U);
}

TEST_F(BoneAttachmentPassTest, SkipsAttachmentsThatDoNotTick) {
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const Entity skeleton = MakeSkeleton(character);
    BoneAttachment3DComponent component;
    component.boneName = "hand";
    const Entity entity = MakeAttachment(character, skeleton, component);
    ProcessComponent process;
    process.mode = TickMode::Never;
    entityManager.AddComponent<ProcessComponent>(entity, process);
    sceneGraph.Update(entityManager);

    const BoneAttachmentPassReport report = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 0U) << "an entity that does not run is not looked at, like every other system";
    EXPECT_FALSE(entityManager.GetComponent<BoneAttachment3DComponent>(entity).bound);

    // Switching it back on resolves it on the very next pass, with no other state to reset.
    entityManager.GetComponent<ProcessComponent>(entity).mode = TickMode::Running;
    sceneGraph.Update(entityManager);
    const BoneAttachmentPassReport again = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(again.considered, 1U);
    EXPECT_EQ(again.bound, 1U);
}

TEST_F(BoneAttachmentPassTest, DoesNothingInASceneWithNoAttachments) {
    const Entity character = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, character, TransformComponent{});
    const BoneAttachmentPassReport report = SyncBoneAttachment3DObjects(entityManager, sceneGraph);
    EXPECT_EQ(report.considered, 0U);
    EXPECT_EQ(report.bound, 0U);
    EXPECT_EQ(report.unbound, 0U);
}

} // namespace
} // namespace UVE::Scene::Tests
