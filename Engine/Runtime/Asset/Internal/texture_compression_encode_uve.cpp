// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/texture_compression_uve.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "basisu_comp.h"
#include "texture_compression_basis_init_uve.h"

namespace UVE::Asset {
namespace {

struct BasisDataDeleterUVE {
    void operator()(void* const data) const noexcept {
        if (data != nullptr) {
            basisu::basis_free_data(data);
        }
    }
};

} // namespace

bool CompressTextureAssetWithBasisUVE(TextureAssetUVE& texture, const TextureCompressionModeUVE mode,
                                      const std::uint8_t quality, const std::uint8_t effort) {
    if (mode == TextureCompressionModeUVE::None) {
        return IsTextureAssetValidUVE(texture);
    }
    if ((mode != TextureCompressionModeUVE::BasisETC1S && mode != TextureCompressionModeUVE::BasisUASTC) ||
        quality > 100U || effort > 10U || texture.payloadEncoding != TexturePayloadEncodingUVE::RawPixels ||
        texture.format != TextureFormatUVE::RGBA8Unorm || !IsTextureAssetValidUVE(texture) ||
        texture.width > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION ||
        texture.height > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION ||
        !Detail::EnsureBasisUniversalInitializedUVE()) {
        return false;
    }

    try {
        const std::size_t levelCount = texture.mipLevels.size() + 1U;
        basisu::vector<basisu::image> sourceImages(levelCount);
        sourceImages[0].init(reinterpret_cast<const std::uint8_t*>(texture.pixels.data()), texture.width,
                             texture.height, 4U);
        for (std::size_t index = 0U; index < texture.mipLevels.size(); ++index) {
            const TextureMipLevelUVE& mip = texture.mipLevels[index];
            sourceImages[index + 1U].init(reinterpret_cast<const std::uint8_t*>(mip.pixels.data()), mip.width,
                                          mip.height, 4U);
        }

        const basist::basis_tex_format basisMode =
            mode == TextureCompressionModeUVE::BasisETC1S ? basist::basis_tex_format::cETC1S
                                                           : basist::basis_tex_format::cUASTC_LDR_4x4;
        std::uint32_t flags = basisu::cFlagKTX2;
        if (texture.colorSpace == TextureColorSpaceUVE::Srgb) {
            flags |= basisu::cFlagSRGB;
        }
        std::size_t compressedSize = 0U;
        std::unique_ptr<void, BasisDataDeleterUVE> compressedData(
            basisu::basis_compress2(basisMode, sourceImages, flags, static_cast<int>(quality),
                                   static_cast<int>(effort), &compressedSize));
        if (compressedData == nullptr || compressedSize == 0U ||
            compressedSize > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }

        TextureAssetUVE candidate;
        candidate.width = texture.width;
        candidate.height = texture.height;
        candidate.format = texture.format;
        candidate.colorSpace = texture.colorSpace;
        candidate.usage = texture.usage;
        candidate.payloadEncoding = TexturePayloadEncodingUVE::BasisUniversalKtx2;
        const auto* const begin = static_cast<const std::byte*>(compressedData.get());
        candidate.basisKtx2Data.assign(begin, begin + compressedSize);
        if (!IsTextureAssetValidUVE(candidate)) {
            return false;
        }
        TextureCompressionInfoUVE info;
        if (!GetTextureCompressionInfoUVE(candidate, info) || info.mipLevels != levelCount) {
            return false;
        }
        texture = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
