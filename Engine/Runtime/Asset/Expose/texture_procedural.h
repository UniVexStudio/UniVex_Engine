// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "texture_asset.h"
#include "vector3.h"

namespace UVE::Asset {

/// The pattern a noise texture is made of.
enum class NoiseTextureTypeUVE : std::uint8_t {
    /// Smooth gradient noise (clouds, dirt, terrain).
    Perlin = 0,
    /// Blocky-soft value noise (a cheaper, softer look).
    Value = 1,
    /// Cells: the distance to the nearest scattered point (stone, scales, foam).
    Cellular = 2,
};

/// How a noise texture is made: its size, pattern, scale and layering, and the two colours it runs
/// between. Seamless tiles (the right edge meets the left, the bottom the top).
struct NoiseTextureSettingsUVE final {
    std::uint32_t width = 512U;
    std::uint32_t height = 512U;
    NoiseTextureTypeUVE type = NoiseTextureTypeUVE::Perlin;
    std::uint32_t seed = 1U;
    /// Features across the texture (whole numbers keep it seamless).
    std::uint32_t frequency = 4U;
    /// Layers of finer detail on top, each `lacunarity` times finer and `gain` as strong.
    std::uint32_t octaves = 4U;
    std::uint32_t lacunarity = 2U;
    float gain = 0.5F;
    Math::Vector3UVE colorA{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE colorB{1.0F, 1.0F, 1.0F};
    /// Make a tangent-space normal map from the noise as height instead of a colour map.
    bool asNormalMap = false;
    float normalStrength = 2.0F;
};

/// A straight or radial blend between two colours.
struct GradientTextureSettingsUVE final {
    std::uint32_t width = 256U;
    std::uint32_t height = 256U;
    /// From the centre outwards, instead of left to right.
    bool radial = false;
    Math::Vector3UVE colorA{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE colorB{1.0F, 1.0F, 1.0F};
};

/// Bakes a noise texture: RGBA8 with a full mip chain, sRGB colour (or a linear normal map). The
/// same settings give the same pixels. False for a size of 0 or over 4096, or no octave.
[[nodiscard]] bool GenerateNoiseTextureUVE(const NoiseTextureSettingsUVE& settings, TextureAssetUVE& outTexture);

/// Bakes a gradient texture: RGBA8 sRGB with a full mip chain. False for a size of 0 or over 4096.
[[nodiscard]] bool GenerateGradientTextureUVE(const GradientTextureSettingsUVE& settings, TextureAssetUVE& outTexture);

} // namespace UVE::Asset
