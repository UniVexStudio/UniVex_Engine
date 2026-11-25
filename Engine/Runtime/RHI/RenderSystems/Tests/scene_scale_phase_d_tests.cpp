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

TEST(HiZOcclusionTest, OccludesBoxBehindNearWallAndKeepsFrontBoxVisible) {
    constexpr std::uint32_t kSize = 16U;
    // Wall filling the whole screen at depth 0.30.
    std::vector<float> depth(kSize * kSize, 0.30F);
    HiZDepthPyramid pyramid;
    ASSERT_TRUE(pyramid.BuildFromDepthBuffer(depth, kSize, kSize));
    EXPECT_EQ(pyramid.MipCount(), 5U);

    const Math::Matrix4x4 proj =
        Math::Matrix4x4::Perspective(Math::DegToRad(60.0F), 1.0F, 0.1F, 100.0F);
    const Math::Matrix4x4 view = Math::Matrix4x4::Identity();
    const Math::Matrix4x4 vp = proj * view;

    // Far box well behind depth 0.30 -> occluded.
    const Math::Aabb farBox{Math::Vector3{-0.5F, -0.5F, -50.0F}, Math::Vector3{0.5F, 0.5F, -48.0F}};
    EXPECT_FALSE(pyramid.TestWorldAabbVisible(farBox, vp));

    // Near box in front of depth 0.30 -> visible.
    const Math::Aabb nearBox{Math::Vector3{-0.05F, -0.05F, -0.15F}, Math::Vector3{0.05F, 0.05F, -0.12F}};
    EXPECT_TRUE(pyramid.TestWorldAabbVisible(nearBox, vp));
}

TEST(OriginRebaseTest, ShiftsRootsAndPreservesDoublePrecisionWorldPosition) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Scene::Entity e = em.CreateEntity();
    Scene::TransformComponent tr{};
    tr.localPosition = Math::Vector3{3000.5F, 10.0F, -2500.25F};
    em.AddComponent<Scene::TransformComponent>(e, tr);
    Scene::WorldTransformComponent wt{};
    wt.worldPosition = tr.localPosition;
    wt.dirty = false;
    em.AddComponent<Scene::WorldTransformComponent>(e, wt);

    World::OriginRebaseState state{};
    state.rebaseThresholdMeters = 2048.0F;
    Math::Vector3 cameraPos{3000.5F, 10.0F, -2500.25F};
    const auto beforeDouble = state.LocalToWorldDouble(tr.localPosition);

    ASSERT_TRUE(World::TryRebaseWorldOrigin(em, cameraPos, state));
    EXPECT_EQ(state.rebaseCount, 1U);
    EXPECT_LT(Math::Length(cameraPos), 2.0F);

    const auto& shifted = em.GetComponent<Scene::TransformComponent>(e);
    const auto afterDouble = state.LocalToWorldDouble(shifted.localPosition);
    EXPECT_NEAR(afterDouble[0], beforeDouble[0], 1.0e-4);
    EXPECT_NEAR(afterDouble[1], beforeDouble[1], 1.0e-4);
    EXPECT_NEAR(afterDouble[2], beforeDouble[2], 1.0e-4);
}

TEST(OriginRebaseTest, AsyncWorldStreamQueueLoadsCellInBackground) {
    const std::filesystem::path tmpFile =
        std::filesystem::temp_directory_path() / "uve_test_stream_cell.bin";
    {
        std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
        out << "CELL_DATA_OK";
    }
    World::AsyncWorldStreamQueue queue;
    queue.RequestLoad("cell_0_0", tmpFile);
    EXPECT_EQ(queue.PendingCount(), 1U);

    std::vector<World::StreamedCellPayload> completed;
    for (int tries = 0; tries < 50 && completed.empty(); ++tries) {
        completed = queue.PollCompleted(4U);
        if (completed.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::filesystem::remove(tmpFile);
    ASSERT_EQ(completed.size(), 1U);
    EXPECT_TRUE(completed[0].succeeded);
    EXPECT_EQ(completed[0].rawBytes.size(), 12U);
}

TEST(FoliageWindTest, DeterministicScatterAndWindDisplacement) {
    Scene::MultiMeshInstance3DComponent mm{};
    Scene::FoliageScatterSettings settings{};
    settings.count = 128U;
    settings.seed = 42U;
    Scene::GenerateFoliageScatterInstances(settings, mm);
    ASSERT_EQ(mm.instances.size(), 128U);

    Scene::FoliageWindZone3DComponent wind{};
    wind.windStrength = 0.5F;
    const Math::Vector3 baseDisp =
        Scene::EvaluateFoliageWindDisplacement(Math::Vector3{1.0F, 0.0F, 2.0F}, 0.0F, 1.2F, wind);
    const Math::Vector3 tipDisp =
        Scene::EvaluateFoliageWindDisplacement(Math::Vector3{1.0F, 1.0F, 2.0F}, 1.0F, 1.2F, wind);
    EXPECT_FLOAT_EQ(Math::Length(baseDisp), 0.0F);
    EXPECT_GT(Math::Length(tipDisp), 0.0F);
    EXPECT_LE(Math::Length(tipDisp), wind.maxSwayMeters + 1.0e-5F);
}

} // namespace
} // namespace UVE::Render
