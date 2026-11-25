// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "curves.h"

namespace UVE::Math::Tests {
namespace {

TEST(CurvesTest, CubicBezier_HitsItsEndpoints) {
    const Vector2 p0{0.0F, 0.0F};
    const Vector2 p1{0.0F, 1.0F};
    const Vector2 p2{1.0F, 1.0F};
    const Vector2 p3{1.0F, 0.0F};
    EXPECT_EQ(CubicBezier(p0, p1, p2, p3, 0.0F), p0);
    EXPECT_EQ(CubicBezier(p0, p1, p2, p3, 1.0F), p3);
    const Vector2 mid = CubicBezier(p0, p1, p2, p3, 0.5F);
    EXPECT_FLOAT_EQ(mid.x, 0.5F);
    EXPECT_FLOAT_EQ(mid.y, 0.75F);
}

TEST(CurvesTest, CatmullRom_InterpolatesItsSegmentEndpoints) {
    const Vector3 p0{0.0F, 0.0F, 0.0F};
    const Vector3 p1{1.0F, 0.0F, 0.0F};
    const Vector3 p2{2.0F, 0.0F, 0.0F};
    const Vector3 p3{3.0F, 0.0F, 0.0F};
    EXPECT_EQ(CatmullRom(p0, p1, p2, p3, 0.0F), p1);
    EXPECT_EQ(CatmullRom(p0, p1, p2, p3, 1.0F), p2);
    // Uniform spacing on a straight line: the midpoint is exact.
    EXPECT_EQ(CatmullRom(p0, p1, p2, p3, 0.5F), (Vector3{1.5F, 0.0F, 0.0F}));
}

TEST(CurvesTest, CatmullRom_ChainedSegmentsShareTangents) {
    // A symmetric bump: sampling the left segment's end tangent and the right segment's
    // start tangent must agree (C1 through the shared point), checked numerically.
    const Vector2 p0{0.0F, 0.0F};
    const Vector2 p1{1.0F, 1.0F};
    const Vector2 p2{2.0F, 1.0F};
    const Vector2 p3{3.0F, 0.0F};
    const Vector2 leftEnd = CatmullRom(p0, p1, p2, p3, 1.0F);
    const Vector2 leftBefore = CatmullRom(p0, p1, p2, p3, 0.999F);
    const Vector2 rightStart = CatmullRom(p1, p2, p3, p3, 0.0F);
    const Vector2 rightAfter = CatmullRom(p1, p2, p3, p3, 0.001F);
    EXPECT_EQ(leftEnd, rightStart);
    const Vector2 leftTangent{(leftEnd.x - leftBefore.x) * 1000.0F,
                                 (leftEnd.y - leftBefore.y) * 1000.0F};
    const Vector2 rightTangent{(rightAfter.x - rightStart.x) * 1000.0F,
                                  (rightAfter.y - rightStart.y) * 1000.0F};
    EXPECT_NEAR(leftTangent.x, rightTangent.x, 1e-2F);
    EXPECT_NEAR(leftTangent.y, rightTangent.y, 1e-2F);
}

} // namespace
} // namespace UVE::Math::Tests
