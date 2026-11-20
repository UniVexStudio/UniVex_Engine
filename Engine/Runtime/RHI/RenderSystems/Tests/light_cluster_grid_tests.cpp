// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "entity_manager.h"
#include "light_cluster_grid.h"
#include "light_component.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "scalar.h"
#include "taa_math.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

TEST(LightClusterGridUVETest, LogarithmicSliceBoundsAreMonotonicAndClamped) {
    EXPECT_EQ(ComputeLightClusterSliceUVE(0.05F, 0.1F, 1000.0F), 0U);
    EXPECT_EQ(ComputeLightClusterSliceUVE(0.1F, 0.1F, 1000.0F), 0U);
    EXPECT_EQ(ComputeLightClusterSliceUVE(1000.0F, 0.1F, 1000.0F), kLightClusterCountZUVE - 1U);
    EXPECT_EQ(ComputeLightClusterSliceUVE(5000.0F, 0.1F, 1000.0F), kLightClusterCountZUVE - 1U);

    std::uint32_t prev = 0U;
    for (float d = 0.1F; d <= 1000.0F; d *= 1.5F) {
        const std::uint32_t slice = ComputeLightClusterSliceUVE(d, 0.1F, 1000.0F);
        EXPECT_GE(slice, prev);
        EXPECT_LT(slice, kLightClusterCountZUVE);
        prev = slice;
    }
}

TEST(LightClusterGridUVETest, PointLightIsLocalizedAndBehindCameraLightIsCulled) {
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::IdentityUVE();
    const Math::Matrix4x4UVE proj =
        Math::Matrix4x4UVE::PerspectiveUVE(Math::DegToRadUVE(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);

    LightDataUVE visiblePoint{};
    visiblePoint.type = Scene::LightTypeUVE::Point;
    visiblePoint.position = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    visiblePoint.intensity = 5.0F;
    visiblePoint.range = 2.0F;

    LightDataUVE behindCameraPoint{};
    behindCameraPoint.type = Scene::LightTypeUVE::Point;
    behindCameraPoint.position = Math::Vector3UVE{0.0F, 0.0F, 20.0F};
    behindCameraPoint.intensity = 5.0F;
    behindCameraPoint.range = 2.0F;

    const std::vector<LightDataUVE> lights{visiblePoint, behindCameraPoint};
    LightClusterGridDataUVE grid;
    BuildLightClusterGridUVE(lights, nullptr, view, proj, 0.1F, 100.0F, grid);

    EXPECT_EQ(grid.activeLightCount, 2U);
    std::size_t nonEmptyClusters = 0U;
    for (const LightClusterCellUVE& cell : grid.cells) {
        if (cell.count > 0U) {
            ++nonEmptyClusters;
            for (std::uint32_t k = 0U; k < cell.count; ++k) {
                EXPECT_EQ(grid.lightIndices[cell.offset + k], 0U);
            }
        }
    }
    EXPECT_GT(nonEmptyClusters, 0U);
    EXPECT_LT(nonEmptyClusters, kTotalLightClustersUVE / 4U);
}

TEST(LightClusterGridUVETest, ExtractsMoreThanSixteenDynamicLightsUpToClusterCap) {
    Scene::EntityManagerUVE em;
    for (int i = 0; i < 40; ++i) {
        const Scene::EntityUVE e = em.CreateEntityUVE();
        Scene::WorldTransformComponentUVE wt{};
        wt.worldPosition = Math::Vector3UVE{static_cast<float>(i), 0.0F, -5.0F};
        wt.dirty = false;
        em.AddComponentUVE<Scene::WorldTransformComponentUVE>(e, wt);
        Scene::LightComponentUVE lc{};
        lc.type = Scene::LightTypeUVE::Point;
        lc.intensity = 2.0F;
        lc.range = 100.0F;
        em.AddComponentUVE<Scene::LightComponentUVE>(e, lc);
    }

    const std::vector<LightDataUVE> extracted =
        ExtractClusteredLightsForViewUVE(em, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 64U);
    EXPECT_EQ(extracted.size(), 40U);
}

TEST(TaaMathUVETest, HaltonJitterStaysWithinHalfPixelAndAppliesToProjection) {
    const Math::Matrix4x4UVE proj =
        Math::Matrix4x4UVE::PerspectiveUVE(Math::DegToRadUVE(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);
    for (std::uint32_t frame = 0U; frame < kTaaJitterSampleCountUVE; ++frame) {
        const auto jitter = ComputeTaaJitterNdcUVE(frame, 1280U, 720U);
        EXPECT_LE(std::abs(jitter[0]), 1.0F / 1280.0F + 1.0e-6F);
        EXPECT_LE(std::abs(jitter[1]), 1.0F / 720.0F + 1.0e-6F);
        const Math::Matrix4x4UVE jProj = ApplyTaaProjectionJitterUVE(proj, frame, 1280U, 720U);
        EXPECT_NEAR(jProj.m[0][2], proj.m[0][2] + jitter[0], 1.0e-6F);
        EXPECT_NEAR(jProj.m[1][2], proj.m[1][2] + jitter[1], 1.0e-6F);
    }
}

} // namespace
} // namespace UVE::Render
