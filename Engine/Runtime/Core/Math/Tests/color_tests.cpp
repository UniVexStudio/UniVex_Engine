// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "color.h"

#include <string>

#include "vector3.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-6F;

TEST(ColorTest, DefaultConstruction_IsBlack) {
    constexpr Color color{};
    EXPECT_EQ(color.r, 0.0F);
    EXPECT_EQ(color.g, 0.0F);
    EXPECT_EQ(color.b, 0.0F);
}

TEST(ColorTest, EqualityOperators_CompareAllChannels) {
    constexpr Color a{0.1F, 0.2F, 0.3F};
    constexpr Color b{0.1F, 0.2F, 0.3F};
    constexpr Color c{0.1F, 0.2F, 0.4F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(ColorTest, ToVector3_PreservesChannelsExactly) {
    constexpr Color color{0.1F, 0.2F, 0.3F};

    EXPECT_EQ(ToVector3(color), (Vector3{0.1F, 0.2F, 0.3F}));
}

TEST(ColorTest, DisplayToLinear_KnownValues_MatchSrgbTransfer) {
    EXPECT_FLOAT_EQ(DisplayToLinear(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(DisplayToLinear(1.0F), 1.0F);
    // Power segment: ((0.5 + 0.055) / 1.055)^2.4.
    EXPECT_NEAR(DisplayToLinear(0.5F), 0.2140411F, kEpsilon);
    // Low segment: 0.02 / 12.92.
    EXPECT_NEAR(DisplayToLinear(0.02F), 0.0015480F, kEpsilon);
}

TEST(ColorTest, LinearToDisplay_KnownValues_MatchSrgbTransfer) {
    EXPECT_FLOAT_EQ(LinearToDisplay(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(LinearToDisplay(1.0F), 1.0F);
    EXPECT_NEAR(LinearToDisplay(0.2140411F), 0.5F, kEpsilon);
    EXPECT_NEAR(LinearToDisplay(0.0015480F), 0.02F, kEpsilon);
}

TEST(ColorTest, TransferRoundTrip_DisplayLinearDisplay_IsIdentity) {
    // Including black, white, both piecewise segments, and an HDR value past 1.
    for (const float display : {0.0F, 0.02F, 0.2F, 0.5F, 1.0F, 2.0F}) {
        EXPECT_NEAR(LinearToDisplay(DisplayToLinear(display)), display, kEpsilon);
    }
}

TEST(ColorTest, ColorFromDisplay_MtlPrimaries_MatchHandComputedLinear) {
    // Pins the MTL import math at the math layer: the converter test pins the wiring.
    const Color albedo = ColorFromDisplay(Vector3{0.2F, 0.4F, 0.6F});
    EXPECT_NEAR(albedo.r, 0.0331048F, kEpsilon);
    EXPECT_NEAR(albedo.g, 0.1328683F, kEpsilon);
    EXPECT_NEAR(albedo.b, 0.3185468F, kEpsilon);

    const Color emissive = ColorFromDisplay(Vector3{1.0F, 2.0F, 3.0F});
    EXPECT_NEAR(emissive.r, 1.0F, kEpsilon);
    EXPECT_NEAR(emissive.g, 4.9538458F, 1e-4F);
    EXPECT_NEAR(emissive.b, 12.8298333F, 1e-3F);
}

TEST(ColorTest, DisplayFromColor_RoundTripsThroughColorFromDisplay) {
    const Color color{0.0331048F, 0.5F, 1.0F};
    const Vector3 display = DisplayFromColor(color);
    const Color back = ColorFromDisplay(display);

    EXPECT_NEAR(back.r, color.r, kEpsilon);
    EXPECT_NEAR(back.g, color.g, kEpsilon);
    EXPECT_NEAR(back.b, color.b, kEpsilon);
}

TEST(ColorTest, ToString_FormatsAllThreeChannels) {
    const Color color{1.0F, 0.5F, 0.0F};
    const std::string text = ToString(color);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("0.500000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
