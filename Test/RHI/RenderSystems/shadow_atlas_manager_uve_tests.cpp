// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/shadow_atlas_manager_uve.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector4_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"

namespace UVE::Render::Tests {
namespace {

/// Full clip-space transform (x, y, z, w). Matrix4x4UVE exposes no matrix*vec4 operator and
/// TransformPointUVE has already divided by w, which is exactly the value the depth assertions need.
[[nodiscard]] Math::Vector4UVE TransformClipUVE(const Math::Matrix4x4UVE& matrix,
                                                const Math::Vector3UVE& point) noexcept {
    Math::Vector4UVE result{};
    result.x = matrix.m[0][0] * point.x + matrix.m[0][1] * point.y + matrix.m[0][2] * point.z + matrix.m[0][3];
    result.y = matrix.m[1][0] * point.x + matrix.m[1][1] * point.y + matrix.m[1][2] * point.z + matrix.m[1][3];
    result.z = matrix.m[2][0] * point.x + matrix.m[2][1] * point.y + matrix.m[2][2] * point.z + matrix.m[2][3];
    result.w = matrix.m[3][0] * point.x + matrix.m[3][1] * point.y + matrix.m[3][2] * point.z + matrix.m[3][3];
    return result;
}

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
        static_cast<void>(manager.AllocateTileUVE(512U));
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
    Math::QuaternionUVE rotation{};
    EXPECT_TRUE(Math::TryMakeAxisAngleUVE(
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -3.14159265358979323846F * 0.5F, rotation)); // Look down -Y
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

TEST(ShadowAtlasManagerUVETest, PointFaceTableIsSixRightHandedOrthonormalBases) {
    const auto table = PointLightShadowFaceTableUVE();
    ASSERT_EQ(table.size(), static_cast<std::size_t>(kPointShadowFaceCountUVE));
    for (std::size_t face = 0; face < table.size(); ++face) {
        const PointShadowFaceBasisUVE& basis = table[face];
        EXPECT_NEAR(Math::LengthUVE(basis.forward), 1.0F, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(Math::LengthUVE(basis.right), 1.0F, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(Math::LengthUVE(basis.up), 1.0F, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(Math::DotUVE(basis.right, basis.up), 0.0F, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(Math::DotUVE(basis.right, basis.forward), 0.0F, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(Math::DotUVE(basis.up, basis.forward), 0.0F, 1.0e-6F) << "face " << face;
        // Right-handed: right x up == forward. A flipped basis would mirror the face's image, and a
        // mirrored depth tile still looks like a shadow - just one belonging to geometry that is not
        // there.
        const Math::Vector3UVE cross = Math::CrossUVE(basis.right, basis.up);
        EXPECT_NEAR(cross.x, basis.forward.x, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(cross.y, basis.forward.y, 1.0e-6F) << "face " << face;
        EXPECT_NEAR(cross.z, basis.forward.z, 1.0e-6F) << "face " << face;
    }
    // All six axes are covered exactly once - a duplicated forward would leave half the sphere
    // unshadowed while every individual face still looked correct.
    const std::array<Math::Vector3UVE, 6> axes = {
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{-1.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, -1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 0.0F, 1.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    for (std::size_t face = 0; face < table.size(); ++face) {
        const float dot = Math::DotUVE(table[face].forward, axes[face]);
        EXPECT_NEAR(dot, 1.0F, 1.0e-6F) << "face " << face << " faces the wrong way";
    }
}

TEST(ShadowAtlasManagerUVETest, PointFaceSelectionUsesTheDominantAxis) {
    const auto faceFor = [](const Math::Vector3UVE& direction) {
        return ShadowAtlasManagerUVE::PointLightShadowFaceForDirectionUVE(direction);
    };
    EXPECT_EQ(faceFor(Math::Vector3UVE{1.0F, 0.0F, 0.0F}), 0U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{-1.0F, 0.0F, 0.0F}), 1U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.0F, 1.0F, 0.0F}), 2U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.0F, -1.0F, 0.0F}), 3U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.0F, 0.0F, 1.0F}), 4U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.0F, 0.0F, -1.0F}), 5U);
    // Ties break X, then Y, then Z - the same order the shader's own selector uses.
    EXPECT_EQ(faceFor(Math::Vector3UVE{1.0F, 1.0F, 1.0F}), 0U);
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.0F, 1.0F, 1.0F}), 2U);
    // A near-axis direction still lands on the face it is nearest to.
    EXPECT_EQ(faceFor(Math::Vector3UVE{0.6F, 0.5F, -0.4F}), 0U);
}

TEST(ShadowAtlasManagerUVETest, PointFaceMatrixProjectsTheFaceAxisThroughTheTileCentre) {
    constexpr float range = 12.0F;
    const Math::Vector3UVE position{3.0F, -2.0F, 4.0F};
    const auto table = PointLightShadowFaceTableUVE();
    for (std::size_t face = 0; face < table.size(); ++face) {
        const Math::Matrix4x4UVE matrix =
            ShadowAtlasManagerUVE::ComputePointLightFaceMatrixUVE(position, static_cast<std::uint32_t>(face),
                                                                  range, 0.1F);
        // A point one unit down the face axis must land dead centre in the tile, one unit in front
        // of the light - the exact sample the shader's face selector performs.
        const Math::Vector3UVE target{position.x + table[face].forward.x,
                                      position.y + table[face].forward.y,
                                      position.z + table[face].forward.z};
        const Math::Vector4UVE clip = TransformClipUVE(matrix, target);
        ASSERT_GT(clip.w, 0.0F) << "face " << face;
        EXPECT_NEAR(clip.x / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        EXPECT_NEAR(clip.y / clip.w, 0.0F, 1.0e-5F) << "face " << face;
        const float normalizedDepth = clip.z / clip.w;
        EXPECT_GT(normalizedDepth, 0.0F) << "face " << face;
        EXPECT_LT(normalizedDepth, 1.0F) << "face " << face;

        // The face's own "right" always lands on +X in the tile, never mirrored.
        const Math::Vector3UVE rightTarget{position.x + table[face].forward.x + table[face].right.x,
                                           position.y + table[face].forward.y + table[face].right.y,
                                           position.z + table[face].forward.z + table[face].right.z};
        const Math::Vector4UVE rightClip = TransformClipUVE(matrix, rightTarget);
        ASSERT_GT(rightClip.w, 0.0F) << "face " << face;
        EXPECT_GT(rightClip.x / rightClip.w, 0.0F) << "face " << face;

        // w is the axis distance, which is what the shader derives its depth from - not the euclidean
        // distance to the fragment.
        const Math::Vector4UVE axisClip = TransformClipUVE(matrix, target);
        EXPECT_NEAR(axisClip.w, 1.0F, 1.0e-5F) << "face " << face;
    }
}

TEST(ShadowAtlasManagerUVETest, PointFaceMatrixUsesASquareNinetyDegreeProjection) {
    const Math::Matrix4x4UVE matrix =
        ShadowAtlasManagerUVE::ComputePointLightFaceMatrixUVE({0.0F, 0.0F, 0.0F}, 0U, 10.0F, 0.1F);
    const PointShadowFaceBasisUVE basis = PointLightShadowFaceTableUVE()[0];
    // 90 degrees vertically means tan(fov/2) == 1, so the face's extent at distance d is exactly d:
    // the direction (forward + right + up) must land on the tile's corner, and x == y proves the
    // pixels are square. Reading m[0][0] instead would only measure the basis rotation.
    const Math::Vector3UVE corner{basis.forward.x + basis.right.x + basis.up.x,
                                  basis.forward.y + basis.right.y + basis.up.y,
                                  basis.forward.z + basis.right.z + basis.up.z};
    const Math::Vector4UVE clip = TransformClipUVE(matrix, corner);
    ASSERT_GT(clip.w, 0.0F);
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    EXPECT_NEAR(ndcX, 1.0F, 1.0e-5F);
    EXPECT_NEAR(ndcX, ndcY, 1.0e-6F);
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
