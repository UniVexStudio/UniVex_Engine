// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "scene_graph.h"
#include <limits>
#include <type_traits>
#include <utility>
#include <gtest/gtest.h>
#include "component_access.h"
#include "hierarchy_component.h"
#include "physics_interpolation_component.h"
#include "world_transform_component.h"
#include "entity_enabled.h"
#include "entity_manager.h"
#include "event_system.h"
#include "heap_allocator.h"
#include "scene_serializer.h"
#include "world.h"

namespace UVE::Scene::Tests {
namespace {
using WorldPose = WorldTransformComponent;
static_assert(std::is_same_v<decltype(std::declval<IEntityManager &>().GetComponent<WorldPose>({})),
                             const WorldPose &>);
static_assert(std::is_same_v<decltype(std::declval<IEntityManager &>().AddComponent<WorldPose>({})),
                             const WorldPose &>);
static_assert(
    std::is_same_v<decltype(std::declval<EntityView<WorldPose>::Entry &>().Get<WorldPose>()),
                   const WorldPose &>);
static_assert(!std::is_default_constructible_v<SceneGraphWriteAccess>);
static_assert(std::is_trivially_copyable_v<WorldPose>);

class DerivedPoseContractTest : public ::testing::Test {
  protected:
    Memory::HeapAllocator allocator;
    Events::EventSystem events;
    EntityManager entities{allocator, events};
    SceneGraph graph;
    Entity Make(float x = 0.0F) {
        const auto entity = entities.CreateEntity();
        TransformComponent local;
        local.localPosition.x = x;
        graph.AttachTransform(entities, entity, local);
        entities.AddComponent<PhysicsInterpolationComponent>(entity);
        return entity;
    }
    void Move(Entity entity, float x) {
        auto local = entities.GetComponent<TransformComponent>(entity);
        local.localPosition.x = x;
        graph.SetLocalTransform(entities, entity, local);
        graph.Update(entities);
    }
    float DrawX(Entity entity, float alpha = 0.5F) {
        Math::Vector3 position, scale;
        Math::Quaternion rotation;
        EXPECT_TRUE(
            TryGetInterpolatedPose(entities.GetComponent<PhysicsInterpolationComponent>(entity),
                                      alpha, position, rotation, scale));
        return position.x;
    }
};
TEST_F(DerivedPoseContractTest, LiveQueriesAndViewsAreReadOnlyButAllowInvalidation) {
    const auto entity = Make(5.0F);
    graph.Update(entities);
    entities.ForEach<WorldPose>([](auto, auto &pose) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(pose)>>);
        EXPECT_FLOAT_EQ(pose.worldPosition.x, 5.0F);
        pose.dirty = true;
    });
    for (auto entry : entities.View<WorldPose>())
        EXPECT_TRUE(entry.Get<WorldPose>().dirty);
    EXPECT_EQ(entities.GetComponentPointer(entity, typeid(WorldPose)), nullptr);
    EXPECT_EQ(std::as_const(entities).GetComponentPointer(entity, typeid(WorldPose)),
              &entities.GetComponent<WorldPose>(entity));
    graph.Update(entities);
    EXPECT_FALSE(entities.GetComponent<WorldPose>(entity).dirty);
}
TEST_F(DerivedPoseContractTest, ErasedAuthoringCannotCreateAMutableDerivedCache) {
    const auto entity = entities.CreateEntity();
    EXPECT_EQ(
        entities.AddComponent(entity, typeid(WorldPose), MakeComponentTypeInfo<WorldPose>()),
        nullptr);
    EXPECT_FALSE(entities.HasComponent<WorldPose>(entity));
}
TEST_F(DerivedPoseContractTest, TeleportSnapsItsSubtreeAndPreservesUnrelatedHistoryAndIndexBorrows) {
    const auto parent = Make(), child = Make(2), other = Make(10);
    graph.SetParent(entities, child, parent);
    graph.Update(entities);
    Move(parent, 2);
    Move(other, 12);
    EXPECT_FLOAT_EQ(DrawX(child), 3.0F);
    graph.RebuildChildrenIndex(entities);
    const auto children = graph.GetIndexedChildren(parent);
    const auto *previousStorage = children.data();
    auto target = entities.GetComponent<TransformComponent>(parent);
    target.localPosition.x = 100.0F;
    ASSERT_TRUE(graph.TryTeleportLocalTransform(entities, parent, target));
    EXPECT_FALSE(entities.GetComponent<PhysicsInterpolationComponent>(child).hasPreviousPose);
    EXPECT_EQ(graph.GetIndexedChildren(parent).data(), previousStorage);
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(parent, 0.0F), 100.0F);
    EXPECT_FLOAT_EQ(DrawX(child, 0.25F), 102.0F);
    EXPECT_FLOAT_EQ(DrawX(other), 11.0F);
}
TEST_F(DerivedPoseContractTest, InvalidTeleportLeavesAuthoredPoseAndHistoryUntouched) {
    const auto entity = Make();
    graph.Update(entities);
    Move(entity, 4);
    const auto before = entities.GetComponent<PhysicsInterpolationComponent>(entity);
    auto invalid = entities.GetComponent<TransformComponent>(entity);
    invalid.localPosition.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(graph.TryTeleportLocalTransform(entities, entity, invalid));
    EXPECT_FALSE(graph.TryTeleportLocalTransform(entities, kInvalidEntity, {}));
    EXPECT_FALSE(graph.ResetInterpolationSubtree(entities, kInvalidEntity));
    EXPECT_FLOAT_EQ(entities.GetComponent<TransformComponent>(entity).localPosition.x, 4.0F);
    EXPECT_EQ(entities.GetComponent<PhysicsInterpolationComponent>(entity).previousPosition,
              before.previousPosition);
    EXPECT_TRUE(entities.GetComponent<PhysicsInterpolationComponent>(entity).hasPreviousPose);
}
TEST_F(DerivedPoseContractTest, ReparentSnapsAndAnIdempotentSetterDoesNotCollapseHistory) {
    const auto a = Make(), b = Make(100), child = Make(1);
    graph.SetParent(entities, child, a);
    graph.Update(entities);
    Move(a, 2);
    graph.SetParent(entities, child, b);
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(child, 0.0F), 101.0F);
    Move(child, 3);
    EXPECT_FLOAT_EQ(DrawX(child), 102.0F);
    graph.SetParent(entities, child, b);
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(child), 102.0F);
}
TEST_F(DerivedPoseContractTest, DisabledTagKeepsWorldUpdatesButDropsOnlyItsOwnHistory) {
    const auto parent = Make(), child = Make(1);
    graph.SetParent(entities, child, parent);
    graph.Update(entities);
    Move(parent, 2);
    SetEntityEnabled(entities, parent, false);
    Move(parent, 10);
    EXPECT_FALSE(entities.GetComponent<PhysicsInterpolationComponent>(parent).hasPreviousPose);
    EXPECT_TRUE(entities.GetComponent<PhysicsInterpolationComponent>(child).hasPreviousPose);
    EXPECT_FLOAT_EQ(entities.GetComponent<WorldPose>(parent).worldPosition.x, 10.0F);
    SetEntityEnabled(entities, parent, true);
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(parent, 0.0F), 10.0F);
}
TEST_F(DerivedPoseContractTest, ResetAlsoSeedsAnUnchangedPoseUnderAPureObject) {
    const auto root = entities.CreateEntity();
    entities.AddComponent<HierarchyComponent>(root);
    const auto child = Make();
    graph.SetParent(entities, child, root);
    graph.Update(entities);
    Move(child, 8);
    ASSERT_TRUE(graph.ResetInterpolationSubtree(entities, root));
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(child, 0.0F), 8.0F);
}
TEST_F(DerivedPoseContractTest, SwitchingFromExactToBlendedDoesNotReintroduceAnOldPose) {
    const auto entity = Make();
    graph.Update(entities);
    Move(entity, 4);
    entities.GetComponent<PhysicsInterpolationComponent>(entity).mode = PoseSmoothing::Exact;
    Move(entity, 8);
    entities.GetComponent<PhysicsInterpolationComponent>(entity).mode = PoseSmoothing::Blended;
    graph.Update(entities);
    EXPECT_FLOAT_EQ(DrawX(entity, 0.0F), 8.0F);
}
TEST_F(DerivedPoseContractTest, RuntimeHistoryNeverChangesSerializedSceneBytes) {
    const auto entity = Make(2);
    graph.Update(entities);
    SceneSerializer serializer;
    const auto before = serializer.Capture(entities, {entity}, SceneAssetType::Scene);
    ASSERT_TRUE(before.has_value());
    auto &interpolation = entities.GetComponent<PhysicsInterpolationComponent>(entity);
    interpolation.previousPosition.x = -50.0F;
    interpolation.currentScale.z = std::numeric_limits<float>::quiet_NaN();
    interpolation.hasPreviousPose = true;
    const auto after = serializer.Capture(entities, {entity}, SceneAssetType::Scene);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(before->bytes, after->bytes);
}
TEST_F(DerivedPoseContractTest, SceneResumeSeedsAfterThePausedInterval) {
    World::World world{allocator, events};
    const auto handle = world.GetPrimarySceneHandle();
    auto *scene = world.GetScene(handle);
    auto &manager = scene->GetEntityManager();
    const auto entity = manager.CreateEntity();
    scene->GetSceneGraph().AttachTransform(manager, entity, {});
    manager.AddComponent<PhysicsInterpolationComponent>(entity);
    ASSERT_EQ(world.TryTick(0.0F), World::WorldResult::Succeeded);
    ASSERT_EQ(world.SetSceneEnabled(handle, false), World::WorldResult::Succeeded);
    auto local = manager.GetComponent<TransformComponent>(entity);
    local.localPosition.x = 100.0F;
    scene->GetSceneGraph().SetLocalTransform(manager, entity, local);
    ASSERT_EQ(world.SetSceneEnabled(handle, true), World::WorldResult::Succeeded);
    ASSERT_EQ(world.TryTick(0.0F), World::WorldResult::Succeeded);
    const auto &pose = manager.GetComponent<PhysicsInterpolationComponent>(entity);
    EXPECT_FLOAT_EQ(pose.previousPosition.x, 100.0F);
    EXPECT_FLOAT_EQ(pose.currentPosition.x, 100.0F);
}
TEST(PhysicsInterpolationBoundaryTest, RejectsNonFinitePosesAndUnknownModeWithoutPublishing) {
    PhysicsInterpolationComponent pose;
    pose.hasPreviousPose = true;
    Math::Vector3 position{7, 8, 9}, scale{2, 3, 4};
    Math::Quaternion rotation;
    for (int variant = 0; variant < 3; ++variant) {
        auto bad = pose;
        if (variant == 0)
            bad.currentPosition.x = std::numeric_limits<float>::infinity();
        if (variant == 1)
            bad.previousScale.z = std::numeric_limits<float>::quiet_NaN();
        if (variant == 2)
            bad.mode = static_cast<PoseSmoothing>(255);
        EXPECT_FALSE(TryGetInterpolatedPose(bad, 0.5F, position, rotation, scale));
        EXPECT_EQ(position, (Math::Vector3{7, 8, 9}));
        EXPECT_EQ(scale, (Math::Vector3{2, 3, 4}));
        EXPECT_EQ(rotation, Math::Quaternion{});
    }
}
TEST(PhysicsInterpolationBoundaryTest, LargeFiniteEndpointsDoNotOverflowTheBlend) {
    PhysicsInterpolationComponent pose;
    pose.hasPreviousPose = true;
    const auto large = std::numeric_limits<float>::max();
    pose.previousPosition = {-large, -large, -large};
    pose.currentPosition = {large, large, large};
    pose.previousScale = {-large, -large, -large};
    pose.currentScale = {large, large, large};
    Math::Vector3 position, scale;
    Math::Quaternion rotation;
    ASSERT_TRUE(TryGetInterpolatedPose(pose, 0.5F, position, rotation, scale));
    EXPECT_EQ(position, Math::Vector3{});
    EXPECT_EQ(scale, Math::Vector3{});
}
} // namespace
} // namespace UVE::Scene::Tests
