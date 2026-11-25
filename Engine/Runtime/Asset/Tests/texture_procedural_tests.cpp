// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <gtest/gtest.h>

#include "texture_procedural.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] int RedAt(const TextureAsset& texture, const std::uint32_t x, const std::uint32_t y) {
    return static_cast<int>(texture.pixels[(static_cast<std::size_t>(y) * texture.width + x) * 4U]);
}

TEST(TextureProceduralTest, TheSameSettingsGiveTheSamePixelsAndAnotherSeedOthers) {
    NoiseTextureSettings settings;
    settings.width = 64U;
    settings.height = 64U;
    TextureAsset a;
    TextureAsset b;
    ASSERT_TRUE(GenerateNoiseTexture(settings, a));
    ASSERT_TRUE(GenerateNoiseTexture(settings, b));
    EXPECT_EQ(a.pixels, b.pixels);
    EXPECT_EQ(a.mipLevels.size(), 6U);
    settings.seed = 2U;
    TextureAsset c;
    ASSERT_TRUE(GenerateNoiseTexture(settings, c));
    EXPECT_NE(a.pixels, c.pixels);
}

TEST(TextureProceduralTest, NoiseTilesWithoutASeam) {
    for (const NoiseTextureType type : {NoiseTextureType::Perlin, NoiseTextureType::Value, NoiseTextureType::Cellular}) {
        NoiseTextureSettings settings;
        settings.width = 128U;
        settings.height = 128U;
        settings.type = type;
        TextureAsset noise;
        ASSERT_TRUE(GenerateNoiseTexture(settings, noise));
        // Across the wrap, neighbours differ no more than neighbours inside do.
        int worstInside = 0;
        int worstAcross = 0;
        for (std::uint32_t y = 0U; y < noise.height; ++y) {
            for (std::uint32_t x = 1U; x < noise.width; ++x) {
                worstInside = std::max(worstInside, std::abs(RedAt(noise, x, y) - RedAt(noise, x - 1U, y)));
            }
            worstAcross = std::max(worstAcross, std::abs(RedAt(noise, 0U, y) - RedAt(noise, noise.width - 1U, y)));
        }
        EXPECT_LE(worstAcross, worstInside + 2) << static_cast<int>(type);
    }
}

TEST(TextureProceduralTest, AGradientRunsFromItsFirstColourToItsSecond) {
    GradientTextureSettings settings;
    settings.width = 100U;
    settings.height = 4U;
    TextureAsset gradient;
    ASSERT_TRUE(GenerateGradientTexture(settings, gradient));
    EXPECT_LT(RedAt(gradient, 0U, 0U), 5);
    EXPECT_GT(RedAt(gradient, 99U, 0U), 250);
    NoiseTextureSettings tooBig;
    tooBig.width = 5000U;
    TextureAsset refused;
    EXPECT_FALSE(GenerateNoiseTexture(tooBig, refused));
}

} // namespace
} // namespace UVE::Asset
