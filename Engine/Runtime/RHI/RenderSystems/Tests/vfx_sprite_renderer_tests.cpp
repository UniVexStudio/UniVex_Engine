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

constexpr float kEpsilon = 1.0e-4F;

TEST(VfxSpriteRendererTest, FacingCameraPlane_AlignsCornersWithCameraRightAndUp) {
    const Math::Vector3 center{1.0F, 2.0F, -3.0F};
    const Math::Vector2 size{2.0F, 4.0F};
    const Math::Vector3 camPos{0.0F, 0.0F, 0.0F};
    const Math::Vector3 camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3 camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3 camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCorners corners = ExpandVfxSpriteCorners(
        center, size, 0.0F, Math::Vector3{}, 0, 0.0F, Math::Vector3{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    EXPECT_NEAR(corners[0].x, 0.0F, kEpsilon);
    EXPECT_NEAR(corners[0].y, 0.0F, kEpsilon);
    EXPECT_NEAR(corners[0].z, -3.0F, kEpsilon);

    EXPECT_NEAR(corners[2].x, 2.0F, kEpsilon);
    EXPECT_NEAR(corners[2].y, 4.0F, kEpsilon);
    EXPECT_NEAR(corners[2].z, -3.0F, kEpsilon);
}

TEST(VfxSpriteRendererTest, FacingSpherical_QuadNormalPointsAtCameraEye) {
    const Math::Vector3 center{3.0F, 1.0F, -4.0F};
    const Math::Vector2 size{1.5F, 1.5F};
    const Math::Vector3 camPos{-2.0F, 4.0F, 2.0F};
    const Math::Vector3 camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3 camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3 camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCorners corners = ExpandVfxSpriteCorners(
        center, size, 0.0F, Math::Vector3{}, 1, 0.0F, Math::Vector3{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    const Math::Vector3 edgeRight = corners[1] - corners[0];
    const Math::Vector3 edgeUp = corners[3] - corners[0];
    const Math::Vector3 quadNormal = Math::Normalize(Math::Cross(edgeRight, edgeUp));
    const Math::Vector3 expectedToCamera = Math::Normalize(camPos - center);

    EXPECT_NEAR(Math::Dot(quadNormal, expectedToCamera), 1.0F, 1.0e-3F);
}

TEST(VfxSpriteRendererTest, FacingVelocityAligned_StretchesAlongVelocityDirection) {
    const Math::Vector3 center{0.0F, 0.0F, -5.0F};
    const Math::Vector2 size{1.0F, 2.0F};
    const Math::Vector3 velocity{0.0F, 10.0F, 0.0F};
    const float stretchScale = 0.25F;
    const Math::Vector3 camPos{0.0F, 0.0F, 0.0F};
    const Math::Vector3 camRight{1.0F, 0.0F, 0.0F};
    const Math::Vector3 camUp{0.0F, 1.0F, 0.0F};
    const Math::Vector3 camForward{0.0F, 0.0F, -1.0F};

    const VfxSpriteQuadCorners corners = ExpandVfxSpriteCorners(
        center, size, 0.0F, velocity, 2, stretchScale, Math::Vector3{0.0F, 1.0F, 0.0F},
        camPos, camRight, camUp, camForward);

    const float width = Math::Length(corners[1] - corners[0]);
    const float height = Math::Length(corners[3] - corners[0]);
    // Height should be base size.y (2.0) + |velocity| (10.0) * stretchScale (0.25) = 4.5.
    EXPECT_NEAR(width, 1.0F, kEpsilon);
    EXPECT_NEAR(height, 4.5F, kEpsilon);
}

TEST(VfxSpriteRendererTest, FacingFixedAxis_LiesInPlaneOrthogonalToAxis) {
    const Math::Vector3 center{0.0F, 0.5F, 0.0F};
    const Math::Vector2 size{2.0F, 2.0F};
    const Math::Vector3 axis{0.0F, 1.0F, 0.0F};

    const VfxSpriteQuadCorners corners = ExpandVfxSpriteCorners(
        center, size, 0.0F, Math::Vector3{}, 3, 0.0F, axis,
        Math::Vector3{0.0F, 5.0F, 5.0F},
        Math::Vector3{1.0F, 0.0F, 0.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{0.0F, 0.0F, -1.0F});

    for (const Math::Vector3& corner : corners) {
        EXPECT_NEAR(corner.y, 0.5F, kEpsilon);
    }
}

TEST(VfxSpriteRendererTest, RadixSort_OrdersNegativeAndPositiveDepthsBackToFront) {
    std::vector<float> depths{-0.25F, 14.5F, -12.0F, 0.0F, 3.2F, 100.0F, -0.01F, 3.2F};
    std::vector<VfxSpriteGpuInstance> instances(depths.size());
    for (std::size_t i = 0U; i < depths.size(); ++i) {
        instances[i].posZ = depths[i];
        instances[i].frame = static_cast<float>(i);
    }

    RadixSortVfxSprites(instances, depths);

    ASSERT_EQ(depths.size(), 8U);
    for (std::size_t i = 1U; i < depths.size(); ++i) {
        EXPECT_GE(depths[i - 1U], depths[i]);
        EXPECT_FLOAT_EQ(instances[i].posZ, depths[i]);
    }
    EXPECT_FLOAT_EQ(depths.front(), 100.0F);
    EXPECT_FLOAT_EQ(depths.back(), -12.0F);
}

TEST(VfxSpriteRendererTest, SoftParticleAndCameraFade_MatchDocumentedClampedFormulas) {
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFade(10.0F, 9.0F, 0.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFade(10.0F, 10.0F, 2.0F), 0.0F);
    EXPECT_NEAR(ComputeVfxSoftParticleFade(10.0F, 9.0F, 2.0F), 0.5F, kEpsilon);
    EXPECT_FLOAT_EQ(ComputeVfxSoftParticleFade(10.0F, 7.0F, 2.0F), 1.0F);

    EXPECT_FLOAT_EQ(ComputeVfxCameraFade(0.5F, 1.0F, 3.0F), 0.0F);
    EXPECT_NEAR(ComputeVfxCameraFade(2.0F, 1.0F, 3.0F), 0.5F, kEpsilon);
    EXPECT_FLOAT_EQ(ComputeVfxCameraFade(5.0F, 1.0F, 3.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeVfxCameraFade(0.2F, 2.0F, 2.0F), 1.0F);
}

TEST(VfxSpriteRendererTest, FlipbookUv_ComputesCellCoordinatesAndSubFrameBlend) {
    // 2 rows x 4 cols = 8 frames (0..7). Frame 5 is row 1, col 1:
    // u in [0.25, 0.50], v in [0.50, 1.00].
    const VfxFlipbookUvSample unblended = ComputeVfxFlipbookUvs(
        Math::Vector2{0.5F, 0.5F}, 5.35F, 2, 4, false);
    EXPECT_EQ(unblended.frame0, 5);
    EXPECT_EQ(unblended.frame1, 5);
    EXPECT_FLOAT_EQ(unblended.blendFactor, 0.0F);
    EXPECT_NEAR(unblended.uv0.x, (1.0F + 0.5F) / 4.0F, kEpsilon);
    EXPECT_NEAR(unblended.uv0.y, (1.0F + 0.5F) / 2.0F, kEpsilon);

    // With frameBlend = true at frame 5.25 -> blends frame 5 (col 1, row 1) and frame 6 (col 2, row 1).
    const VfxFlipbookUvSample blended = ComputeVfxFlipbookUvs(
        Math::Vector2{0.0F, 1.0F}, 5.25F, 2, 4, true);
    EXPECT_EQ(blended.frame0, 5);
    EXPECT_EQ(blended.frame1, 6);
    EXPECT_NEAR(blended.blendFactor, 0.25F, kEpsilon);
    EXPECT_NEAR(blended.uv0.x, 1.0F / 4.0F, kEpsilon);
    EXPECT_NEAR(blended.uv1.x, 2.0F / 4.0F, kEpsilon);
}

TEST(VfxSpriteRendererTest, BudgetCap_TruncatesBeyondOneMillionAndReportsCount) {
    VfxDrawList drawList;
    VfxSpriteBatch batchA;
    batchA.blend = 0;
    batchA.instances.resize(600'000U);
    batchA.sortDepths.resize(600'000U, 5.0F);

    VfxSpriteBatch batchB;
    batchB.blend = 1;
    batchB.instances.resize(500'000U);
    batchB.sortDepths.resize(500'000U, 8.0F);

    drawList.batches.push_back(std::move(batchA));
    drawList.batches.push_back(std::move(batchB));

    const std::size_t truncated = ApplyVfxSpriteBudget(drawList, kMaximumVfxSpriteInstancesPerFrame);
    EXPECT_EQ(truncated, 100'000U);
    EXPECT_EQ(drawList.stats.instancesExtracted, kMaximumVfxSpriteInstancesPerFrame);
    EXPECT_EQ(drawList.stats.instancesTruncated, 100'000U);
    ASSERT_EQ(drawList.batches.size(), 2U);
    EXPECT_EQ(drawList.batches[0].instances.size(), 600'000U);
    EXPECT_EQ(drawList.batches[1].instances.size(), 400'000U);
}

TEST(VfxSpriteRendererTest, ExtractVfx_ExtractsLiveParticlesFromWorldIntoBatches) {
    VFX::VfxWorld world;
    const Asset::ParticleSystemAsset sparks =
        VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Sparks);
    const VFX::VfxHandle handle = world.SpawnFromAsset("sparks", sparks, VFX::VfxOwner{});
    ASSERT_TRUE(handle.IsValid());
    world.Tick(1.0F / 60.0F);

    VfxDrawList drawList;
    ParticleRenderBridge::ExtractVfx(
        world, drawList, Math::Vector3{0.0F, 0.0F, 5.0F}, Math::Vector3{0.0F, 0.0F, -1.0F});

    EXPECT_GT(drawList.stats.batchesExtracted, 0U);
    EXPECT_GT(drawList.stats.instancesExtracted, 0U);
    EXPECT_EQ(drawList.stats.instancesTruncated, 0U);
    EXPECT_TRUE(drawList.HasTranslucentBatches());
}

} // namespace
} // namespace UVE::Render
