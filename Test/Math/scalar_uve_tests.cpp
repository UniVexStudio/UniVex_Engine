// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/scalar_uve.h"

#include <numbers>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(ScalarUVETest, PiConstants_MatchStdNumbers) {
    EXPECT_FLOAT_EQ(kPiUVE, std::numbers::pi_v<float>);
    EXPECT_DOUBLE_EQ(kPiDoubleUVE, std::numbers::pi_v<double>);
}

TEST(ScalarUVETest, DegToRad_RoundTripsThroughRadToDeg) {
    EXPECT_FLOAT_EQ(DegToRadUVE(180.0F), kPiUVE);
    EXPECT_FLOAT_EQ(RadToDegUVE(kPiUVE), 180.0F);
    EXPECT_DOUBLE_EQ(DegToRadUVE(90.0), kPiDoubleUVE / 2.0);
    EXPECT_DOUBLE_EQ(RadToDegUVE(DegToRadUVE(45.0)), 45.0);
}

TEST(ScalarUVETest, Lerp_HitsEndpointsAndMidpoint) {
    EXPECT_FLOAT_EQ(LerpUVE(10.0F, 20.0F, 0.0F), 10.0F);
    EXPECT_FLOAT_EQ(LerpUVE(10.0F, 20.0F, 1.0F), 20.0F);
    EXPECT_FLOAT_EQ(LerpUVE(10.0F, 20.0F, 0.5F), 15.0F);
    EXPECT_DOUBLE_EQ(LerpUVE(-1.0, 1.0, 0.25), -0.5);
}

TEST(ScalarUVETest, Clamp_PinsOutsideAndPassesInside) {
    EXPECT_EQ(ClampUVE(-5, 0, 10), 0);
    EXPECT_EQ(ClampUVE(5, 0, 10), 5);
    EXPECT_EQ(ClampUVE(50, 0, 10), 10);
    EXPECT_FLOAT_EQ(ClampUVE(-1.0F, 0.0F, 1.0F), 0.0F);
    EXPECT_FLOAT_EQ(ClampUVE(0.5F, 0.0F, 1.0F), 0.5F);
}

TEST(ScalarUVETest, SmoothStep_EasesAcrossTheSpan) {
    EXPECT_FLOAT_EQ(SmoothStepUVE(0.0F, 1.0F, -1.0F), 0.0F);
    EXPECT_FLOAT_EQ(SmoothStepUVE(0.0F, 1.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(SmoothStepUVE(0.0F, 1.0F, 0.5F), 0.5F);
    EXPECT_FLOAT_EQ(SmoothStepUVE(0.0F, 1.0F, 1.0F), 1.0F);
    EXPECT_FLOAT_EQ(SmoothStepUVE(0.0F, 1.0F, 2.0F), 1.0F);
}

TEST(ScalarUVETest, ApproximatelyEqual_ComparesAgainstExplicitEpsilon) {
    EXPECT_TRUE(ApproximatelyEqualUVE(1.0F, 1.0F, 0.0F));
    EXPECT_TRUE(ApproximatelyEqualUVE(1.0F, 1.05F, 0.1F));
    EXPECT_FALSE(ApproximatelyEqualUVE(1.0F, 1.5F, 0.1F));
    EXPECT_TRUE(ApproximatelyEqualUVE(-2.0, -2.0, 1e-12));
}

TEST(ScalarUVETest, EverythingIsConstexprUsable) {
    static_assert(DegToRadUVE(180.0) > 3.14 && DegToRadUVE(180.0) < 3.15);
    static_assert(LerpUVE(0.0F, 10.0F, 0.5F) == 5.0F);
    static_assert(SmoothStepUVE(0.0F, 1.0F, 0.5F) == 0.5F);
    static_assert(ApproximatelyEqualUVE(1.0F, 1.0F, 0.0F));
    static_assert(ClampUVE(7, 0, 10) == 7);
}

} // namespace
} // namespace UVE::Math::Tests
