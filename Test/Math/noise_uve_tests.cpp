// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/math/noise_uve.h"

namespace UVE::Math::Tests {
namespace {

TEST(NoiseUVETest, ValueNoise_IsDeterministicPerSeed) {
    EXPECT_FLOAT_EQ(ValueNoise2DUVE(3, -7, 11U), ValueNoise2DUVE(3, -7, 11U));
    EXPECT_FLOAT_EQ(ValueNoise2DUVE(1.5F, -2.25F, 11U), ValueNoise2DUVE(1.5F, -2.25F, 11U));
    EXPECT_FLOAT_EQ(GradientNoise2DUVE(1.5F, -2.25F, 11U), GradientNoise2DUVE(1.5F, -2.25F, 11U));
}

TEST(NoiseUVETest, ValueNoise_StaysInUnitRangeEverywhereSampled) {
    for (int y = -25; y <= 25; ++y) {
        for (int x = -25; x <= 25; ++x) {
            const float lattice = ValueNoise2DUVE(x, y, 3U);
            EXPECT_GE(lattice, 0.0F);
            EXPECT_LT(lattice, 1.0F);
            const float smooth = ValueNoise2DUVE(static_cast<float>(x) + 0.37F, static_cast<float>(y) - 0.11F, 3U);
            EXPECT_GE(smooth, 0.0F);
            EXPECT_LE(smooth, 1.0F);
        }
    }
}

TEST(NoiseUVETest, SmoothValueNoise_MeetsTheLatticeExactly) {
    // At integer coordinates the fade weights are exactly 0/1, so the smooth sampler must
    // return the lattice value bit-for-bit — continuity with itself, not just smoothness.
    EXPECT_FLOAT_EQ(ValueNoise2DUVE(4.0F, -2.0F, 9U), ValueNoise2DUVE(4, -2, 9U));
    EXPECT_FLOAT_EQ(GradientNoise2DUVE(4.0F, -2.0F, 9U), 0.0F);
}

TEST(NoiseUVETest, GradientNoise_StaysBoundedAndVaries) {
    bool sawPositive = false;
    bool sawNegative = false;
    for (int sample = 0; sample < 2000; ++sample) {
        const float value = GradientNoise2DUVE(static_cast<float>(sample) * 0.731F, static_cast<float>(sample) * -0.377F, 5U);
        EXPECT_GT(value, -1.5F);
        EXPECT_LT(value, 1.5F);
        sawPositive = sawPositive || (value > 0.1F);
        sawNegative = sawNegative || (value < -0.1F);
    }
    EXPECT_TRUE(sawPositive);
    EXPECT_TRUE(sawNegative);
}

TEST(NoiseUVETest, DifferentSeeds_ProduceDifferentFields) {
    bool anyDiffer = false;
    for (int sample = 0; sample < 16; ++sample) {
        anyDiffer = anyDiffer || (ValueNoise2DUVE(sample, sample * 2, 1U) !=
                                  ValueNoise2DUVE(sample, sample * 2, 2U));
    }
    EXPECT_TRUE(anyDiffer);
}

} // namespace
} // namespace UVE::Math::Tests
