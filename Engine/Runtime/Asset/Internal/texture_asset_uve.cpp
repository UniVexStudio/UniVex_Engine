// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_asset_uve.h"
#include <utility>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/bmp_metadata_uve.h"
#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/tga_metadata_uve.h"
#include "uve/asset/texture_mipmap_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Asset {
namespace {

// The original texture payload had no discriminator. Version 1 added import metadata, version 2
// added raw mip levels, and version 3 adds a storage-encoding tag plus portable Basis/KTX2 bytes.
constexpr std::array<char, 4U> kTexturePayloadMagicUVE{'U', 'V', 'T', 'X'};
constexpr std::array<std::uint8_t, 12U> kKtx2IdentifierUVE{
    0xABU, 0x4BU, 0x54U, 0x58U, 0x20U, 0x32U, 0x30U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::uint32_t kTexturePayloadMetadataVersionUVE = 1U;
constexpr std::uint32_t kTexturePayloadMipmapVersionUVE = 2U;
constexpr std::uint32_t kTexturePayloadVersionUVE = 3U;

[[nodiscard]] bool IsTextureColorSpaceValidUVE(const TextureAssetColorSpaceUVE colorSpace) noexcept {
    switch (colorSpace) {
        case TextureAssetColorSpaceUVE::Linear:
        case TextureAssetColorSpaceUVE::Srgb:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsTextureUsageValidUVE(const TextureUsageUVE usage) noexcept {
    switch (usage) {
        case TextureUsageUVE::Generic:
        case TextureUsageUVE::Color:
        case TextureUsageUVE::Normal:
        case TextureUsageUVE::Data:
        case TextureUsageUVE::Hdr:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsTexturePayloadEncodingValidUVE(const TexturePayloadEncodingUVE encoding) noexcept {
    switch (encoding) {
        case TexturePayloadEncodingUVE::RawPixels:
        case TexturePayloadEncodingUVE::BasisUniversalKtx2:
            return true;
    }
    return false;
}

[[nodiscard]] bool HasVersionedTexturePayloadMagicUVE(const std::vector<std::byte>& payload) noexcept {
    return payload.size() >= kTexturePayloadMagicUVE.size() &&
           std::memcmp(payload.data(), kTexturePayloadMagicUVE.data(), kTexturePayloadMagicUVE.size()) == 0;
}

[[nodiscard]] bool CalculateExpectedPixelBytesUVE(const std::uint32_t width, const std::uint32_t height,
                                                   const TextureAssetFormatUVE format,
                                                   std::uint64_t& outExpectedBytes) noexcept {
    if (width == 0U || height == 0U) {
        return false;
    }

    const std::uint32_t bytesPerPixel = BytesPerPixelUVE(format);
    if (bytesPerPixel == 0U) {
        return false;
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel) {
        return false;
    }

    outExpectedBytes = pixelCount * bytesPerPixel;
    return outExpectedBytes <= std::numeric_limits<std::size_t>::max();
}

[[nodiscard]] std::uint32_t MaximumMipLevelCountUVE(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return 0U;
    }
    std::uint32_t levelCount = 1U;
    while (width > 1U || height > 1U) {
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
        ++levelCount;
    }
    return levelCount;
}

[[nodiscard]] bool IsBasisKtx2PayloadStructurallyValidUVE(const TextureAssetUVE& texture) noexcept {
    const std::vector<std::byte>& bytes = texture.basisKtx2Data;
    constexpr std::size_t kKtx2HeaderSizeUVE = 80U;
    constexpr std::size_t kKtx2LevelIndexEntrySizeUVE = 24U;
    if (bytes.size() < kKtx2HeaderSizeUVE || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    for (std::size_t index = 0U; index < kKtx2IdentifierUVE.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[index]) != kKtx2IdentifierUVE[index]) {
            return false;
        }
    }

    std::size_t offset = kKtx2IdentifierUVE.size();
    std::uint32_t vkFormat = 0U;
    std::uint32_t typeSize = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t depth = 0U;
    std::uint32_t layerCount = 0U;
    std::uint32_t faceCount = 0U;
    std::uint32_t levelCount = 0U;
    std::uint32_t supercompressionScheme = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(bytes, offset, vkFormat) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, typeSize) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, width) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, height) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, depth) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, layerCount) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, faceCount) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, levelCount) ||
        !Utilities::ReadUint32LeFromBufferUVE(bytes, offset, supercompressionScheme)) {
        return false;
    }

    const std::uint32_t maximumMipCount = MaximumMipLevelCountUVE(texture.width, texture.height);
    if (vkFormat != 0U || typeSize != 1U || width != texture.width || height != texture.height ||
        depth != 0U || layerCount != 0U || faceCount != 1U || levelCount == 0U ||
        levelCount > maximumMipCount || (supercompressionScheme != 0U && supercompressionScheme != 1U)) {
        return false;
    }
    if (std::cmp_greater(levelCount, (std::numeric_limits<std::size_t>::max() - kKtx2HeaderSizeUVE) /
                         kKtx2LevelIndexEntrySizeUVE)) {
        return false;
    }
    const std::size_t levelIndexEnd = kKtx2HeaderSizeUVE +
                                      static_cast<std::size_t>(levelCount) * kKtx2LevelIndexEntrySizeUVE;
    if (levelIndexEnd > bytes.size()) {
        return false;
    }

    offset = kKtx2HeaderSizeUVE;
    for (std::uint32_t level = 0U; level < levelCount; ++level) {
        std::uint64_t levelOffset = 0U;
        std::uint64_t levelLength = 0U;
        std::uint64_t uncompressedLength = 0U;
        if (!Utilities::ReadUint64LeFromBufferUVE(bytes, offset, levelOffset) ||
            !Utilities::ReadUint64LeFromBufferUVE(bytes, offset, levelLength) ||
            !Utilities::ReadUint64LeFromBufferUVE(bytes, offset, uncompressedLength)) {
            return false;
        }
        // KTX2 BasisLZ (ETC1S) requires uncompressedByteLength == 0; uncompressed UASTC
        // levels require it to equal byteLength.
        const bool uncompressedLengthMatchesScheme =
            supercompressionScheme == 1U ? uncompressedLength == 0U : uncompressedLength == levelLength;
        if (levelOffset < levelIndexEnd || levelLength == 0U || !uncompressedLengthMatchesScheme ||
            levelOffset > bytes.size() || levelLength > bytes.size() - levelOffset) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ReadPixelBytesUVE(const std::vector<std::byte>& payload, std::size_t& offset,
                                     const std::uint64_t byteCount,
                                     std::vector<std::byte>& outPixels) {
    if (byteCount > std::numeric_limits<std::size_t>::max() || offset > payload.size() ||
        byteCount > payload.size() - offset) {
        return false;
    }
    const std::size_t pixelBytes = static_cast<std::size_t>(byteCount);
    const auto begin = payload.begin() + static_cast<std::vector<std::byte>::difference_type>(offset);
    outPixels.assign(begin, begin + static_cast<std::vector<std::byte>::difference_type>(pixelBytes));
    offset += pixelBytes;
    return true;
}

} // namespace

std::uint32_t BytesPerPixelUVE(const TextureAssetFormatUVE format) noexcept {
    switch (format) {
        case TextureAssetFormatUVE::RGBA8Unorm:
            return 4U;
        case TextureAssetFormatUVE::RGBA16Float:
            return 8U;
    }
    return 0U;
}

bool IsTextureAssetMetadataValidUVE(const TextureAssetUVE& texture) noexcept {
    return BytesPerPixelUVE(texture.format) != 0U && IsTextureColorSpaceValidUVE(texture.colorSpace) &&
           IsTextureUsageValidUVE(texture.usage) &&
           (texture.colorSpace != TextureAssetColorSpaceUVE::Srgb ||
            texture.format == TextureAssetFormatUVE::RGBA8Unorm);
}

bool IsTextureAssetValidUVE(const TextureAssetUVE& texture) noexcept {
    if (!IsTextureAssetMetadataValidUVE(texture) || !IsTexturePayloadEncodingValidUVE(texture.payloadEncoding)) {
        return false;
    }
    if (texture.payloadEncoding == TexturePayloadEncodingUVE::BasisUniversalKtx2) {
        return texture.format == TextureAssetFormatUVE::RGBA8Unorm && texture.pixels.empty() &&
               texture.mipLevels.empty() && IsBasisKtx2PayloadStructurallyValidUVE(texture);
    }
    if (!texture.basisKtx2Data.empty()) {
        return false;
    }

    std::uint64_t expectedPixelBytes = 0U;
    if (!CalculateExpectedPixelBytesUVE(texture.width, texture.height, texture.format, expectedPixelBytes) ||
        expectedPixelBytes != texture.pixels.size()) {
        return false;
    }

    const std::uint32_t maximumMipCount = MaximumMipLevelCountUVE(texture.width, texture.height);
    if (texture.mipLevels.size() > static_cast<std::size_t>(maximumMipCount - 1U)) {
        return false;
    }
    std::uint32_t expectedWidth = texture.width;
    std::uint32_t expectedHeight = texture.height;
    for (const TextureMipLevelUVE& mipLevel : texture.mipLevels) {
        expectedWidth = std::max(1U, expectedWidth / 2U);
        expectedHeight = std::max(1U, expectedHeight / 2U);
        if (mipLevel.width != expectedWidth || mipLevel.height != expectedHeight) {
            return false;
        }
        if (!CalculateExpectedPixelBytesUVE(mipLevel.width, mipLevel.height, texture.format,
                                            expectedPixelBytes) ||
            expectedPixelBytes != mipLevel.pixels.size()) {
            return false;
        }
    }
    return true;
}

bool LoadTextureAssetUVE(const std::filesystem::path& path, TextureAssetUVE& outTexture) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Texture) {
        UVE_ERROR("TextureAssetUVE: \"{}\" is not a texture file (asset type {})", path.string(),
                  static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    const bool hasVersionedHeader = HasVersionedTexturePayloadMagicUVE(payload);
    std::uint32_t payloadVersion = 0U;
    std::size_t offset = 0U;
    if (hasVersionedHeader) {
        offset += kTexturePayloadMagicUVE.size();
        if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, payloadVersion)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated payload version", path.string());
            return false;
        }
        if (payloadVersion != kTexturePayloadMetadataVersionUVE &&
            payloadVersion != kTexturePayloadMipmapVersionUVE && payloadVersion != kTexturePayloadVersionUVE) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has unsupported texture payload version {}", path.string(),
                      payloadVersion);
            return false;
        }
    }

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t formatValue = 0U;
    std::uint32_t colorSpaceValue = static_cast<std::uint32_t>(TextureAssetColorSpaceUVE::Linear);
    std::uint32_t usageValue = static_cast<std::uint32_t>(TextureUsageUVE::Generic);
    std::uint32_t encodingValue = static_cast<std::uint32_t>(TexturePayloadEncodingUVE::RawPixels);
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, width) ||
        !Utilities::ReadUint32LeFromBufferUVE(payload, offset, height) ||
        !Utilities::ReadUint32LeFromBufferUVE(payload, offset, formatValue)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated header", path.string());
        return false;
    }
    if (payloadVersion >= kTexturePayloadMetadataVersionUVE &&
        (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, colorSpaceValue) ||
         !Utilities::ReadUint32LeFromBufferUVE(payload, offset, usageValue))) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has truncated texture metadata", path.string());
        return false;
    }
    if (payloadVersion == kTexturePayloadVersionUVE &&
        !Utilities::ReadUint32LeFromBufferUVE(payload, offset, encodingValue)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated texture storage encoding", path.string());
        return false;
    }

    const auto format = static_cast<TextureAssetFormatUVE>(formatValue);
    const auto colorSpace = static_cast<TextureAssetColorSpaceUVE>(colorSpaceValue);
    const auto usage = static_cast<TextureUsageUVE>(usageValue);
    const auto payloadEncoding = static_cast<TexturePayloadEncodingUVE>(encodingValue);
    if (BytesPerPixelUVE(format) == 0U || !IsTextureColorSpaceValidUVE(colorSpace) ||
        !IsTextureUsageValidUVE(usage) || !IsTexturePayloadEncodingValidUVE(payloadEncoding)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has an unknown format, color space, usage, or storage encoding",
                  path.string());
        return false;
    }
    if (colorSpace == TextureAssetColorSpaceUVE::Srgb && format != TextureAssetFormatUVE::RGBA8Unorm) {
        UVE_ERROR("TextureAssetUVE: \"{}\" uses sRGB color space with an unsupported pixel format", path.string());
        return false;
    }

    TextureAssetUVE candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.format = format;
    candidate.colorSpace = colorSpace;
    candidate.usage = usage;
    candidate.payloadEncoding = payloadEncoding;

    std::uint64_t byteCount = 0U;
    if (payloadEncoding == TexturePayloadEncodingUVE::BasisUniversalKtx2) {
        if (payloadVersion != kTexturePayloadVersionUVE ||
            !Utilities::ReadUint64LeFromBufferUVE(payload, offset, byteCount) ||
            !ReadPixelBytesUVE(payload, offset, byteCount, candidate.basisKtx2Data)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has a malformed or truncated Basis/KTX2 payload", path.string());
            return false;
        }
    } else {
        if (!Utilities::ReadUint64LeFromBufferUVE(payload, offset, byteCount)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated level-0 byte count", path.string());
            return false;
        }
        std::uint64_t expectedPixelByteCount = 0U;
        if (!CalculateExpectedPixelBytesUVE(width, height, format, expectedPixelByteCount)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has zero or overflowing texture dimensions", path.string());
            return false;
        }
        if (byteCount != expectedPixelByteCount) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has {} pixel bytes, expected {} for a {}x{} texture",
                      path.string(), byteCount, expectedPixelByteCount, width, height);
            return false;
        }
        if (!ReadPixelBytesUVE(payload, offset, byteCount, candidate.pixels)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has truncated level-0 pixel data", path.string());
            return false;
        }

        if (payloadVersion == kTexturePayloadMipmapVersionUVE || payloadVersion == kTexturePayloadVersionUVE) {
            std::uint32_t mipLevelCount = 0U;
            if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, mipLevelCount)) {
                UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated mip-level count", path.string());
                return false;
            }
            const std::uint32_t maximumMipCount = MaximumMipLevelCountUVE(width, height);
            if (mipLevelCount > maximumMipCount - 1U) {
                UVE_ERROR("TextureAssetUVE: \"{}\" declares too many mip levels for {}x{}", path.string(),
                          width, height);
                return false;
            }
            candidate.mipLevels.reserve(mipLevelCount);
            std::uint32_t expectedWidth = width;
            std::uint32_t expectedHeight = height;
            for (std::uint32_t levelIndex = 0U; levelIndex < mipLevelCount; ++levelIndex) {
                std::uint32_t levelWidth = 0U;
                std::uint32_t levelHeight = 0U;
                std::uint64_t levelByteCount = 0U;
                if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, levelWidth) ||
                    !Utilities::ReadUint32LeFromBufferUVE(payload, offset, levelHeight) ||
                    !Utilities::ReadUint64LeFromBufferUVE(payload, offset, levelByteCount)) {
                    UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated mip-level header", path.string());
                    return false;
                }
                expectedWidth = std::max(1U, expectedWidth / 2U);
                expectedHeight = std::max(1U, expectedHeight / 2U);
                if (levelWidth != expectedWidth || levelHeight != expectedHeight) {
                    UVE_ERROR("TextureAssetUVE: \"{}\" mip level {} has invalid dimensions", path.string(),
                              levelIndex + 1U);
                    return false;
                }
                std::uint64_t expectedLevelByteCount = 0U;
                if (!CalculateExpectedPixelBytesUVE(levelWidth, levelHeight, format, expectedLevelByteCount) ||
                    levelByteCount != expectedLevelByteCount) {
                    UVE_ERROR("TextureAssetUVE: \"{}\" mip level {} has an invalid byte count", path.string(),
                              levelIndex + 1U);
                    return false;
                }
                TextureMipLevelUVE mipLevel;
                mipLevel.width = levelWidth;
                mipLevel.height = levelHeight;
                if (!ReadPixelBytesUVE(payload, offset, levelByteCount, mipLevel.pixels)) {
                    UVE_ERROR("TextureAssetUVE: \"{}\" has truncated pixel data for mip level {}", path.string(),
                              levelIndex + 1U);
                    return false;
                }
                candidate.mipLevels.push_back(std::move(mipLevel));
            }
        }
    }

    // Versioned layouts are consumed exactly. Keep the original legacy tolerance for trailing
    // bytes because those files predate the payload discriminator and may be extended.
    if (hasVersionedHeader && offset != payload.size()) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has unexpected trailing bytes after texture data", path.string());
        return false;
    }
    if (!IsTextureAssetValidUVE(candidate)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" failed final texture, mip-chain, or KTX2 validation", path.string());
        return false;
    }
    outTexture = std::move(candidate);
    return true;
}

bool SaveTextureAssetUVE(const TextureAssetUVE& texture, const std::filesystem::path& path) {
    if (!IsTextureAssetValidUVE(texture)) {
        UVE_ERROR("TextureAssetUVE: rejected invalid texture, mip chain, or KTX2 payload before writing {}",
                  path.string());
        return false;
    }

    std::vector<std::byte> payload;
    payload.reserve(kTexturePayloadMagicUVE.size() + 7U * sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                    texture.pixels.size() + texture.basisKtx2Data.size());
    Utilities::AppendBytesUVE(payload, kTexturePayloadMagicUVE.data(), kTexturePayloadMagicUVE.size());
    Utilities::AppendUint32LeUVE(payload, kTexturePayloadVersionUVE);
    Utilities::AppendUint32LeUVE(payload, texture.width);
    Utilities::AppendUint32LeUVE(payload, texture.height);
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.payloadEncoding));
    if (texture.payloadEncoding == TexturePayloadEncodingUVE::BasisUniversalKtx2) {
        Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(texture.basisKtx2Data.size()));
        Utilities::AppendBytesUVE(payload, texture.basisKtx2Data.data(), texture.basisKtx2Data.size());
    } else {
        Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
        Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());
        Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.mipLevels.size()));
        for (const TextureMipLevelUVE& mipLevel : texture.mipLevels) {
            Utilities::AppendUint32LeUVE(payload, mipLevel.width);
            Utilities::AppendUint32LeUVE(payload, mipLevel.height);
            Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(mipLevel.pixels.size()));
            Utilities::AppendBytesUVE(payload, mipLevel.pixels.data(), mipLevel.pixels.size());
        }
    }
    return WriteUveFileUVE(path, AssetKindUVE::Texture, payload);
}

namespace {

/// One source image, bounded exactly as every image importer bounds it, so a load can never become
/// a way around the limit the import path holds.
constexpr std::uint64_t kMaximumSourceImageBytesUVE = 64ULL * 1024ULL * 1024ULL;

/// The source images a texture reference may name instead of a published `.uvtex`: the same five
/// forms the four decoders below read and the Inspector's picker offers.
[[nodiscard]] bool IsSourceImageExtensionUVE(const std::string_view extension) noexcept {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga";
}

/// Moves one decoder's result into `texture` under the metadata an importer would have stamped on
/// it with default settings: RGBA8, sRGB, a colour map. A decoder that produced nothing usable
/// leaves `texture` untouched and reports failure, so a half-decoded image never reaches the GPU as
/// a texture with zero pixels in it.
template <typename ImageUVE>
[[nodiscard]] bool AdoptDecodedSourceImageUVE(ImageUVE& image, TextureAssetUVE& texture) {
    if (image.width == 0U || image.height == 0U || image.pixels.empty()) {
        return false;
    }
    texture.width = image.width;
    texture.height = image.height;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    texture.usage = TextureUsageUVE::Color;
    texture.pixels = std::move(image.pixels);
    return true;
}

} // namespace

bool LoadTextureAssetSourceUVE(const std::filesystem::path& path, TextureAssetUVE& outTexture) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (!IsSourceImageExtensionUVE(extension)) {
        // Everything else is either the envelope or not a texture at all; the envelope reader is
        // the one that can say which, in the terms of the file it just opened.
        return LoadTextureAssetUVE(path, outTexture);
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(path, "TextureAssetUVE", kMaximumSourceImageBytesUVE, sourceBytes)) {
        return false;
    }

    TextureAssetUVE texture;
    bool decoded = false;
    if (extension == ".png") {
        PngRgba8ImageUVE image;
        decoded = DecodePngRgba8ImageUVE(sourceBytes, image) && AdoptDecodedSourceImageUVE(image, texture);
    } else if (extension == ".jpg" || extension == ".jpeg") {
        JpegRgba8ImageUVE image;
        decoded = DecodeJpegRgba8ImageUVE(sourceBytes, image) && AdoptDecodedSourceImageUVE(image, texture);
    } else if (extension == ".bmp") {
        BmpRgba8ImageUVE image;
        decoded = DecodeBmpRgba8ImageUVE(sourceBytes, image) && AdoptDecodedSourceImageUVE(image, texture);
    } else {
        TgaRgba8ImageUVE image;
        decoded = DecodeTgaRgba8ImageUVE(sourceBytes, image) && AdoptDecodedSourceImageUVE(image, texture);
    }
    if (!decoded) {
        UVE_ERROR("TextureAssetUVE: source image \"{}\" could not be decoded", path.string());
        return false;
    }

    // Without a chain the map aliases badly the moment the surface is not facing the camera; the
    // importer would have built one, and the importer's defaults are the ones taken above, so the
    // two paths produce the same picture.
    if (!GenerateTextureMipmapsUVE(texture, TextureMipmapFilterUVE::Box, 0U) || !IsTextureAssetValidUVE(texture)) {
        UVE_ERROR("TextureAssetUVE: source image \"{}\" could not be completed into a texture", path.string());
        return false;
    }
    outTexture = std::move(texture);
    return true;
}

namespace {

/// Past this the slope is a wall: every edge becomes a cliff and the surface reads as embossed
/// metal rather than as the material the picture shows.
constexpr float kMaximumDerivedNormalStrengthUVE = 16.0F;

} // namespace

bool DeriveNormalTextureUVE(const TextureAssetUVE& source, const float strength,
                            TextureAssetUVE& outNormal) {
    if (source.format != TextureAssetFormatUVE::RGBA8Unorm || source.width == 0U || source.height == 0U ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4U) {
        return false;
    }
    if (!(strength >= 0.0F) || strength > kMaximumDerivedNormalStrengthUVE) {
        return false;
    }

    // Height is read from the stored bytes, so the picture keeps the even weighting it has on
    // screen; decoding a colour map to linear first would push every dark area towards flat.
    const auto heightAtUVE = [&source](const std::uint32_t x, const std::uint32_t y) -> float {
        // Clamped, not wrapped: a tileable map derives the same map it would without the seam,
        // and a photo's border does not pull detail in from the opposite edge.
        const std::uint32_t clampedX = std::min(x, source.width - 1U);
        const std::uint32_t clampedY = std::min(y, source.height - 1U);
        const std::size_t index = (static_cast<std::size_t>(clampedY) * source.width + clampedX) * 4U;
        const float red = static_cast<float>(std::to_integer<unsigned char>(source.pixels[index]));
        const float green = static_cast<float>(std::to_integer<unsigned char>(source.pixels[index + 1U]));
        const float blue = static_cast<float>(std::to_integer<unsigned char>(source.pixels[index + 2U]));
        return (0.299F * red + 0.587F * green + 0.114F * blue) / 255.0F;
    };

    TextureAssetUVE normal{};
    normal.width = source.width;
    normal.height = source.height;
    normal.format = TextureAssetFormatUVE::RGBA8Unorm;
    normal.colorSpace = TextureAssetColorSpaceUVE::Linear;
    normal.usage = TextureUsageUVE::Normal;
    normal.pixels.resize(source.pixels.size());

    for (std::uint32_t y = 0U; y < source.height; ++y) {
        for (std::uint32_t x = 0U; x < source.width; ++x) {
            // Central difference over one pixel each way. Rows run downwards while the tangent
            // space the shader samples has +Y up, which is where the green sign comes from.
            const float slopeX = 0.5F * (heightAtUVE(x + 1U, y) - heightAtUVE(x - 1U, y)) * strength;
            const float slopeY = 0.5F * (heightAtUVE(x, y + 1U) - heightAtUVE(x, y - 1U)) * strength;
            const float normalX = -slopeX;
            const float normalY = slopeY;
            const float normalZ = 1.0F;
            const float inverseLength = 1.0F / std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
            const std::size_t index = (static_cast<std::size_t>(y) * source.width + x) * 4U;
            const auto encode = [](const float component) {
                const float unbiased = component * 0.5F + 0.5F;
                return static_cast<std::byte>(static_cast<unsigned char>(std::lround(unbiased * 255.0F)));
            };
            normal.pixels[index] = encode(normalX * inverseLength);
            normal.pixels[index + 1U] = encode(normalY * inverseLength);
            normal.pixels[index + 2U] = encode(normalZ * inverseLength);
            normal.pixels[index + 3U] = std::byte{0xFF};
        }
    }

    // The chain the importer would have built, so a derived map is not the one asset in the
    // project that aliases when the surface turns away from the camera.
    if (!IsTextureAssetValidUVE(normal) || !GenerateTextureMipmapsUVE(normal, TextureMipmapFilterUVE::Box, 0U)) {
        UVE_ERROR("TextureAssetUVE: could not complete a derived normal map of {}x{}", normal.width,
                  normal.height);
        return false;
    }
    outNormal = std::move(normal);
    return true;
}

} // namespace UVE::Asset
