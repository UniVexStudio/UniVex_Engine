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

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

/// Resolves texture-specific settings passed through the generic importer interface. Other
/// settings types use the raster-image defaults above.
[[nodiscard]] TextureImportSettingsUVE ResolveTextureImportSettingsUVE(
    const AssetImportSettingsUVE& settings) noexcept;

} // namespace UVE::Asset
