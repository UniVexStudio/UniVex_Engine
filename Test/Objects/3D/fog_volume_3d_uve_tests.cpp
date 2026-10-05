// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"

#include <array>
#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class FogVolume3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE PlaceFogUVE(const FogVolume3DComponentUVE& fog, const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        FogVolume3DObjectDefinitionUVE definition{};
        definition.fog = fog;
        ApplyFogVolume3DObjectDefinitionUVE(entityManager, entity, definition);
        entityManager.GetComponentUVE<FogVolume3DComponentUVE>(entity) = fog;
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    FogVolume3DFrameUVE FrameOfUVE(const EntityUVE entity) {
        FogVolume3DFrameUVE frame{};
        const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
        EXPECT_TRUE(TryMakeFogVolume3DFrameUVE(entityManager.GetComponentUVE<FogVolume3DComponentUVE>(entity),
                                               world.worldPosition, world.worldRotation, world.worldScale, frame));
        return frame;
    }
};

TEST_F(FogVolume3DUVETest, BoxCentreHasAuthoredDensityAndOutsideIsEmpty) {
    FogVolume3DComponentUVE fog{};
    fog.shape = FogVolumeShapeUVE::Box;
    fog.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    fog.density = 0.8F;
    fog.edgeFade = 0.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    EXPECT_NEAR(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.0F, 0.0F, 0.0F}), 0.8F, 1.0e-5F);
    EXPECT_NEAR(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.99F, 0.0F, 0.0F}), 0.8F, 1.0e-5F);
    EXPECT_EQ(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{1.01F, 0.0F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DUVETest, EdgeFadeThinsTheSkinAndLeavesTheCentre) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 1.0F;
    fog.density = 1.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    const float centre = SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const float halfway = SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.5F, 0.0F, 0.0F});
    const float skin = SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.99F, 0.0F, 0.0F});
    EXPECT_NEAR(centre, 1.0F, 1.0e-4F);
    EXPECT_GT(centre, halfway);
    EXPECT_GT(halfway, skin);
    EXPECT_LT(skin, 0.05F);
}

TEST_F(FogVolume3DUVETest, NegativeDensityCarves) {
    FogVolume3DComponentUVE fog{};
    fog.density = -2.0F;
    fog.edgeFade = 0.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    EXPECT_NEAR(SampleFogVolume3DDensityUVE(FrameOfUVE(entity), Math::Vector3UVE{}), -2.0F, 1.0e-5F);
}

TEST_F(FogVolume3DUVETest, HeightFalloffThinsTowardTheTop) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    fog.heightFalloff = 1.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    const float bottom = SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.0F, -0.9F, 0.0F});
    const float top = SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.0F, 0.9F, 0.0F});
    EXPECT_GT(bottom, top);
    EXPECT_GT(bottom, 0.5F);
}

TEST_F(FogVolume3DUVETest, EllipsoidRejectsTheBoxCorner) {
    FogVolume3DComponentUVE fog{};
    fog.shape = FogVolumeShapeUVE::Ellipsoid;
    fog.edgeFade = 0.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    EXPECT_GT(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.95F, 0.95F, 0.95F}), 0.0F);
}

TEST_F(FogVolume3DUVETest, ConeHoldsTheBaseAndDropsTheTipSides) {
    FogVolume3DComponentUVE fog{};
    fog.shape = FogVolumeShapeUVE::Cone;
    fog.edgeFade = 0.0F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    EXPECT_GT(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.0F, -0.5F, 0.0F}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{0.8F, 0.8F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DUVETest, WorldFillsEverywhere) {
    FogVolume3DComponentUVE fog{};
    fog.shape = FogVolumeShapeUVE::World;
    fog.density = 0.25F;
    const EntityUVE entity = PlaceFogUVE(fog);
    EXPECT_NEAR(SampleFogVolume3DDensityUVE(FrameOfUVE(entity), Math::Vector3UVE{80.0F, 12.0F, -40.0F}), 0.25F, 1.0e-5F);
}

TEST_F(FogVolume3DUVETest, ScaleWidensTheVolumeInWorld) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    TransformComponentUVE transform{};
    transform.localScale = Math::Vector3UVE{2.0F, 1.0F, 1.0F};
    const EntityUVE entity = PlaceFogUVE(fog, transform);
    const FogVolume3DFrameUVE frame = FrameOfUVE(entity);
    EXPECT_GT(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{1.9F, 0.0F, 0.0F}), 0.0F);
    EXPECT_EQ(SampleFogVolume3DDensityUVE(frame, Math::Vector3UVE{2.1F, 0.0F, 0.0F}), 0.0F);
}

TEST_F(FogVolume3DUVETest, RayThroughABoxMatchesDensityTimesWidth) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    fog.density = 0.5F;
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DRaySampleUVE sample =
        IntegrateFogVolume3DRayUVE(FrameOfUVE(entity), Math::Vector3UVE{-4.0F, 0.0F, 0.0F},
                                   Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 8.0F);
    EXPECT_NEAR(sample.opticalDepth, 1.0F, 0.08F);
    EXPECT_GT(sample.scatterWeight, 0.0F);
}

TEST_F(FogVolume3DUVETest, RayThatMissesIsEmpty) {
    FogVolume3DComponentUVE fog{};
    const EntityUVE entity = PlaceFogUVE(fog);
    const FogVolume3DRaySampleUVE sample =
        IntegrateFogVolume3DRayUVE(FrameOfUVE(entity), Math::Vector3UVE{0.0F, 4.0F, 0.0F},
                                   Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 8.0F);
    EXPECT_EQ(sample.opticalDepth, 0.0F);
    EXPECT_EQ(sample.scatterWeight, 0.0F);
}

TEST_F(FogVolume3DUVETest, CollectSkipsAPartitionHiddenVolume) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    const EntityUVE volume = PlaceFogUVE(fog);
    const EntityUVE partition = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, partition, TransformComponentUVE{});
    entityManager.AddComponentUVE<WorldPartition3DComponentUVE>(partition);
    entityManager.AddComponentUVE<WorldPartition3DMembershipComponentUVE>(
        volume, WorldPartition3DMembershipComponentUVE{partition, false});
    sceneGraph.UpdateUVE(entityManager);

    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    EXPECT_EQ(CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames), 0U);
}

TEST_F(FogVolume3DUVETest, CollectSkipsARegionHiddenVolume) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    const EntityUVE volume = PlaceFogUVE(fog);
    const EntityUVE region = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, region, TransformComponentUVE{});
    entityManager.AddComponentUVE<VisibilityRegion3DComponentUVE>(region);
    entityManager.AddComponentUVE<VisibilityRegion3DMembershipComponentUVE>(
        volume, VisibilityRegion3DMembershipComponentUVE{region, false});
    sceneGraph.UpdateUVE(entityManager);

    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    EXPECT_EQ(CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames), 0U);
}

TEST_F(FogVolume3DUVETest, CollectKeepsTheNearestAndDropsHidden) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    PlaceFogUVE(fog);
    TransformComponentUVE farTransform{};
    farTransform.localPosition = Math::Vector3UVE{50.0F, 0.0F, 0.0F};
    PlaceFogUVE(fog, farTransform);

    const EntityUVE hidden = PlaceFogUVE(fog);
    entityManager.GetComponentUVE<VisibilityComponentUVE>(hidden).visible = false;
    sceneGraph.UpdateUVE(entityManager);

    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    const std::size_t count = CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames);
    EXPECT_EQ(count, 2U);
    EXPECT_NEAR(frames[0].worldPosition.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(frames[1].worldPosition.x, 50.0F, 1.0e-4F);
}

TEST_F(FogVolume3DUVETest, WorldVolumesWinTheBudgetOverFarBoxes) {
    FogVolume3DComponentUVE world{};
    world.shape = FogVolumeShapeUVE::World;
    world.density = 0.1F;
    TransformComponentUVE worldTransform{};
    worldTransform.localPosition = Math::Vector3UVE{100.0F, 0.0F, 0.0F};
    PlaceFogUVE(world, worldTransform);

    FogVolume3DComponentUVE box{};
    PlaceFogUVE(box);

    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    const std::size_t count = CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames);
    ASSERT_EQ(count, 2U);
    EXPECT_EQ(frames[0].shape, FogVolumeShapeUVE::World);
}

TEST_F(FogVolume3DUVETest, ZeroDensityAndNoEmissionIsSkipped) {
    FogVolume3DComponentUVE fog{};
    fog.density = 0.0F;
    PlaceFogUVE(fog);
    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    EXPECT_EQ(CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames), 0U);
}

TEST_F(FogVolume3DUVETest, EmissionOnlyVolumeStillCollects) {
    FogVolume3DComponentUVE fog{};
    fog.density = 0.0F;
    fog.emission = Math::Vector3UVE{1.0F, 0.2F, 0.0F};
    PlaceFogUVE(fog);
    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    ASSERT_EQ(CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames), 1U);
    EXPECT_NEAR(frames[0].emission.x, 1.0F, 1.0e-5F);
}

TEST_F(FogVolume3DUVETest, CollectCapsAtTheFrameBudget) {
    FogVolume3DComponentUVE fog{};
    fog.edgeFade = 0.0F;
    for (std::uint32_t i = 0; i < kMaximumFogVolumesPerFrameUVE + 2U; ++i) {
        TransformComponentUVE transform{};
        transform.localPosition = Math::Vector3UVE{static_cast<float>(i) * 3.0F, 0.0F, 0.0F};
        PlaceFogUVE(fog, transform);
    }
    std::array<FogVolume3DFrameUVE, kMaximumFogVolumesPerFrameUVE> frames{};
    EXPECT_EQ(CollectFogVolume3DFramesUVE(entityManager, Math::Vector3UVE{}, frames), kMaximumFogVolumesPerFrameUVE);
    EXPECT_NEAR(frames[0].worldPosition.x, 0.0F, 1.0e-4F);
}

TEST_F(FogVolume3DUVETest, DegenerateScaleFailsClosed) {
    FogVolume3DComponentUVE fog{};
    FogVolume3DFrameUVE frame{};
    EXPECT_FALSE(TryMakeFogVolume3DFrameUVE(fog, Math::Vector3UVE{}, Math::QuaternionUVE{},
                                            Math::Vector3UVE{0.0F, 1.0F, 1.0F}, frame));
}

TEST_F(FogVolume3DUVETest, GizmoSkipsEditorInternal) {
    FogVolume3DComponentUVE fog{};
    const EntityUVE hidden = PlaceFogUVE(fog);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(hidden);
    const EntityUVE visible = PlaceFogUVE(fog);
    std::vector<FogVolume3DGizmoUVE> gizmos;
    CollectFogVolume3DGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_EQ(gizmos[0].origin.x, entityManager.GetComponentUVE<WorldTransformComponentUVE>(visible).worldPosition.x);
}

} // namespace
} // namespace UVE::Scene::Tests
