// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "entity_manager.h"
#include "event_system.h"
#include "light_cluster_grid.h"
#include "light_component.h"
#include "matrix4x4.h"
#include "memory_manager.h"
#include "quaternion.h"
#include "scalar.h"
#include "taa_math.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

TEST(LightClusterGridTest, LogarithmicSliceBoundsAreMonotonicAndClamped) {
    EXPECT_EQ(ComputeLightClusterSlice(0.05F, 0.1F, 1000.0F), 0U);
    EXPECT_EQ(ComputeLightClusterSlice(0.1F, 0.1F, 1000.0F), 0U);
    EXPECT_EQ(ComputeLightClusterSlice(1000.0F, 0.1F, 1000.0F), kLightClusterCountZ - 1U);
    EXPECT_EQ(ComputeLightClusterSlice(5000.0F, 0.1F, 1000.0F), kLightClusterCountZ - 1U);

    std::uint32_t prev = 0U;
    for (float d = 0.1F; d <= 1000.0F; d *= 1.5F) {
        const std::uint32_t slice = ComputeLightClusterSlice(d, 0.1F, 1000.0F);
        EXPECT_GE(slice, prev);
        EXPECT_LT(slice, kLightClusterCountZ);
        prev = slice;
    }
}

TEST(LightClusterGridTest, PointLightIsLocalizedAndBehindCameraLightIsCulled) {
    const Math::Matrix4x4 view = Math::Matrix4x4::Identity();
    const Math::Matrix4x4 proj =
        Math::Matrix4x4::Perspective(Math::DegToRad(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);

    LightData visiblePoint{};
    visiblePoint.type = Scene::LightType::Point;
    visiblePoint.position = Math::Vector3{0.0F, 0.0F, -10.0F};
    visiblePoint.intensity = 5.0F;
    visiblePoint.range = 2.0F;

    LightData behindCameraPoint{};
    behindCameraPoint.type = Scene::LightType::Point;
    behindCameraPoint.position = Math::Vector3{0.0F, 0.0F, 20.0F};
    behindCameraPoint.intensity = 5.0F;
    behindCameraPoint.range = 2.0F;

    const std::vector<LightData> lights{visiblePoint, behindCameraPoint};
    LightClusterGridData grid;
    BuildLightClusterGrid(lights, nullptr, view, proj, 0.1F, 100.0F, grid);

    EXPECT_EQ(grid.activeLightCount, 2U);
    std::size_t nonEmptyClusters = 0U;
    for (const LightClusterCell& cell : grid.cells) {
        if (cell.count > 0U) {
            ++nonEmptyClusters;
            for (std::uint32_t k = 0U; k < cell.count; ++k) {
                EXPECT_EQ(grid.lightIndices[cell.offset + k], 0U);
            }
        }
    }
    EXPECT_GT(nonEmptyClusters, 0U);
    EXPECT_LT(nonEmptyClusters, kTotalLightClusters / 4U);
}

TEST(LightClusterGridTest, ExtractsMoreThanSixteenDynamicLightsUpToClusterCap) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    for (int i = 0; i < 40; ++i) {
        const Scene::Entity e = em.CreateEntity();
        Scene::WorldTransformComponent wt{};
        wt.worldPosition = Math::Vector3{static_cast<float>(i), 0.0F, -5.0F};
        wt.dirty = false;
        em.AddComponent<Scene::WorldTransformComponent>(e, wt);
        Scene::LightComponent lc{};
        lc.type = Scene::LightType::Point;
        lc.intensity = 2.0F;
        lc.range = 100.0F;
        em.AddComponent<Scene::LightComponent>(e, lc);
    }

    const std::vector<LightData> extracted =
        ExtractClusteredLightsForView(em, Math::Vector3{0.0F, 0.0F, 0.0F}, 64U);
    EXPECT_EQ(extracted.size(), 40U);
}

TEST(TaaMathTest, HaltonJitterStaysWithinHalfPixelAndAppliesToProjection) {
    const Math::Matrix4x4 proj =
        Math::Matrix4x4::Perspective(Math::DegToRad(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);
    for (std::uint32_t frame = 0U; frame < kTaaJitterSampleCount; ++frame) {
        const auto jitter = ComputeTaaJitterNdc(frame, 1280U, 720U);
        EXPECT_LE(std::abs(jitter[0]), 1.0F / 1280.0F + 1.0e-6F);
        EXPECT_LE(std::abs(jitter[1]), 1.0F / 720.0F + 1.0e-6F);
        const Math::Matrix4x4 jProj = ApplyTaaProjectionJitter(proj, frame, 1280U, 720U);
        EXPECT_NEAR(jProj.m[0][2], proj.m[0][2] + jitter[0], 1.0e-6F);
        EXPECT_NEAR(jProj.m[1][2], proj.m[1][2] + jitter[1], 1.0e-6F);
    }
}

} // namespace
} // namespace UVE::Render
