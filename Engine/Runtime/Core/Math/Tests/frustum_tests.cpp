// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "frustum.h"

#include <cmath>
#include <limits>
#include <numbers>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

// A 90-degree-vertical-FOV, square-aspect, near=1/far=100 frustum looking down -Z from the
// world origin (identity view) — shared by every test below.
[[nodiscard]] Frustum MakeTestFrustum() {
    const Matrix4x4 projection =
        Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Matrix4x4 view = Matrix4x4::ViewFromPositionAndRotation(Vector3{0.0F, 0.0F, 0.0F}, Quaternion{});
    return Frustum::FromViewProjection(projection * view);
}

TEST(FrustumTest, FromViewProjection_PreservesExtremeFiniteAxisPlaneNormals) {
    const float maximum = std::numeric_limits<float>::max();
    Matrix4x4 viewProjection{};
    viewProjection.m[0][0] = maximum;
    viewProjection.m[1][1] = maximum;
    viewProjection.m[2][2] = maximum;
    viewProjection.m[3][3] = 1.0F;

    const Frustum frustum = Frustum::FromViewProjection(viewProjection);

    EXPECT_TRUE(std::isfinite(frustum.planes[0].normal.x));
    EXPECT_FLOAT_EQ(frustum.planes[0].normal.x, 1.0F);
    EXPECT_FLOAT_EQ(frustum.planes[1].normal.x, -1.0F);
    EXPECT_FLOAT_EQ(frustum.planes[2].normal.y, 1.0F);
    EXPECT_FLOAT_EQ(frustum.planes[3].normal.y, -1.0F);
    EXPECT_FLOAT_EQ(frustum.planes[4].normal.z, 1.0F);
    EXPECT_FLOAT_EQ(frustum.planes[5].normal.z, -1.0F);
}

TEST(FrustumTest, Intersects_BoxDirectlyAheadWithinRange_IsVisible) {
    const Frustum frustum = MakeTestFrustum();
    const Aabb box = Aabb::FromCenterExtents(Vector3{0.0F, 0.0F, -10.0F}, Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_TRUE(frustum.Intersects(box));
}

TEST(FrustumTest, Intersects_BoxBehindCamera_IsNotVisible) {
    const Frustum frustum = MakeTestFrustum();
    const Aabb box = Aabb::FromCenterExtents(Vector3{0.0F, 0.0F, 10.0F}, Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_FALSE(frustum.Intersects(box));
}

TEST(FrustumTest, Intersects_BoxFarOffToTheSide_IsNotVisible) {
    const Frustum frustum = MakeTestFrustum();
    const Aabb box = Aabb::FromCenterExtents(Vector3{1000.0F, 0.0F, -10.0F}, Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_FALSE(frustum.Intersects(box));
}

TEST(FrustumTest, Intersects_BoxBeyondFarPlane_IsNotVisible) {
    const Frustum frustum = MakeTestFrustum();
    const Aabb box = Aabb::FromCenterExtents(Vector3{0.0F, 0.0F, -1000.0F}, Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_FALSE(frustum.Intersects(box));
}

TEST(FrustumTest, Intersects_BoxCloserThanNearPlane_IsNotVisible) {
    const Frustum frustum = MakeTestFrustum();
    const Aabb box = Aabb::FromCenterExtents(Vector3{0.0F, 0.0F, -0.1F}, Vector3{0.01F, 0.01F, 0.01F});

    EXPECT_FALSE(frustum.Intersects(box));
}

} // namespace
} // namespace UVE::Math::Tests
