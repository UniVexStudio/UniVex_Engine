// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "particle_render_bridge.h"
#include "vfx_draw_list.h"
#include "vfx_sprite_renderer.h"
#include "vfx_templates.h"
#include "vfx_world.h"

namespace UVE::Render {
namespace {

constexpr float kEpsilonUVE = 1.0e-4F;

TEST(VfxSpriteRendererUVETest, FacingCameraPlane_AlignsCornersWithCameraRightAndUp) {
    const Math::Vector3UVE center{1.0F, 2.0F, -3.0F};
    const Math::Vector2UVE size{2.0F, 4.0F};
    const Math::Vector3UVE camPos{0.0F, 0.0F, 0.0F};
    const Math::Vector3UVE camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3UVE camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCornersUVE corners = ExpandVfxSpriteCornersUVE(
        center, size, 0.0F, Math::Vector3UVE{}, 0, 0.0F, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    EXPECT_NEAR(corners[0].x, 0.0F, kEpsilonUVE);
    EXPECT_NEAR(corners[0].y, 0.0F, kEpsilonUVE);
    EXPECT_NEAR(corners[0].z, -3.0F, kEpsilonUVE);

    EXPECT_NEAR(corners[2].x, 2.0F, kEpsilonUVE);
    EXPECT_NEAR(corners[2].y, 4.0F, kEpsilonUVE);
    EXPECT_NEAR(corners[2].z, -3.0F, kEpsilonUVE);
}

TEST(VfxSpriteRendererUVETest, FacingSpherical_QuadNormalPointsAtCameraEye) {
    const Math::Vector3UVE center{3.0F, 1.0F, -4.0F};
    const Math::Vector2UVE size{1.5F, 1.5F};
    const Math::Vector3UVE camPos{-2.0F, 4.0F, 2.0F};
    const Math::Vector3UVE camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3UVE camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCornersUVE corners = ExpandVfxSpriteCornersUVE(
        center, size, 0.0F, Math::Vector3UVE{}, 1, 0.0F, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    const Math::Vector3UVE edgeRight = corners[1] - corners[0];
    const Math::Vector3UVE edgeUp = corners[3] - corners[0];
    const Math::Vector3UVE quadNormal = Math::NormalizeUVE(Math::CrossUVE(edgeRight, edgeUp));
    const Math::Vector3UVE expectedToCamera = Math::NormalizeUVE(camPos - center);

    EXPECT_NEAR(Math::DotUVE(quadNormal, expectedToCamera), 1.0F, 1.0e-3F);
}

TEST(VfxSpriteRendererUVETest, FacingVelocityAligned_StretchesAlongVelocityDirection) {
    const Math::Vector3UVE center{0.0F, 0.0F, -5.0F};
    const Math::Vector2UVE size{1.0F, 2.0F};
    const Math::Vector3UVE velocity{0.0F, 10.0F, 0.0F};
    const float stretchScale = 0.25F;
    const Math::Vector3UVE camPos{0.0F, 0.0F, 0.0F};
    const Math::Vector3UVE camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3UVE camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCornersUVE corners = ExpandVfxSpriteCornersUVE(
        center, size, 0.0F, velocity, 2, stretchScale, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    const float width = Math::LengthUVE(corners[1] - corners[0]);
    const float height = Math::LengthUVE(corners[3] - corners[0]);
    // Height should be base size.y (2.0) + |velocity| (10.0) * stretchScale (0.25) = 4.5.
    EXPECT_NEAR(width, 1.0F, kEpsilonUVE);
    EXPECT_NEAR(height, 4.5F, kEpsilonUVE);
}

TEST(VfxSpriteRendererUVETest, FacingFixedAxis_LiesInPlaneOrthogonalToAxis) {
    const Math::Vector3UVE center{0.0F, 0.5F, 0.0F};
    const Math::Vector2UVE size{2.0F, 2.0F};
    const Math::Vector3UVE axis{0.0F, 1.0F, 0.0F};

    const VfxSpriteQuadCornersUVE corners = ExpandVfxSpriteCornersUVE(
        center, size, 0.0F, Math::Vector3UVE{}, 3, 0.0F, axis,
        Math::Vector3UVE{0.0F, 5.0F, 5.0F},
        Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 0.0F, -1.0F});

    for (const Math::Vector3UVE& corner : corners) {
        EXPECT_NEAR(corner.y, 0.5F, kEpsilonUVE);
    }
}

TEST(VfxSpriteRendererUVETest, RadixSort_OrdersNegativeAndPositiveDepthsBackToFront) {
    std::vector<float> depths{-0.25F, 14.5F, -12.0F, 0.0F, 3.2F, 100.0F, -0.01F, 3.2F};
    std::vector<VfxSpriteGpuInstanceUVE> instances(depths.size());
    for (std::size_t i = 0U; i < depths.size(); ++i) {
        instances[i].posZ = depths[i];
        instances[i].frame = static_cast<float>(i);
    }

    RadixSortVfxSpritesUVE(instances, depths);

    ASSERT_EQ(depths.size(), 8U);
    for (std::size_t i = 1U; i < depths.size(); ++i) {
        EXPECT_GE(depths[i - 1U], depths[i]);
        EXPECT_FLOAT_EQ(instances[i].posZ, depths[i]);
    }
    EXPECT_FLOAT_EQ(depths.front(), 100.0F);
    EXPECT_FLOAT_EQ(depths.back(), -12.0F);
}

TEST(VfxSpriteRendererUVETest, SoftParticleAndCameraFade_MatchDocumentedClampedFormulas) {
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFadeUVE(10.0F, 9.0F, 0.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFadeUVE(10.0F, 10.0F, 2.0F), 0.0F);
    EXPECT_NEAR(ComputeVfxSoftParticleFadeUVE(10.0F, 9.0F, 2.0F), 0.5F, kEpsilonUVE);
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFadeUVE(10.0F, 7.0F, 2.0F), 1.0F);

    EXPECT_FLOAT_EQ(ComputeVfxCameraFadeUVE(0.5F, 1.0F, 3.0F), 0.0F);
    EXPECT_NEAR(ComputeVfxCameraFadeUVE(2.0F, 1.0F, 3.0F), 0.5F, kEpsilonUVE);
    EXPECT_FLOAT_EQ(ComputeVfxCameraFadeUVE(5.0F, 1.0F, 3.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeVfxCameraFadeUVE(0.2F, 2.0F, 2.0F), 1.0F);
}

TEST(VfxSpriteRendererUVETest, FlipbookUv_ComputesCellCoordinatesAndSubFrameBlend) {
    // 2 rows x 4 cols = 8 frames (0..7). Frame 5 is row 1, col 1:
    // u in [0.25, 0.50], v in [0.50, 1.00].
    const VfxFlipbookUvSampleUVE unblended = ComputeVfxFlipbookUvsUVE(
        Math::Vector2UVE{0.5F, 0.5F}, 5.35F, 2, 4, false);
    EXPECT_EQ(unblended.frame0, 5);
    EXPECT_EQ(unblended.frame1, 5);
    EXPECT_FLOAT_EQ(unblended.blendFactor, 0.0F);
    EXPECT_NEAR(unblended.uv0.x, (1.0F + 0.5F) / 4.0F, kEpsilonUVE);
    EXPECT_NEAR(unblended.uv0.y, (1.0F + 0.5F) / 2.0F, kEpsilonUVE);

    // With frameBlend = true at frame 5.25 -> blends frame 5 (col 1, row 1) and frame 6 (col 2, row 1).
    const VfxFlipbookUvSampleUVE blended = ComputeVfxFlipbookUvsUVE(
        Math::Vector2UVE{0.0F, 1.0F}, 5.25F, 2, 4, true);
    EXPECT_EQ(blended.frame0, 5);
    EXPECT_EQ(blended.frame1, 6);
    EXPECT_NEAR(blended.blendFactor, 0.25F, kEpsilonUVE);
    EXPECT_NEAR(blended.uv0.x, 1.0F / 4.0F, kEpsilonUVE);
    EXPECT_NEAR(blended.uv1.x, 2.0F / 4.0F, kEpsilonUVE);
}

TEST(VfxSpriteRendererUVETest, BudgetCap_TruncatesBeyondOneMillionAndReportsCount) {
    VfxDrawListUVE drawList;
    VfxSpriteBatchUVE batchA;
    batchA.blend = 0;
    batchA.instances.resize(600'000U);
    batchA.sortDepths.resize(600'000U, 5.0F);

    VfxSpriteBatchUVE batchB;
    batchB.blend = 1;
    batchB.instances.resize(500'000U);
    batchB.sortDepths.resize(500'000U, 8.0F);

    drawList.batches.push_back(std::move(batchA));
    drawList.batches.push_back(std::move(batchB));

    const std::size_t truncated = ApplyVfxSpriteBudgetUVE(drawList, kMaximumVfxSpriteInstancesPerFrameUVE);
    EXPECT_EQ(truncated, 100'000U);
    EXPECT_EQ(drawList.stats.instancesExtracted, kMaximumVfxSpriteInstancesPerFrameUVE);
    EXPECT_EQ(drawList.stats.instancesTruncated, 100'000U);
    ASSERT_EQ(drawList.batches.size(), 2U);
    EXPECT_EQ(drawList.batches[0].instances.size(), 600'000U);
    EXPECT_EQ(drawList.batches[1].instances.size(), 400'000U);
}

TEST(VfxSpriteRendererUVETest, ExtractVfxUVE_ExtractsLiveParticlesFromWorldIntoBatches) {
    VFX::VfxWorldUVE world;
    const Asset::ParticleSystemAssetUVE sparks =
        VFX::CreateParticleTemplateUVE(VFX::ParticleTemplateKindUVE::Sparks);
    const VFX::VfxHandleUVE handle = world.SpawnFromAssetUVE("sparks", sparks, VFX::VfxOwnerUVE{});
    ASSERT_TRUE(handle.IsValidUVE());
    world.TickUVE(1.0F / 60.0F);

    VfxDrawListUVE drawList;
    ParticleRenderBridgeUVE::ExtractVfxUVE(
        world, drawList, Math::Vector3UVE{0.0F, 0.0F, 5.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F});

    EXPECT_GT(drawList.stats.batchesExtracted, 0U);
    EXPECT_GT(drawList.stats.instancesExtracted, 0U);
    EXPECT_EQ(drawList.stats.instancesTruncated, 0U);
    EXPECT_TRUE(drawList.HasTranslucentBatchesUVE());
}

} // namespace
} // namespace UVE::Render
