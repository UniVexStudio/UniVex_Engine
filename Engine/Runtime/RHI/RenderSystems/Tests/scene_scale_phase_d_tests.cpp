// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "entity_manager.h"
#include "event_system.h"
#include "foliage_wind_3d.h"
#include "hiz_occlusion.h"
#include "matrix4x4.h"
#include "memory_manager.h"
#include "origin_rebase.h"
#include "scalar.h"
#include "transform_component.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

TEST(HiZOcclusionUVETest, OccludesBoxBehindNearWallAndKeepsFrontBoxVisible) {
    constexpr std::uint32_t kSize = 16U;
    // Wall filling the whole screen at depth 0.30.
    std::vector<float> depth(kSize * kSize, 0.30F);
    HiZDepthPyramidUVE pyramid;
    ASSERT_TRUE(pyramid.BuildFromDepthBufferUVE(depth, kSize, kSize));
    EXPECT_EQ(pyramid.MipCountUVE(), 5U);

    const Math::Matrix4x4UVE proj =
        Math::Matrix4x4UVE::PerspectiveUVE(Math::DegToRadUVE(60.0F), 1.0F, 0.1F, 100.0F);
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::IdentityUVE();
    const Math::Matrix4x4UVE vp = proj * view;

    // Far box well behind depth 0.30 -> occluded.
    const Math::AabbUVE farBox{Math::Vector3UVE{-0.5F, -0.5F, -50.0F}, Math::Vector3UVE{0.5F, 0.5F, -48.0F}};
    EXPECT_FALSE(pyramid.TestWorldAabbVisibleUVE(farBox, vp));

    // Near box in front of depth 0.30 -> visible.
    const Math::AabbUVE nearBox{Math::Vector3UVE{-0.05F, -0.05F, -0.15F}, Math::Vector3UVE{0.05F, 0.05F, -0.12F}};
    EXPECT_TRUE(pyramid.TestWorldAabbVisibleUVE(nearBox, vp));
}

TEST(OriginRebaseUVETest, ShiftsRootsAndPreservesDoublePrecisionWorldPosition) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE em{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const Scene::EntityUVE e = em.CreateEntityUVE();
    Scene::TransformComponentUVE tr{};
    tr.localPosition = Math::Vector3UVE{3000.5F, 10.0F, -2500.25F};
    em.AddComponentUVE<Scene::TransformComponentUVE>(e, tr);
    Scene::WorldTransformComponentUVE wt{};
    wt.worldPosition = tr.localPosition;
    wt.dirty = false;
    em.AddComponentUVE<Scene::WorldTransformComponentUVE>(e, wt);

    World::OriginRebaseStateUVE state{};
    state.rebaseThresholdMeters = 2048.0F;
    Math::Vector3UVE cameraPos{3000.5F, 10.0F, -2500.25F};
    const auto beforeDouble = state.LocalToWorldDoubleUVE(tr.localPosition);

    ASSERT_TRUE(World::TryRebaseWorldOriginUVE(em, cameraPos, state));
    EXPECT_EQ(state.rebaseCount, 1U);
    EXPECT_LT(Math::LengthUVE(cameraPos), 2.0F);

    const auto& shifted = em.GetComponentUVE<Scene::TransformComponentUVE>(e);
    const auto afterDouble = state.LocalToWorldDoubleUVE(shifted.localPosition);
    EXPECT_NEAR(afterDouble[0], beforeDouble[0], 1.0e-4);
    EXPECT_NEAR(afterDouble[1], beforeDouble[1], 1.0e-4);
    EXPECT_NEAR(afterDouble[2], beforeDouble[2], 1.0e-4);
}

TEST(OriginRebaseUVETest, AsyncWorldStreamQueueLoadsCellInBackground) {
    const std::filesystem::path tmpFile =
        std::filesystem::temp_directory_path() / "uve_test_stream_cell.bin";
    {
        std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
        out << "CELL_DATA_OK";
    }
    World::AsyncWorldStreamQueueUVE queue;
    queue.RequestLoadUVE("cell_0_0", tmpFile);
    EXPECT_EQ(queue.PendingCountUVE(), 1U);

    std::vector<World::StreamedCellPayloadUVE> completed;
    for (int tries = 0; tries < 50 && completed.empty(); ++tries) {
        completed = queue.PollCompletedUVE(4U);
        if (completed.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::filesystem::remove(tmpFile);
    ASSERT_EQ(completed.size(), 1U);
    EXPECT_TRUE(completed[0].succeeded);
    EXPECT_EQ(completed[0].rawBytes.size(), 12U);
}

TEST(FoliageWindUVETest, DeterministicScatterAndWindDisplacement) {
    Scene::MultiMeshInstance3DComponentUVE mm{};
    Scene::FoliageScatterSettingsUVE settings{};
    settings.count = 128U;
    settings.seed = 42U;
    Scene::GenerateFoliageScatterInstancesUVE(settings, mm);
    ASSERT_EQ(mm.instances.size(), 128U);

    Scene::FoliageWindZone3DComponentUVE wind{};
    wind.windStrength = 0.5F;
    const Math::Vector3UVE baseDisp =
        Scene::EvaluateFoliageWindDisplacementUVE(Math::Vector3UVE{1.0F, 0.0F, 2.0F}, 0.0F, 1.2F, wind);
    const Math::Vector3UVE tipDisp =
        Scene::EvaluateFoliageWindDisplacementUVE(Math::Vector3UVE{1.0F, 1.0F, 2.0F}, 1.0F, 1.2F, wind);
    EXPECT_FLOAT_EQ(Math::LengthUVE(baseDisp), 0.0F);
    EXPECT_GT(Math::LengthUVE(tipDisp), 0.0F);
    EXPECT_LE(Math::LengthUVE(tipDisp), wind.maxSwayMeters + 1.0e-5F);
}

} // namespace
} // namespace UVE::Render
