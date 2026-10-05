// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/texture_compression_uve.h"

namespace UVE::Asset {

bool CompressTextureAssetWithBasisUVE(TextureAssetUVE& texture, const TextureCompressionModeUVE mode,
                                      const std::uint8_t quality, const std::uint8_t effort) {
    (void)quality;
    (void)effort;
    // Mobile/runtime builds keep the Basis transcoder for loading already-compressed assets, but do
    // not carry the much larger authoring encoder. Asset imports requesting new compression fail
    // explicitly rather than silently writing a raw payload.
    return mode == TextureCompressionModeUVE::None && IsTextureAssetValidUVE(texture);
}

} // namespace UVE::Asset
