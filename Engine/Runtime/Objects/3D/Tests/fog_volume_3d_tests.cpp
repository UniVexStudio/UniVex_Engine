// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "fog_volume_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"

#include <array>
#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "editor_internal_entity_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class FogVolume3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity PlaceFog(const FogVolume3DComponent& fog, const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        FogVolume3DObjectDefinition definition{};
        definition.fog = fog;
        ApplyFogVolume3DObjectDefinition(entityManager, entity, definition);
        entityManager.GetComponent<FogVolume3DComponent>(entity) = fog;
        sceneGraph.Update(entityManager);
        return entity;
    }

    FogVolume3DFrame FrameOf(const Entity entity) {
        FogVolume3DFrame frame{};
        const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(entity);
        EXPECT_TRUE(TryMakeFogVolume3DFrame(entityManager.GetComponent<FogVolume3DComponent>(entity),
                                               world.worldPosition, world.worldRotation, world.worldScale, frame));
        return frame;
    }
};

TEST_F(FogVolume3DTest, BoxCentreHasAuthoredDensityAndOutsideIsEmpty) {
    FogVolume3DComponent fog{};
    fog.shape = FogVolumeShape::Box;
    fog.size = Math::Vector3{2.0F, 2.0F, 2.0F};
    fog.density = 0.8F;
    fog.edgeFade = 0.0F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DFrame frame = FrameOf(entity);
    EXPECT_NEAR(SampleFogVolume3DDensity(frame, Math::Vector3{0.0F, 0.0F, 0.0F}), 0.8F, 1.0e-5F);
    EXPECT_NEAR(SampleFogVolume3DDensity(frame, Math::Vector3{0.99F, 0.0F, 0.0F}), 0.8F, 1.0e-5F);
    EXPECT_EQ(SampleFogVolume3DDensity(frame, Math::Vector3{1.01F, 0.0F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DTest, EdgeFadeThinsTheSkinAndLeavesTheCentre) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 1.0F;
    fog.density = 1.0F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DFrame frame = FrameOf(entity);
    const float centre = SampleFogVolume3DDensity(frame, Math::Vector3{0.0F, 0.0F, 0.0F});
    const float halfway = SampleFogVolume3DDensity(frame, Math::Vector3{0.5F, 0.0F, 0.0F});
    const float skin = SampleFogVolume3DDensity(frame, Math::Vector3{0.99F, 0.0F, 0.0F});
    EXPECT_NEAR(centre, 1.0F, 1.0e-4F);
    EXPECT_GT(centre, halfway);
    EXPECT_GT(halfway, skin);
    EXPECT_LT(skin, 0.05F);
}

TEST_F(FogVolume3DTest, NegativeDensityCarves) {
    FogVolume3DComponent fog{};
    fog.density = -2.0F;
    fog.edgeFade = 0.0F;
    const Entity entity = PlaceFog(fog);
    EXPECT_NEAR(SampleFogVolume3DDensity(FrameOf(entity), Math::Vector3{}), -2.0F, 1.0e-5F);
}

TEST_F(FogVolume3DTest, HeightFalloffThinsTowardTheTop) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    fog.heightFalloff = 1.0F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DFrame frame = FrameOf(entity);
    const float bottom = SampleFogVolume3DDensity(frame, Math::Vector3{0.0F, -0.9F, 0.0F});
    const float top = SampleFogVolume3DDensity(frame, Math::Vector3{0.0F, 0.9F, 0.0F});
    EXPECT_GT(bottom, top);
    EXPECT_GT(bottom, 0.5F);
}

TEST_F(FogVolume3DTest, EllipsoidRejectsTheBoxCorner) {
    FogVolume3DComponent fog{};
    fog.shape = FogVolumeShape::Ellipsoid;
    fog.edgeFade = 0.0F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DFrame frame = FrameOf(entity);
    EXPECT_GT(SampleFogVolume3DDensity(frame, Math::Vector3{}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensity(frame, Math::Vector3{0.95F, 0.95F, 0.95F}), 0.0F);
}

TEST_F(FogVolume3DTest, ConeHoldsTheBaseAndDropsTheTipSides) {
    FogVolume3DComponent fog{};
    fog.shape = FogVolumeShape::Cone;
    fog.edgeFade = 0.0F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DFrame frame = FrameOf(entity);
    EXPECT_GT(SampleFogVolume3DDensity(frame, Math::Vector3{0.0F, -0.5F, 0.0F}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensity(frame, Math::Vector3{0.8F, 0.8F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DTest, WorldFillsEverywhere) {
    FogVolume3DComponent fog{};
    fog.shape = FogVolumeShape::World;
    fog.density = 0.25F;
    const Entity entity = PlaceFog(fog);
    EXPECT_NEAR(SampleFogVolume3DDensity(FrameOf(entity), Math::Vector3{80.0F, 12.0F, -40.0F}), 0.25F, 1.0e-5F);
}

TEST_F(FogVolume3DTest, ScaleWidensTheVolumeInWorld) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    TransformComponent transform{};
    transform.localScale = Math::Vector3{2.0F, 1.0F, 1.0F};
    const Entity entity = PlaceFog(fog, transform);
    const FogVolume3DFrame frame = FrameOf(entity);
    EXPECT_GT(SampleFogVolume3DDensity(frame, Math::Vector3{1.9F, 0.0F, 0.0F}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensity(frame, Math::Vector3{2.1F, 0.0F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DTest, RayThroughABoxMatchesDensityTimesWidth) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    fog.density = 0.5F;
    const Entity entity = PlaceFog(fog);
    const FogVolume3DRaySample sample =
        IntegrateFogVolume3DRay(FrameOf(entity), Math::Vector3{-4.0F, 0.0F, 0.0F},
                                   Math::Vector3{1.0F, 0.0F, 0.0F}, 8.0F);
    EXPECT_NEAR(sample.opticalDepth, 1.0F, 0.08F);
    EXPECT_GT(sample.scatterWeight, 0.0F);
}

TEST_F(FogVolume3DTest, RayThatMissesIsEmpty) {
    FogVolume3DComponent fog{};
    const Entity entity = PlaceFog(fog);
    const FogVolume3DRaySample sample =
        IntegrateFogVolume3DRay(FrameOf(entity), Math::Vector3{0.0F, 4.0F, 0.0F},
                                   Math::Vector3{1.0F, 0.0F, 0.0F}, 8.0F);
    EXPECT_EQ(sample.opticalDepth, 0.0F);
    EXPECT_EQ(sample.scatterWeight, 0.0F);
}

TEST_F(FogVolume3DTest, CollectSkipsAPartitionHiddenVolume) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    const Entity volume = PlaceFog(fog);
    const Entity partition = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, partition, TransformComponent{});
    entityManager.AddComponent<WorldPartition3DComponent>(partition);
    entityManager.AddComponent<WorldPartition3DMembershipComponent>(
        volume, WorldPartition3DMembershipComponent{partition, false});
    sceneGraph.Update(entityManager);

    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    EXPECT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), 0U);
}

TEST_F(FogVolume3DTest, CollectSkipsAnOccludedVolume) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    TransformComponent fogTransform{};
    fogTransform.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    PlaceFog(fog, fogTransform);

    const Entity wall = entityManager.CreateEntity();
    TransformComponent wallTransform{};
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{4.0F, 4.0F, 2.0F};
    entityManager.AddComponent<Occluder3DComponent>(wall, wallOccluder);
    sceneGraph.Update(entityManager);

    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    EXPECT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), 0U);
}

TEST_F(FogVolume3DTest, CollectSkipsARegionHiddenVolume) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    const Entity volume = PlaceFog(fog);
    const Entity region = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, region, TransformComponent{});
    entityManager.AddComponent<VisibilityRegion3DComponent>(region);
    entityManager.AddComponent<VisibilityRegion3DMembershipComponent>(
        volume, VisibilityRegion3DMembershipComponent{region, false});
    sceneGraph.Update(entityManager);

    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    EXPECT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), 0U);
}

TEST_F(FogVolume3DTest, CollectKeepsTheNearestAndDropsHidden) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    PlaceFog(fog);
    TransformComponent farTransform{};
    farTransform.localPosition = Math::Vector3{50.0F, 0.0F, 0.0F};
    PlaceFog(fog, farTransform);

    const Entity hidden = PlaceFog(fog);
    entityManager.GetComponent<VisibilityComponent>(hidden).visible = false;
    sceneGraph.Update(entityManager);

    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    const std::size_t count = CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames);
    EXPECT_EQ(count, 2U);
    EXPECT_NEAR(frames[0].worldPosition.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(frames[1].worldPosition.x, 50.0F, 1.0e-4F);
}

TEST_F(FogVolume3DTest, WorldVolumesWinTheBudgetOverFarBoxes) {
    FogVolume3DComponent world{};
    world.shape = FogVolumeShape::World;
    world.density = 0.1F;
    TransformComponent worldTransform{};
    worldTransform.localPosition = Math::Vector3{100.0F, 0.0F, 0.0F};
    PlaceFog(world, worldTransform);

    FogVolume3DComponent box{};
    PlaceFog(box);

    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    const std::size_t count = CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames);
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(frames[0].shape, FogVolumeShape::World);
}

TEST_F(FogVolume3DTest, ZeroDensityAndNoEmissionIsSkipped) {
    FogVolume3DComponent fog{};
    fog.density = 0.0F;
    PlaceFog(fog);
    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    EXPECT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), 0U);
}

TEST_F(FogVolume3DTest, EmissionOnlyVolumeStillCollects) {
    FogVolume3DComponent fog{};
    fog.density = 0.0F;
    fog.emission = Math::Vector3{1.0F, 0.2F, 0.0F};
    PlaceFog(fog);
    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    ASSERT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), 1U);
    EXPECT_NEAR(frames[0].emission.x, 1.0F, 1.0e-5F);
}

TEST_F(FogVolume3DTest, CollectCapsAtTheFrameBudget) {
    FogVolume3DComponent fog{};
    fog.edgeFade = 0.0F;
    for (std::uint32_t i = 0; i < kMaximumFogVolumesPerFrame + 2U; ++i) {
        TransformComponent transform{};
        transform.localPosition = Math::Vector3{static_cast<float>(i) * 3.0F, 0.0F, 0.0F};
        PlaceFog(fog, transform);
    }
    std::array<FogVolume3DFrame, kMaximumFogVolumesPerFrame> frames{};
    EXPECT_EQ(CollectFogVolume3DFrames(entityManager, Math::Vector3{}, frames), kMaximumFogVolumesPerFrame);
    EXPECT_NEAR(frames[0].worldPosition.x, 0.0F, 1.0e-4F);
}

TEST_F(FogVolume3DTest, DegenerateScaleFailsClosed) {
    FogVolume3DComponent fog{};
    FogVolume3DFrame frame{};
    EXPECT_FALSE(TryMakeFogVolume3DFrame(fog, Math::Vector3{}, Math::Quaternion{},
                                            Math::Vector3{0.0F, 1.0F, 1.0F}, frame));
}

TEST_F(FogVolume3DTest, GizmoSkipsEditorInternal) {
    FogVolume3DComponent fog{};
    const Entity hidden = PlaceFog(fog);
    entityManager.AddComponent<EditorInternalEntityComponent>(hidden);
    const Entity visible = PlaceFog(fog);
    std::vector<FogVolume3DGizmo> gizmos;
    CollectFogVolume3DGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_EQ(gizmos[0].origin.x, entityManager.GetComponent<WorldTransformComponent>(visible).worldPosition.x);
}

} // namespace
} // namespace UVE::Scene::Tests
