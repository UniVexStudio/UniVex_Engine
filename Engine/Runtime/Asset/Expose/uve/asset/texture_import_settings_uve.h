// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "uve/asset/i_asset_importer_uve.h"
#include "uve/asset/texture_asset_uve.h"

namespace UVE::Asset {

/// CPU filter used to build each generated texture mip level.
enum class TextureMipmapFilterUVE : std::uint8_t {
    Box = 0,     ///< Area-weighted box filter; sRGB RGB channels are averaged in linear space.
    Nearest = 1, ///< Selects the source texel nearest each destination texel center.
};

/// Optional portable Basis Universal storage mode. ETC1S favors smaller assets; UASTC LDR
/// favors higher visual quality and faster device-side transcodes. Both retain a GPU-independent
/// KTX2 payload and defer native BCn/ETC2/ASTC selection to the render device.
enum class TextureCompressionModeUVE : std::uint8_t {
    None = 0,
    BasisETC1S = 1,
    BasisUASTC = 2,
};

/// Import policy for decoded raster images. Painted/color images default to sRGB; normal and
/// packed-data maps should explicitly select Linear and the corresponding usage. Mip generation
/// defaults on, builds the full chain, and can be capped by total level count (0 means no cap).
/// Every option is persisted in TextureAssetUVE output or participates in the import cache key.
struct TextureImportSettingsUVE final : AssetImportSettingsUVE {
    TextureAssetColorSpaceUVE colorSpace = TextureAssetColorSpaceUVE::Srgb;
    TextureUsageUVE usage = TextureUsageUVE::Color;
    bool generateMipmaps = true;
    TextureMipmapFilterUVE mipFilter = TextureMipmapFilterUVE::Box;
    std::uint32_t maxMipLevels = 0U;
    // Compression is opt-in until the editor exposes the quality/memory preview; opting in
    // produces one portable Basis/KTX2 payload, not a platform-specific .uvtex variant.
    TextureCompressionModeUVE compressionMode = TextureCompressionModeUVE::None;
    std::uint8_t compressionQuality = 85U; // Basis quality [0,100]
    std::uint8_t compressionEffort = 5U;   // Basis effort [0,10]

    // Part 4.8 extended texture import options.
    bool premultiplyAlpha = false;
    bool dropOpaqueAlpha = false;
    bool invertGreenChannel = false;
    std::uint32_t maxResolutionClamp = 4096U;
    bool fixAlphaBorder = false;
    std::uint8_t anisotropyLevel = 1U;
    float hdrClampExposure = 0.0F;
    bool streamingEnabled = true;
    std::uint32_t spriteSliceGridWidth = 0U;
    std::uint32_t spriteSliceGridHeight = 0U;
    std::uint16_t ninePatchMarginLeft = 0U;
    std::uint16_t ninePatchMarginRight = 0U;
    std::uint16_t ninePatchMarginTop = 0U;
    std::uint16_t ninePatchMarginBottom = 0U;
    float svgRasterScale = 1.0F;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

/// Estimates the imported texture's GPU/storage footprint in bytes for editor import preview (Part 4.8).
[[nodiscard]] constexpr std::size_t EstimateImportedTextureMemoryBytesUVE(
    const std::uint32_t width, const std::uint32_t height,
    const TextureImportSettingsUVE& settings) noexcept {
    const std::uint32_t clampedWidth =
        (settings.maxResolutionClamp > 0U && width > settings.maxResolutionClamp) ? settings.maxResolutionClamp : width;
    const std::uint32_t clampedHeight =
        (settings.maxResolutionClamp > 0U && height > settings.maxResolutionClamp) ? settings.maxResolutionClamp
                                                                                   : height;
    const std::size_t baseBytes = static_cast<std::size_t>(clampedWidth) * static_cast<std::size_t>(clampedHeight) * 4U;
    const std::size_t mipExpandedBytes = settings.generateMipmaps ? ((baseBytes * 4U) / 3U) : baseBytes;
    if (settings.compressionMode == TextureCompressionModeUVE::BasisETC1S) {
        return mipExpandedBytes / 8U;
    }
    if (settings.compressionMode == TextureCompressionModeUVE::BasisUASTC) {
        return mipExpandedBytes / 4U;
    }
    return mipExpandedBytes;
}

/// Resolves texture-specific settings passed through the generic importer interface. Other
/// settings types use the raster-image defaults above.
[[nodiscard]] TextureImportSettingsUVE ResolveTextureImportSettingsUVE(
    const AssetImportSettingsUVE& settings) noexcept;

} // namespace UVE::Asset
