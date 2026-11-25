// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "plane.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(PlaneTest, FromNormalAndPoint_PointUsedToConstructIt_HasZeroDistance) {
    const Plane plane = Plane::FromNormalAndPoint(Vector3{0.0F, 1.0F, 0.0F}, Vector3{5.0F, 3.0F, -2.0F});

    EXPECT_NEAR(plane.GetSignedDistance(Vector3{5.0F, 3.0F, -2.0F}), 0.0F, kEpsilon);
}

TEST(PlaneTest, ExtremeFiniteDotCancellation_PreservesPlaneConstructionAndDistance) {
    const float maximum = std::numeric_limits<float>::max();
    const float diagonal = 0.57735026919F;
    const Vector3 normal{diagonal, diagonal, -diagonal};
    const Vector3 point{maximum, maximum, maximum};
    const Plane plane = Plane::FromNormalAndPoint(normal, point);

    EXPECT_TRUE(std::isfinite(plane.distance));
    EXPECT_TRUE(std::isfinite(plane.GetSignedDistance(point)));
    EXPECT_LT(std::fabs(plane.GetSignedDistance(point)), maximum * 1.0e-5F);
}

TEST(PlaneTest, GetSignedDistance_PointOnNormalSide_IsPositive) {
    const Plane plane = Plane::FromNormalAndPoint(Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(plane.GetSignedDistance(Vector3{0.0F, 10.0F, 0.0F}), 10.0F, kEpsilon);
}

TEST(PlaneTest, GetSignedDistance_PointOnOppositeSide_IsNegative) {
    const Plane plane = Plane::FromNormalAndPoint(Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(plane.GetSignedDistance(Vector3{0.0F, -4.0F, 0.0F}), -4.0F, kEpsilon);
}

} // namespace
} // namespace UVE::Math::Tests
