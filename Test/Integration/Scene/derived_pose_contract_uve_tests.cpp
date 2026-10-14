// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/scene/scene_graph_uve.h"
#include <limits>
#include <type_traits>
#include <utility>
#include <gtest/gtest.h>
#include "uve/component/component_access_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_enabled_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/heap_allocator_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/world/world_uve.h"

namespace UVE::Scene::Tests {
namespace {
using WorldPoseUVE = WorldTransformComponentUVE;
static_assert(std::is_same_v<decltype(std::declval<IEntityManagerUVE &>().GetComponentUVE<WorldPoseUVE>({})),
                             const WorldPoseUVE &>);
static_assert(std::is_same_v<decltype(std::declval<IEntityManagerUVE &>().AddComponentUVE<WorldPoseUVE>({})),
                             const WorldPoseUVE &>);
static_assert(
    std::is_same_v<decltype(std::declval<EntityViewUVE<WorldPoseUVE>::EntryUVE &>().GetUVE<WorldPoseUVE>()),
                   const WorldPoseUVE &>);
static_assert(!std::is_default_constructible_v<SceneGraphWriteAccessUVE>);
static_assert(std::is_trivially_copyable_v<WorldPoseUVE>);

class DerivedPoseContractUVETest : public ::testing::Test {
  protected:
    Memory::HeapAllocatorUVE allocator;
    Events::EventSystemUVE events;
    EntityManagerUVE entities{allocator, events};
    SceneGraphUVE graph;
    EntityUVE MakeUVE(float x = 0.0F) {
        const auto entity = entities.CreateEntityUVE();
        TransformComponentUVE local;
        local.localPosition.x = x;
        graph.AttachTransformUVE(entities, entity, local);
        entities.AddComponentUVE<PhysicsInterpolationComponentUVE>(entity);
        return entity;
    }
    void MoveUVE(EntityUVE entity, float x) {
        auto local = entities.GetComponentUVE<TransformComponentUVE>(entity);
        local.localPosition.x = x;
        graph.SetLocalTransformUVE(entities, entity, local);
        graph.UpdateUVE(entities);
    }
    float DrawXUVE(EntityUVE entity, float alpha = 0.5F) {
        Math::Vector3UVE position, scale;
        Math::QuaternionUVE rotation;
        EXPECT_TRUE(
            TryGetInterpolatedPoseUVE(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity),
                                      alpha, position, rotation, scale));
        return position.x;
    }
};
TEST_F(DerivedPoseContractUVETest, LiveQueriesAndViewsAreReadOnlyButAllowInvalidation) {
    const auto entity = MakeUVE(5.0F);
    graph.UpdateUVE(entities);
    entities.ForEachUVE<WorldPoseUVE>([](auto, auto &pose) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(pose)>>);
        EXPECT_FLOAT_EQ(pose.worldPosition.x, 5.0F);
        pose.dirty = true;
    });
    for (auto entry : entities.ViewUVE<WorldPoseUVE>())
        EXPECT_TRUE(entry.GetUVE<WorldPoseUVE>().dirty);
    EXPECT_EQ(entities.GetComponentPointerUVE(entity, typeid(WorldPoseUVE)), nullptr);
    EXPECT_EQ(std::as_const(entities).GetComponentPointerUVE(entity, typeid(WorldPoseUVE)),
              &entities.GetComponentUVE<WorldPoseUVE>(entity));
    graph.UpdateUVE(entities);
    EXPECT_FALSE(entities.GetComponentUVE<WorldPoseUVE>(entity).dirty);
}
TEST_F(DerivedPoseContractUVETest, ErasedAuthoringCannotCreateAMutableDerivedCache) {
    const auto entity = entities.CreateEntityUVE();
    EXPECT_EQ(
        entities.AddComponentUVE(entity, typeid(WorldPoseUVE), MakeComponentTypeInfoUVE<WorldPoseUVE>()),
        nullptr);
    EXPECT_FALSE(entities.HasComponentUVE<WorldPoseUVE>(entity));
}
TEST_F(DerivedPoseContractUVETest, TeleportSnapsItsSubtreeAndPreservesUnrelatedHistoryAndIndexBorrows) {
    const auto parent = MakeUVE(), child = MakeUVE(2), other = MakeUVE(10);
    graph.SetParentUVE(entities, child, parent);
    graph.UpdateUVE(entities);
    MoveUVE(parent, 2);
    MoveUVE(other, 12);
    EXPECT_FLOAT_EQ(DrawXUVE(child), 3.0F);
    graph.RebuildChildrenIndexUVE(entities);
    const auto children = graph.GetIndexedChildrenUVE(parent);
    const auto *previousStorage = children.data();
    auto target = entities.GetComponentUVE<TransformComponentUVE>(parent);
    target.localPosition.x = 100.0F;
    ASSERT_TRUE(graph.TryTeleportLocalTransformUVE(entities, parent, target));
    EXPECT_FALSE(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(child).hasPreviousPose);
    EXPECT_EQ(graph.GetIndexedChildrenUVE(parent).data(), previousStorage);
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(parent, 0.0F), 100.0F);
    EXPECT_FLOAT_EQ(DrawXUVE(child, 0.25F), 102.0F);
    EXPECT_FLOAT_EQ(DrawXUVE(other), 11.0F);
}
TEST_F(DerivedPoseContractUVETest, InvalidTeleportLeavesAuthoredPoseAndHistoryUntouched) {
    const auto entity = MakeUVE();
    graph.UpdateUVE(entities);
    MoveUVE(entity, 4);
    const auto before = entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    auto invalid = entities.GetComponentUVE<TransformComponentUVE>(entity);
    invalid.localPosition.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(graph.TryTeleportLocalTransformUVE(entities, entity, invalid));
    EXPECT_FALSE(graph.TryTeleportLocalTransformUVE(entities, kInvalidEntityUVE, {}));
    EXPECT_FALSE(graph.ResetInterpolationSubtreeUVE(entities, kInvalidEntityUVE));
    EXPECT_FLOAT_EQ(entities.GetComponentUVE<TransformComponentUVE>(entity).localPosition.x, 4.0F);
    EXPECT_EQ(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity).previousPosition,
              before.previousPosition);
    EXPECT_TRUE(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity).hasPreviousPose);
}
TEST_F(DerivedPoseContractUVETest, ReparentSnapsAndAnIdempotentSetterDoesNotCollapseHistory) {
    const auto a = MakeUVE(), b = MakeUVE(100), child = MakeUVE(1);
    graph.SetParentUVE(entities, child, a);
    graph.UpdateUVE(entities);
    MoveUVE(a, 2);
    graph.SetParentUVE(entities, child, b);
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(child, 0.0F), 101.0F);
    MoveUVE(child, 3);
    EXPECT_FLOAT_EQ(DrawXUVE(child), 102.0F);
    graph.SetParentUVE(entities, child, b);
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(child), 102.0F);
}
TEST_F(DerivedPoseContractUVETest, DisabledTagKeepsWorldUpdatesButDropsOnlyItsOwnHistory) {
    const auto parent = MakeUVE(), child = MakeUVE(1);
    graph.SetParentUVE(entities, child, parent);
    graph.UpdateUVE(entities);
    MoveUVE(parent, 2);
    SetEntityEnabledUVE(entities, parent, false);
    MoveUVE(parent, 10);
    EXPECT_FALSE(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(parent).hasPreviousPose);
    EXPECT_TRUE(entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(child).hasPreviousPose);
    EXPECT_FLOAT_EQ(entities.GetComponentUVE<WorldPoseUVE>(parent).worldPosition.x, 10.0F);
    SetEntityEnabledUVE(entities, parent, true);
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(parent, 0.0F), 10.0F);
}
TEST_F(DerivedPoseContractUVETest, ResetAlsoSeedsAnUnchangedPoseUnderAPureObject) {
    const auto root = entities.CreateEntityUVE();
    entities.AddComponentUVE<HierarchyComponentUVE>(root);
    const auto child = MakeUVE();
    graph.SetParentUVE(entities, child, root);
    graph.UpdateUVE(entities);
    MoveUVE(child, 8);
    ASSERT_TRUE(graph.ResetInterpolationSubtreeUVE(entities, root));
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(child, 0.0F), 8.0F);
}
TEST_F(DerivedPoseContractUVETest, SwitchingFromExactToBlendedDoesNotReintroduceAnOldPose) {
    const auto entity = MakeUVE();
    graph.UpdateUVE(entities);
    MoveUVE(entity, 4);
    entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity).mode = PoseSmoothingUVE::Exact;
    MoveUVE(entity, 8);
    entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity).mode = PoseSmoothingUVE::Blended;
    graph.UpdateUVE(entities);
    EXPECT_FLOAT_EQ(DrawXUVE(entity, 0.0F), 8.0F);
}
TEST_F(DerivedPoseContractUVETest, RuntimeHistoryNeverChangesSerializedSceneBytes) {
    const auto entity = MakeUVE(2);
    graph.UpdateUVE(entities);
    SceneSerializerUVE serializer;
    const auto before = serializer.CaptureUVE(entities, {entity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(before.has_value());
    auto &interpolation = entities.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    interpolation.previousPosition.x = -50.0F;
    interpolation.currentScale.z = std::numeric_limits<float>::quiet_NaN();
    interpolation.hasPreviousPose = true;
    const auto after = serializer.CaptureUVE(entities, {entity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(before->bytes, after->bytes);
}
TEST_F(DerivedPoseContractUVETest, SceneResumeSeedsAfterThePausedInterval) {
    World::WorldUVE world{allocator, events};
    const auto handle = world.GetPrimarySceneHandleUVE();
    auto *scene = world.GetSceneUVE(handle);
    auto &manager = scene->GetEntityManagerUVE();
    const auto entity = manager.CreateEntityUVE();
    scene->GetSceneGraphUVE().AttachTransformUVE(manager, entity, {});
    manager.AddComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    ASSERT_EQ(world.TryTickUVE(0.0F), World::WorldResultUVE::Succeeded);
    ASSERT_EQ(world.SetSceneEnabledUVE(handle, false), World::WorldResultUVE::Succeeded);
    auto local = manager.GetComponentUVE<TransformComponentUVE>(entity);
    local.localPosition.x = 100.0F;
    scene->GetSceneGraphUVE().SetLocalTransformUVE(manager, entity, local);
    ASSERT_EQ(world.SetSceneEnabledUVE(handle, true), World::WorldResultUVE::Succeeded);
    ASSERT_EQ(world.TryTickUVE(0.0F), World::WorldResultUVE::Succeeded);
    const auto &pose = manager.GetComponentUVE<PhysicsInterpolationComponentUVE>(entity);
    EXPECT_FLOAT_EQ(pose.previousPosition.x, 100.0F);
    EXPECT_FLOAT_EQ(pose.currentPosition.x, 100.0F);
}
TEST(PhysicsInterpolationBoundaryUVETest, RejectsNonFinitePosesAndUnknownModeWithoutPublishing) {
    PhysicsInterpolationComponentUVE pose;
    pose.hasPreviousPose = true;
    Math::Vector3UVE position{7, 8, 9}, scale{2, 3, 4};
    Math::QuaternionUVE rotation;
    for (int variant = 0; variant < 3; ++variant) {
        auto bad = pose;
        if (variant == 0)
            bad.currentPosition.x = std::numeric_limits<float>::infinity();
        if (variant == 1)
            bad.previousScale.z = std::numeric_limits<float>::quiet_NaN();
        if (variant == 2)
            bad.mode = static_cast<PoseSmoothingUVE>(255);
        EXPECT_FALSE(TryGetInterpolatedPoseUVE(bad, 0.5F, position, rotation, scale));
        EXPECT_EQ(position, (Math::Vector3UVE{7, 8, 9}));
        EXPECT_EQ(scale, (Math::Vector3UVE{2, 3, 4}));
        EXPECT_EQ(rotation, Math::QuaternionUVE{});
    }
}
TEST(PhysicsInterpolationBoundaryUVETest, LargeFiniteEndpointsDoNotOverflowTheBlend) {
    PhysicsInterpolationComponentUVE pose;
    pose.hasPreviousPose = true;
    const auto large = std::numeric_limits<float>::max();
    pose.previousPosition = {-large, -large, -large};
    pose.currentPosition = {large, large, large};
    pose.previousScale = {-large, -large, -large};
    pose.currentScale = {large, large, large};
    Math::Vector3UVE position, scale;
    Math::QuaternionUVE rotation;
    ASSERT_TRUE(TryGetInterpolatedPoseUVE(pose, 0.5F, position, rotation, scale));
    EXPECT_EQ(position, Math::Vector3UVE{});
    EXPECT_EQ(scale, Math::Vector3UVE{});
}
} // namespace
} // namespace UVE::Scene::Tests
