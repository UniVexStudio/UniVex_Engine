// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/color_uve.h"

#include <string>

#include "uve/math/vector3_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-6F;

TEST(ColorUVETest, DefaultConstruction_IsBlack) {
    constexpr ColorUVE color{};
    EXPECT_EQ(color.r, 0.0F);
    EXPECT_EQ(color.g, 0.0F);
    EXPECT_EQ(color.b, 0.0F);
}

TEST(ColorUVETest, EqualityOperators_CompareAllChannels) {
    constexpr ColorUVE a{0.1F, 0.2F, 0.3F};
    constexpr ColorUVE b{0.1F, 0.2F, 0.3F};
    constexpr ColorUVE c{0.1F, 0.2F, 0.4F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(ColorUVETest, ToVector3UVE_PreservesChannelsExactly) {
    constexpr ColorUVE color{0.1F, 0.2F, 0.3F};

    EXPECT_EQ(ToVector3UVE(color), (Vector3UVE{0.1F, 0.2F, 0.3F}));
}

TEST(ColorUVETest, DisplayToLinearUVE_KnownValues_MatchSrgbTransfer) {
    EXPECT_FLOAT_EQ(DisplayToLinearUVE(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(DisplayToLinearUVE(1.0F), 1.0F);
    // Power segment: ((0.5 + 0.055) / 1.055)^2.4.
    EXPECT_NEAR(DisplayToLinearUVE(0.5F), 0.2140411F, kEpsilon);
    // Low segment: 0.02 / 12.92.
    EXPECT_NEAR(DisplayToLinearUVE(0.02F), 0.0015480F, kEpsilon);
}

TEST(ColorUVETest, LinearToDisplayUVE_KnownValues_MatchSrgbTransfer) {
    EXPECT_FLOAT_EQ(LinearToDisplayUVE(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(LinearToDisplayUVE(1.0F), 1.0F);
    EXPECT_NEAR(LinearToDisplayUVE(0.2140411F), 0.5F, kEpsilon);
    EXPECT_NEAR(LinearToDisplayUVE(0.0015480F), 0.02F, kEpsilon);
}

TEST(ColorUVETest, TransferRoundTrip_DisplayLinearDisplay_IsIdentity) {
    // Including black, white, both piecewise segments, and an HDR value past 1.
    for (const float display : {0.0F, 0.02F, 0.2F, 0.5F, 1.0F, 2.0F}) {
        EXPECT_NEAR(LinearToDisplayUVE(DisplayToLinearUVE(display)), display, kEpsilon);
    }
}

TEST(ColorUVETest, ColorFromDisplay_MtlPrimaries_MatchHandComputedLinear) {
    // Pins the MTL import math at the math layer: the converter test pins the wiring.
    const ColorUVE albedo = ColorFromDisplayUVE(Vector3UVE{0.2F, 0.4F, 0.6F});
    EXPECT_NEAR(albedo.r, 0.0331048F, kEpsilon);
    EXPECT_NEAR(albedo.g, 0.1328683F, kEpsilon);
    EXPECT_NEAR(albedo.b, 0.3185468F, kEpsilon);

    const ColorUVE emissive = ColorFromDisplayUVE(Vector3UVE{1.0F, 2.0F, 3.0F});
    EXPECT_NEAR(emissive.r, 1.0F, kEpsilon);
    EXPECT_NEAR(emissive.g, 4.9538458F, 1e-4F);
    EXPECT_NEAR(emissive.b, 12.8298333F, 1e-3F);
}

TEST(ColorUVETest, DisplayFromColor_RoundTripsThroughColorFromDisplay) {
    const ColorUVE color{0.0331048F, 0.5F, 1.0F};
    const Vector3UVE display = DisplayFromColorUVE(color);
    const ColorUVE back = ColorFromDisplayUVE(display);

    EXPECT_NEAR(back.r, color.r, kEpsilon);
    EXPECT_NEAR(back.g, color.g, kEpsilon);
    EXPECT_NEAR(back.b, color.b, kEpsilon);
}

TEST(ColorUVETest, ToStringUVE_FormatsAllThreeChannels) {
    const ColorUVE color{1.0F, 0.5F, 0.0F};
    const std::string text = ToStringUVE(color);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("0.500000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
