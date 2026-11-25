// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "texture_asset.h"

namespace UVE::Asset {

struct SvgRasterOptions final {
    float rasterScale = 1.0F;
    std::uint32_t fallbackWidth = 128U;
    std::uint32_t fallbackHeight = 128U;
    std::uint32_t maxDimension = 2048U;
    bool antiAlias2x = true;
};

/// Dependency-free SVG vector rasterizer supporting `<svg>`, `viewBox`, `<rect>`, `<circle>`,
/// `<ellipse>`, `<polygon>`, `<polyline>`, `<line>`, and `<path>` (`M/m/L/l/H/h/V/v/Q/q/C/c/Z/z`)
/// with hex/rgb/named fills, strokes, and opacity. Outputs tightly-packed RGBA8 pixels into
/// `outTexture`.
[[nodiscard]] bool RasterizeSvgToTextureAsset(std::string_view svgSource,
                                                 const SvgRasterOptions& options,
                                                 TextureAsset& outTexture);

} // namespace UVE::Asset
