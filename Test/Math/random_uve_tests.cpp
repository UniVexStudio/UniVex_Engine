// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>

#include <gtest/gtest.h>

#include "uve/math/random_uve.h"

namespace UVE::Math::Tests {
namespace {

TEST(RandomUVETest, FirstOutputs_MatchThePcg32ReferenceVectors) {
    // pcg32_srandom(42, 54), first six outputs — the PCG paper's own test sequence. If this
    // fails, the generator is not PCG32, whatever the structural tests below say.
    RandomUVE random{42U, 54U};
    EXPECT_EQ(random.NextU32UVE(), 0xA15C02B7U);
    EXPECT_EQ(random.NextU32UVE(), 0x7B47F409U);
    EXPECT_EQ(random.NextU32UVE(), 0xBA1D3330U);
    EXPECT_EQ(random.NextU32UVE(), 0x83D2F293U);
    EXPECT_EQ(random.NextU32UVE(), 0xBFA4784BU);
    EXPECT_EQ(random.NextU32UVE(), 0xCBED606EU);
}

TEST(RandomUVETest, SameSeed_ReproducesTheSameSequence) {
    RandomUVE left{1234U};
    RandomUVE right{1234U};
    for (int draw = 0; draw < 100; ++draw) {
        EXPECT_EQ(left.NextU64UVE(), right.NextU64UVE());
    }
}

TEST(RandomUVETest, DifferentStreams_DivergeImmediately) {
    RandomUVE left{42U, 1U};
    RandomUVE right{42U, 2U};
    EXPECT_NE(left.NextU32UVE(), right.NextU32UVE());
}

TEST(RandomUVETest, NextFloat_StaysInUnitRange) {
    RandomUVE random{7U};
    for (int draw = 0; draw < 10000; ++draw) {
        const float value = random.NextFloatUVE();
        EXPECT_GE(value, 0.0F);
        EXPECT_LT(value, 1.0F);
    }
}

TEST(RandomUVETest, IntRange_StaysInBoundsAndReachesBothEnds) {
    RandomUVE random{99U};
    bool sawLow = false;
    bool sawHigh = false;
    for (int draw = 0; draw < 10000; ++draw) {
        const std::int32_t value = random.RangeUVE(-5, 6);
        EXPECT_GE(value, -5);
        EXPECT_LT(value, 6);
        sawLow = sawLow || (value == -5);
        sawHigh = sawHigh || (value == 5);
    }
    EXPECT_TRUE(sawLow);
    EXPECT_TRUE(sawHigh);
}

TEST(RandomUVETest, Chance_ExtremesAreDeterministic) {
    RandomUVE random{5U};
    for (int draw = 0; draw < 100; ++draw) {
        EXPECT_FALSE(random.ChanceUVE(0.0F));
        EXPECT_TRUE(random.ChanceUVE(1.0F));
    }
}

} // namespace
} // namespace UVE::Math::Tests
