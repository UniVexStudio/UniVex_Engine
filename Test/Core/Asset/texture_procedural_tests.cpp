// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <gtest/gtest.h>

#include "texture_procedural.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] int RedAtUVE(const TextureAssetUVE& texture, const std::uint32_t x, const std::uint32_t y) {
    return static_cast<int>(texture.pixels[(static_cast<std::size_t>(y) * texture.width + x) * 4U]);
}

TEST(TextureProceduralUVETest, TheSameSettingsGiveTheSamePixelsAndAnotherSeedOthers) {
    NoiseTextureSettingsUVE settings;
    settings.width = 64U;
    settings.height = 64U;
    TextureAssetUVE a;
    TextureAssetUVE b;
    ASSERT_TRUE(GenerateNoiseTextureUVE(settings, a));
    ASSERT_TRUE(GenerateNoiseTextureUVE(settings, b));
    EXPECT_EQ(a.pixels, b.pixels);
    EXPECT_EQ(a.mipLevels.size(), 6U);
    settings.seed = 2U;
    TextureAssetUVE c;
    ASSERT_TRUE(GenerateNoiseTextureUVE(settings, c));
    EXPECT_NE(a.pixels, c.pixels);
}

TEST(TextureProceduralUVETest, NoiseTilesWithoutASeam) {
    for (const NoiseTextureTypeUVE type : {NoiseTextureTypeUVE::Perlin, NoiseTextureTypeUVE::Value, NoiseTextureTypeUVE::Cellular}) {
        NoiseTextureSettingsUVE settings;
        settings.width = 128U;
        settings.height = 128U;
        settings.type = type;
        TextureAssetUVE noise;
        ASSERT_TRUE(GenerateNoiseTextureUVE(settings, noise));
        // Across the wrap, neighbours differ no more than neighbours inside do.
        int worstInside = 0;
        int worstAcross = 0;
        for (std::uint32_t y = 0U; y < noise.height; ++y) {
            for (std::uint32_t x = 1U; x < noise.width; ++x) {
                worstInside = std::max(worstInside, std::abs(RedAtUVE(noise, x, y) - RedAtUVE(noise, x - 1U, y)));
            }
            worstAcross = std::max(worstAcross, std::abs(RedAtUVE(noise, 0U, y) - RedAtUVE(noise, noise.width - 1U, y)));
        }
        EXPECT_LE(worstAcross, worstInside + 2) << static_cast<int>(type);
    }
}

TEST(TextureProceduralUVETest, AGradientRunsFromItsFirstColourToItsSecond) {
    GradientTextureSettingsUVE settings;
    settings.width = 100U;
    settings.height = 4U;
    TextureAssetUVE gradient;
    ASSERT_TRUE(GenerateGradientTextureUVE(settings, gradient));
    EXPECT_LT(RedAtUVE(gradient, 0U, 0U), 5);
    EXPECT_GT(RedAtUVE(gradient, 99U, 0U), 250);
    NoiseTextureSettingsUVE tooBig;
    tooBig.width = 5000U;
    TextureAssetUVE refused;
    EXPECT_FALSE(GenerateNoiseTextureUVE(tooBig, refused));
}

} // namespace
} // namespace UVE::Asset
