// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "texture_compression.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "basisu_comp.h"
#include "texture_compression_basis_init.h"

namespace UVE::Asset {
namespace {

struct BasisDataDeleter {
    void operator()(void* const data) const noexcept {
        if (data != nullptr) {
            basisu::basis_free_data(data);
        }
    }
};

} // namespace

bool CompressTextureAssetWithBasis(TextureAsset& texture, const TextureCompressionMode mode,
                                      const std::uint8_t quality, const std::uint8_t effort) {
    if (mode == TextureCompressionMode::None) {
        return IsTextureAssetValid(texture);
    }
    if ((mode != TextureCompressionMode::BasisETC1S && mode != TextureCompressionMode::BasisUASTC) ||
        quality > 100U || effort > 10U || texture.payloadEncoding != TexturePayloadEncoding::RawPixels ||
        texture.format != TextureAssetFormat::RGBA8Unorm || !IsTextureAssetValid(texture) ||
        texture.width > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION ||
        texture.height > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION ||
        !Detail::EnsureBasisUniversalInitialized()) {
        return false;
    }

    try {
        const std::size_t levelCount = texture.mipLevels.size() + 1U;
        basisu::vector<basisu::image> sourceImages(levelCount);
        sourceImages[0].init(reinterpret_cast<const std::uint8_t*>(texture.pixels.data()), texture.width,
                             texture.height, 4U);
        for (std::size_t index = 0U; index < texture.mipLevels.size(); ++index) {
            const TextureMipLevel& mip = texture.mipLevels[index];
            sourceImages[index + 1U].init(reinterpret_cast<const std::uint8_t*>(mip.pixels.data()), mip.width,
                                          mip.height, 4U);
        }

        const basist::basis_tex_format basisMode =
            mode == TextureCompressionMode::BasisETC1S ? basist::basis_tex_format::cETC1S
                                                           : basist::basis_tex_format::cUASTC_LDR_4x4;
        std::uint32_t flags = basisu::cFlagKTX2;
        if (texture.colorSpace == TextureAssetColorSpace::Srgb) {
            flags |= basisu::cFlagSRGB;
        }
        std::size_t compressedSize = 0U;
        std::unique_ptr<void, BasisDataDeleter> compressedData(
            basisu::basis_compress2(basisMode, sourceImages, flags, static_cast<int>(quality),
                                   static_cast<int>(effort), &compressedSize));
        if (compressedData == nullptr || compressedSize == 0U ||
            compressedSize > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }

        TextureAsset candidate;
        candidate.width = texture.width;
        candidate.height = texture.height;
        candidate.format = texture.format;
        candidate.colorSpace = texture.colorSpace;
        candidate.usage = texture.usage;
        candidate.payloadEncoding = TexturePayloadEncoding::BasisUniversalKtx2;
        const auto* const begin = static_cast<const std::byte*>(compressedData.get());
        candidate.basisKtx2Data.assign(begin, begin + compressedSize);
        if (!IsTextureAssetValid(candidate)) {
            return false;
        }
        TextureCompressionInfo info;
        if (!GetTextureCompressionInfo(candidate, info) || info.mipLevels != levelCount) {
            return false;
        }
        texture = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
