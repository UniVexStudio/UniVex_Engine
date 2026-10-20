// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/shadow_atlas_manager_uve.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"

namespace UVE::Render::Tests {
namespace {

TEST(ShadowAtlasManagerUVETest, InitializesEmptyInCpuMode) {
    ShadowAtlasManagerUVE manager(nullptr);
    EXPECT_EQ(manager.GetAtlasTextureUVE(), kInvalidTextureHandleUVE);
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 0U);
    EXPECT_EQ(manager.GetTotalNodeCountUVE(), 1U);
}

TEST(ShadowAtlasManagerUVETest, AllocatesAndFreesRootTile) {
    ShadowAtlasManagerUVE manager(nullptr);
    const auto tileOpt = manager.AllocateTileUVE(kShadowAtlasResolutionUVE);
    ASSERT_TRUE(tileOpt.has_value());

    const TileAllocUVE& tile = *tileOpt;
    EXPECT_EQ(tile.tileId, 0U);
    EXPECT_EQ(tile.pixelRect.position.x, 0);
    EXPECT_EQ(tile.pixelRect.position.y, 0);
    EXPECT_EQ(tile.pixelRect.size.x, static_cast<std::int32_t>(kShadowAtlasResolutionUVE));
    EXPECT_EQ(tile.pixelRect.size.y, static_cast<std::int32_t>(kShadowAtlasResolutionUVE));
    EXPECT_FLOAT_EQ(tile.normalizedRect.x, 0.0F);
    EXPECT_FLOAT_EQ(tile.normalizedRect.y, 0.0F);
    EXPECT_FLOAT_EQ(tile.normalizedRect.z, 1.0F);
    EXPECT_FLOAT_EQ(tile.normalizedRect.w, 1.0F);
    EXPECT_FLOAT_EQ(tile.GetExtentUVE(), 1.0F);

    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 1U);
    EXPECT_TRUE(manager.IsTileAllocatedUVE(tile.tileId));

    // When root is allocated, further allocations must fail
    const auto failTileOpt = manager.AllocateTileUVE(128U);
    EXPECT_FALSE(failTileOpt.has_value());

    manager.FreeTileUVE(tile);
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 0U);
    EXPECT_FALSE(manager.IsTileAllocatedUVE(tile.tileId));
}

TEST(ShadowAtlasManagerUVETest, PowerOfTwoClampingAndBounds) {
    ShadowAtlasManagerUVE manager(nullptr);

    // 100 clamps to min 128
    const auto tile100 = manager.AllocateTileUVE(100U);
    ASSERT_TRUE(tile100.has_value());
    EXPECT_EQ(tile100->pixelRect.size.x, 128);

    // 300 clamps to 512
    const auto tile300 = manager.AllocateTileUVE(300U);
    ASSERT_TRUE(tile300.has_value());
    EXPECT_EQ(tile300->pixelRect.size.x, 512);

    // Exceeding atlas resolution fails
    const auto tileOver = manager.AllocateTileUVE(5000U);
    EXPECT_FALSE(tileOver.has_value());
}

TEST(ShadowAtlasManagerUVETest, QuadtreeSubdivisionFourWayQuadrants) {
    ShadowAtlasManagerUVE manager(nullptr);

    // Allocating 2048 should split root into four 2048 nodes
    const auto t0 = manager.AllocateTileUVE(2048U);
    const auto t1 = manager.AllocateTileUVE(2048U);
    const auto t2 = manager.AllocateTileUVE(2048U);
    const auto t3 = manager.AllocateTileUVE(2048U);

    ASSERT_TRUE(t0.has_value());
    ASSERT_TRUE(t1.has_value());
    ASSERT_TRUE(t2.has_value());
    ASSERT_TRUE(t3.has_value());

    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 4U);

    // Verify coordinates of the four quadrants
    EXPECT_EQ(t0->pixelRect.position.x, 0);
    EXPECT_EQ(t0->pixelRect.position.y, 0);

    EXPECT_EQ(t1->pixelRect.position.x, 2048);
    EXPECT_EQ(t1->pixelRect.position.y, 0);

    EXPECT_EQ(t2->pixelRect.position.x, 0);
    EXPECT_EQ(t2->pixelRect.position.y, 2048);

    EXPECT_EQ(t3->pixelRect.position.x, 2048);
    EXPECT_EQ(t3->pixelRect.position.y, 2048);

    // Fifth 2048 must fail as atlas is fully occupied
    const auto t4 = manager.AllocateTileUVE(2048U);
    EXPECT_FALSE(t4.has_value());
}

TEST(ShadowAtlasManagerUVETest, BuddyCoalescingRestoresFullAtlasCapacity) {
    ShadowAtlasManagerUVE manager(nullptr);

    const auto t0 = manager.AllocateTileUVE(2048U);
    const auto t1 = manager.AllocateTileUVE(2048U);
    const auto t2 = manager.AllocateTileUVE(2048U);
    const auto t3 = manager.AllocateTileUVE(2048U);

    ASSERT_TRUE(t0.has_value() && t1.has_value() && t2.has_value() && t3.has_value());

    // Free in non-sequential order
    manager.FreeTileUVE(*t1);
    manager.FreeTileUVE(*t3);
    manager.FreeTileUVE(*t0);

    // Root cannot coalesce yet because t2 is still allocated
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 1U);
    EXPECT_FALSE(manager.AllocateTileUVE(4096U).has_value());

    // Free final sibling: triggers 4-way buddy coalescing into root
    manager.FreeTileUVE(*t2);
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 0U);

    // Full 4096 tile allocation must now succeed with zero fragmentation
    const auto fullTile = manager.AllocateTileUVE(4096U);
    ASSERT_TRUE(fullTile.has_value());
    EXPECT_EQ(fullTile->pixelRect.size.x, 4096);
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 1U);
}

TEST(ShadowAtlasManagerUVETest, RecursiveDeepBuddyCoalescing) {
    ShadowAtlasManagerUVE manager(nullptr);

    // Allocate 128 (depth 5: 4096 -> 2048 -> 1024 -> 512 -> 256 -> 128)
    const auto t128_0 = manager.AllocateTileUVE(128U);
    const auto t128_1 = manager.AllocateTileUVE(128U);
    const auto t128_2 = manager.AllocateTileUVE(128U);
    const auto t128_3 = manager.AllocateTileUVE(128U);

    ASSERT_TRUE(t128_0.has_value() && t128_1.has_value() && t128_2.has_value() && t128_3.has_value());
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 4U);

    // Free all 4 128 buddies -> coalesces into 256 node
    manager.FreeTileUVE(*t128_0);
    manager.FreeTileUVE(*t128_2);
    manager.FreeTileUVE(*t128_1);
    manager.FreeTileUVE(*t128_3);

    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 0U);

    // Entire tree should coalesce back to root 4096
    const auto rootTile = manager.AllocateTileUVE(4096U);
    ASSERT_TRUE(rootTile.has_value());
    EXPECT_EQ(rootTile->pixelRect.size.x, 4096);
}

TEST(ShadowAtlasManagerUVETest, OutOfMemoryGracefulFailureAndRecovery) {
    ShadowAtlasManagerUVE manager(nullptr);

    // Fill atlas with sixteen 1024 tiles (4x4 = 16)
    std::vector<TileAllocUVE> tiles;
    tiles.reserve(16);
    for (std::size_t i = 0; i < 16; ++i) {
        const auto tile = manager.AllocateTileUVE(1024U);
        ASSERT_TRUE(tile.has_value());
        tiles.push_back(*tile);
    }

    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 16U);

    // 17th tile fails
    const auto overflow = manager.AllocateTileUVE(128U);
    EXPECT_FALSE(overflow.has_value());

    // Free one 1024 tile
    manager.FreeTileUVE(tiles[0]);
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 15U);

    // Can allocate smaller tiles inside the freed 1024 region
    const auto sub512_0 = manager.AllocateTileUVE(512U);
    const auto sub512_1 = manager.AllocateTileUVE(512U);
    ASSERT_TRUE(sub512_0.has_value());
    ASSERT_TRUE(sub512_1.has_value());
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 17U);
}

TEST(ShadowAtlasManagerUVETest, ResetUVE_ClearsAllAllocations) {
    ShadowAtlasManagerUVE manager(nullptr);

    for (int i = 0; i < 8; ++i) {
        manager.AllocateTileUVE(512U);
    }
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 8U);

    manager.ResetUVE();
    EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 0U);

    // Root capacity is fully available
    const auto root = manager.AllocateTileUVE(4096U);
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(root->pixelRect.size.x, 4096);
}

TEST(ShadowAtlasManagerUVETest, SpotLightPerspectiveMatrixMath) {
    const Math::Vector3UVE position{0.0F, 10.0F, 0.0F};
    const Math::QuaternionUVE rotation = Math::QuaternionUVE::FromAxisAngleUVE(
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -3.14159265358979323846F * 0.5F); // Look down -Y
    const float spotAngleDegrees = 45.0F;
    const float range = 50.0F;

    const Math::Matrix4x4UVE matrix = ShadowAtlasManagerUVE::ComputeSpotLightMatrixUVE(
        position, rotation, spotAngleDegrees, range, 0.1F);

    // Verify matrix values are finite and non-degenerate
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            EXPECT_TRUE(std::isfinite(matrix.m[r][c]));
        }
    }
    EXPECT_NE(matrix.m[3][2], 0.0F); // Perspective projection has non-zero w-term generator
}

TEST(ShadowAtlasManagerUVETest, OctahedralEncodingAndDecodingRoundTrip) {
    const std::vector<Math::Vector3UVE> testDirections = {
        Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        Math::Vector3UVE{-1.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{0.0F, -1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 0.0F, 1.0F},
        Math::Vector3UVE{0.0F, 0.0F, -1.0F},
        Math::NormalizeUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}),
        Math::NormalizeUVE(Math::Vector3UVE{-1.0F, 2.0F, -3.0F}),
        Math::NormalizeUVE(Math::Vector3UVE{0.5F, -0.7F, 0.4F}),
    };

    for (const auto& originalDir : testDirections) {
        const Math::Vector2UVE uv = ShadowAtlasManagerUVE::OctahedralEncodeUVE(originalDir);
        EXPECT_GE(uv.x, 0.0F);
        EXPECT_LE(uv.x, 1.0F);
        EXPECT_GE(uv.y, 0.0F);
        EXPECT_LE(uv.y, 1.0F);

        const Math::Vector3UVE reconstructed = ShadowAtlasManagerUVE::OctahedralDecodeUVE(uv);
        const float dot = Math::DotUVE(originalDir, reconstructed);
        EXPECT_GT(dot, 0.999F) << "Direction failed round-trip fidelity: ("
                               << originalDir.x << ", " << originalDir.y << ", " << originalDir.z << ")";
    }
}

TEST(ShadowAtlasManagerUVETest, TileRectCalculations) {
    const Math::Vector4UVE rect = ShadowAtlasManagerUVE::ComputeTileRectUVE(1024U, 2048U, 512U);
    EXPECT_FLOAT_EQ(rect.x, 1024.0F / 4096.0F); // 0.25
    EXPECT_FLOAT_EQ(rect.y, 2048.0F / 4096.0F); // 0.50
    EXPECT_FLOAT_EQ(rect.z, 1536.0F / 4096.0F); // 0.375
    EXPECT_FLOAT_EQ(rect.w, 2560.0F / 4096.0F); // 0.625

    const ViewportRectUVE pixelRect = ShadowAtlasManagerUVE::ComputePixelRectUVE(1024U, 2048U, 512U);
    EXPECT_EQ(pixelRect.position.x, 1024);
    EXPECT_EQ(pixelRect.position.y, 2048);
    EXPECT_EQ(pixelRect.size.x, 512);
    EXPECT_EQ(pixelRect.size.y, 512);
}

TEST(ShadowAtlasManagerUVETest, DeviceBackedLifecycleWithNullRenderDevice) {
    NullRenderDeviceUVE renderDevice;
    {
        ShadowAtlasManagerUVE manager(renderDevice);
        const TextureHandleUVE handle = manager.GetAtlasTextureUVE();
        EXPECT_NE(handle, kInvalidTextureHandleUVE);

        const auto tile = manager.AllocateTileUVE(1024U);
        ASSERT_TRUE(tile.has_value());
        EXPECT_EQ(manager.GetAllocatedTileCountUVE(), 1U);
    }
    // Destructor cleanly destroys texture via device without leaks
}

} // namespace
} // namespace UVE::Render::Tests
