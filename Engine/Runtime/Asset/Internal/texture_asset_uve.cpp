// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_asset_uve.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

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

[[nodiscard]] bool IsTextureColorSpaceValidUVE(const TextureColorSpaceUVE colorSpace) noexcept {
    switch (colorSpace) {
        case TextureColorSpaceUVE::Linear:
        case TextureColorSpaceUVE::Srgb:
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
                                                   const TextureFormatUVE format,
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
    if (!Utilities::ReadUint32FromBufferUVE(bytes, offset, vkFormat) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, typeSize) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, width) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, height) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, depth) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, layerCount) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, faceCount) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, levelCount) ||
        !Utilities::ReadUint32FromBufferUVE(bytes, offset, supercompressionScheme)) {
        return false;
    }

    const std::uint32_t maximumMipCount = MaximumMipLevelCountUVE(texture.width, texture.height);
    if (vkFormat != 0U || typeSize != 1U || width != texture.width || height != texture.height ||
        depth != 0U || layerCount != 0U || faceCount != 1U || levelCount == 0U ||
        levelCount > maximumMipCount || (supercompressionScheme != 0U && supercompressionScheme != 1U)) {
        return false;
    }
    if (levelCount > (std::numeric_limits<std::size_t>::max() - kKtx2HeaderSizeUVE) /
                         kKtx2LevelIndexEntrySizeUVE) {
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
        if (!Utilities::ReadUint64FromBufferUVE(bytes, offset, levelOffset) ||
            !Utilities::ReadUint64FromBufferUVE(bytes, offset, levelLength) ||
            !Utilities::ReadUint64FromBufferUVE(bytes, offset, uncompressedLength) ||
            levelOffset < levelIndexEnd || levelLength == 0U || uncompressedLength == 0U ||
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

std::uint32_t BytesPerPixelUVE(const TextureFormatUVE format) noexcept {
    switch (format) {
        case TextureFormatUVE::RGBA8Unorm:
            return 4U;
        case TextureFormatUVE::RGBA16Float:
            return 8U;
    }
    return 0U;
}

bool IsTextureAssetMetadataValidUVE(const TextureAssetUVE& texture) noexcept {
    return BytesPerPixelUVE(texture.format) != 0U && IsTextureColorSpaceValidUVE(texture.colorSpace) &&
           IsTextureUsageValidUVE(texture.usage) &&
           (texture.colorSpace != TextureColorSpaceUVE::Srgb ||
            texture.format == TextureFormatUVE::RGBA8Unorm);
}

bool IsTextureAssetValidUVE(const TextureAssetUVE& texture) noexcept {
    if (!IsTextureAssetMetadataValidUVE(texture) || !IsTexturePayloadEncodingValidUVE(texture.payloadEncoding)) {
        return false;
    }
    if (texture.payloadEncoding == TexturePayloadEncodingUVE::BasisUniversalKtx2) {
        return texture.format == TextureFormatUVE::RGBA8Unorm && texture.pixels.empty() &&
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
        if (!Utilities::ReadUint32FromBufferUVE(payload, offset, payloadVersion)) {
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
    std::uint32_t colorSpaceValue = static_cast<std::uint32_t>(TextureColorSpaceUVE::Linear);
    std::uint32_t usageValue = static_cast<std::uint32_t>(TextureUsageUVE::Generic);
    std::uint32_t encodingValue = static_cast<std::uint32_t>(TexturePayloadEncodingUVE::RawPixels);
    if (!Utilities::ReadUint32FromBufferUVE(payload, offset, width) ||
        !Utilities::ReadUint32FromBufferUVE(payload, offset, height) ||
        !Utilities::ReadUint32FromBufferUVE(payload, offset, formatValue)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated header", path.string());
        return false;
    }
    if (payloadVersion >= kTexturePayloadMetadataVersionUVE &&
        (!Utilities::ReadUint32FromBufferUVE(payload, offset, colorSpaceValue) ||
         !Utilities::ReadUint32FromBufferUVE(payload, offset, usageValue))) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has truncated texture metadata", path.string());
        return false;
    }
    if (payloadVersion == kTexturePayloadVersionUVE &&
        !Utilities::ReadUint32FromBufferUVE(payload, offset, encodingValue)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated texture storage encoding", path.string());
        return false;
    }

    const auto format = static_cast<TextureFormatUVE>(formatValue);
    const auto colorSpace = static_cast<TextureColorSpaceUVE>(colorSpaceValue);
    const auto usage = static_cast<TextureUsageUVE>(usageValue);
    const auto payloadEncoding = static_cast<TexturePayloadEncodingUVE>(encodingValue);
    if (BytesPerPixelUVE(format) == 0U || !IsTextureColorSpaceValidUVE(colorSpace) ||
        !IsTextureUsageValidUVE(usage) || !IsTexturePayloadEncodingValidUVE(payloadEncoding)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has an unknown format, color space, usage, or storage encoding",
                  path.string());
        return false;
    }
    if (colorSpace == TextureColorSpaceUVE::Srgb && format != TextureFormatUVE::RGBA8Unorm) {
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
            !Utilities::ReadUint64FromBufferUVE(payload, offset, byteCount) ||
            !ReadPixelBytesUVE(payload, offset, byteCount, candidate.basisKtx2Data)) {
            UVE_ERROR("TextureAssetUVE: \"{}\" has a malformed or truncated Basis/KTX2 payload", path.string());
            return false;
        }
    } else {
        if (!Utilities::ReadUint64FromBufferUVE(payload, offset, byteCount)) {
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
            if (!Utilities::ReadUint32FromBufferUVE(payload, offset, mipLevelCount)) {
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
                if (!Utilities::ReadUint32FromBufferUVE(payload, offset, levelWidth) ||
                    !Utilities::ReadUint32FromBufferUVE(payload, offset, levelHeight) ||
                    !Utilities::ReadUint64FromBufferUVE(payload, offset, levelByteCount)) {
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
    Utilities::AppendUint32UVE(payload, kTexturePayloadVersionUVE);
    Utilities::AppendUint32UVE(payload, texture.width);
    Utilities::AppendUint32UVE(payload, texture.height);
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.payloadEncoding));
    if (texture.payloadEncoding == TexturePayloadEncodingUVE::BasisUniversalKtx2) {
        Utilities::AppendUint64UVE(payload, static_cast<std::uint64_t>(texture.basisKtx2Data.size()));
        Utilities::AppendBytesUVE(payload, texture.basisKtx2Data.data(), texture.basisKtx2Data.size());
    } else {
        Utilities::AppendUint64UVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
        Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());
        Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.mipLevels.size()));
        for (const TextureMipLevelUVE& mipLevel : texture.mipLevels) {
            Utilities::AppendUint32UVE(payload, mipLevel.width);
            Utilities::AppendUint32UVE(payload, mipLevel.height);
            Utilities::AppendUint64UVE(payload, static_cast<std::uint64_t>(mipLevel.pixels.size()));
            Utilities::AppendBytesUVE(payload, mipLevel.pixels.data(), mipLevel.pixels.size());
        }
    }
    return WriteUveFileUVE(path, AssetKindUVE::Texture, payload);
}

} // namespace UVE::Asset
