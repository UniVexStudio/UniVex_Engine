// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "texture_asset.h"
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

#include "import_helpers.h"

#include "bmp_metadata.h"
#include "jpeg_metadata.h"
#include "png_metadata.h"
#include "tga_metadata.h"
#include "texture_mipmap.h"
#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Asset {
namespace {

// The original texture payload had no discriminator. Version 1 added import metadata, version 2
// added raw mip levels, and version 3 adds a storage-encoding tag plus portable Basis/KTX2 bytes.
constexpr std::array<char, 4U> kTexturePayloadMagic{'U', 'V', 'T', 'X'};
constexpr std::array<std::uint8_t, 12U> kKtx2Identifier{
    0xABU, 0x4BU, 0x54U, 0x58U, 0x20U, 0x32U, 0x30U, 0xBBU, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
constexpr std::uint32_t kTexturePayloadMetadataVersion = 1U;
constexpr std::uint32_t kTexturePayloadMipmapVersion = 2U;
constexpr std::uint32_t kTexturePayloadVersion = 3U;

[[nodiscard]] bool IsTextureColorSpaceValid(const TextureAssetColorSpace colorSpace) noexcept {
    switch (colorSpace) {
        case TextureAssetColorSpace::Linear:
        case TextureAssetColorSpace::Srgb:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsTextureUsageValid(const TextureUsage usage) noexcept {
    switch (usage) {
        case TextureUsage::Generic:
        case TextureUsage::Color:
        case TextureUsage::Normal:
        case TextureUsage::Data:
        case TextureUsage::Hdr:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsTexturePayloadEncodingValid(const TexturePayloadEncoding encoding) noexcept {
    switch (encoding) {
        case TexturePayloadEncoding::RawPixels:
        case TexturePayloadEncoding::BasisUniversalKtx2:
            return true;
    }
    return false;
}

[[nodiscard]] bool HasVersionedTexturePayloadMagic(const std::vector<std::byte>& payload) noexcept {
    return payload.size() >= kTexturePayloadMagic.size() &&
           std::memcmp(payload.data(), kTexturePayloadMagic.data(), kTexturePayloadMagic.size()) == 0;
}

[[nodiscard]] bool CalculateExpectedPixelBytes(const std::uint32_t width, const std::uint32_t height,
                                                   const TextureAssetFormat format,
                                                   std::uint64_t& outExpectedBytes) noexcept {
    if (width == 0U || height == 0U) {
        return false;
    }

    const std::uint32_t bytesPerPixel = BytesPerPixel(format);
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

[[nodiscard]] std::uint32_t MaximumMipLevelCount(std::uint32_t width, std::uint32_t height) noexcept {
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

[[nodiscard]] bool IsBasisKtx2PayloadStructurallyValid(const TextureAsset& texture) noexcept {
    const std::vector<std::byte>& bytes = texture.basisKtx2Data;
    constexpr std::size_t kKtx2HeaderSize = 80U;
    constexpr std::size_t kKtx2LevelIndexEntrySize = 24U;
    if (bytes.size() < kKtx2HeaderSize || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    for (std::size_t index = 0U; index < kKtx2Identifier.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[index]) != kKtx2Identifier[index]) {
            return false;
        }
    }

    std::size_t offset = kKtx2Identifier.size();
    std::uint32_t vkFormat = 0U;
    std::uint32_t typeSize = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t depth = 0U;
    std::uint32_t layerCount = 0U;
    std::uint32_t faceCount = 0U;
    std::uint32_t levelCount = 0U;
    std::uint32_t supercompressionScheme = 0U;
    if (!Utilities::ReadUint32LeFromBuffer(bytes, offset, vkFormat) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, typeSize) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, width) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, height) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, depth) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, layerCount) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, faceCount) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, levelCount) ||
        !Utilities::ReadUint32LeFromBuffer(bytes, offset, supercompressionScheme)) {
        return false;
    }

    const std::uint32_t maximumMipCount = MaximumMipLevelCount(texture.width, texture.height);
    if (vkFormat != 0U || typeSize != 1U || width != texture.width || height != texture.height ||
        depth != 0U || layerCount != 0U || faceCount != 1U || levelCount == 0U ||
        levelCount > maximumMipCount || (supercompressionScheme != 0U && supercompressionScheme != 1U)) {
        return false;
    }
    if (std::cmp_greater(levelCount, (std::numeric_limits<std::size_t>::max() - kKtx2HeaderSize) /
                         kKtx2LevelIndexEntrySize)) {
        return false;
    }
    const std::size_t levelIndexEnd = kKtx2HeaderSize +
                                      static_cast<std::size_t>(levelCount) * kKtx2LevelIndexEntrySize;
    if (levelIndexEnd > bytes.size()) {
        return false;
    }

    offset = kKtx2HeaderSize;
    for (std::uint32_t level = 0U; level < levelCount; ++level) {
        std::uint64_t levelOffset = 0U;
        std::uint64_t levelLength = 0U;
        std::uint64_t uncompressedLength = 0U;
        if (!Utilities::ReadUint64LeFromBuffer(bytes, offset, levelOffset) ||
            !Utilities::ReadUint64LeFromBuffer(bytes, offset, levelLength) ||
            !Utilities::ReadUint64LeFromBuffer(bytes, offset, uncompressedLength)) {
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

[[nodiscard]] bool ReadPixelBytes(const std::vector<std::byte>& payload, std::size_t& offset,
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

std::uint32_t BytesPerPixel(const TextureAssetFormat format) noexcept {
    switch (format) {
        case TextureAssetFormat::RGBA8Unorm:
            return 4U;
        case TextureAssetFormat::RGBA16Float:
            return 8U;
    }
    return 0U;
}

bool IsTextureAssetMetadataValid(const TextureAsset& texture) noexcept {
    return BytesPerPixel(texture.format) != 0U && IsTextureColorSpaceValid(texture.colorSpace) &&
           IsTextureUsageValid(texture.usage) &&
           (texture.colorSpace != TextureAssetColorSpace::Srgb ||
            texture.format == TextureAssetFormat::RGBA8Unorm);
}

bool IsTextureAssetValid(const TextureAsset& texture) noexcept {
    if (!IsTextureAssetMetadataValid(texture) || !IsTexturePayloadEncodingValid(texture.payloadEncoding)) {
        return false;
    }
    if (texture.payloadEncoding == TexturePayloadEncoding::BasisUniversalKtx2) {
        return texture.format == TextureAssetFormat::RGBA8Unorm && texture.pixels.empty() &&
               texture.mipLevels.empty() && IsBasisKtx2PayloadStructurallyValid(texture);
    }
    if (!texture.basisKtx2Data.empty()) {
        return false;
    }

    std::uint64_t expectedPixelBytes = 0U;
    if (!CalculateExpectedPixelBytes(texture.width, texture.height, texture.format, expectedPixelBytes) ||
        expectedPixelBytes != texture.pixels.size()) {
        return false;
    }

    const std::uint32_t maximumMipCount = MaximumMipLevelCount(texture.width, texture.height);
    if (texture.mipLevels.size() > static_cast<std::size_t>(maximumMipCount - 1U)) {
        return false;
    }
    std::uint32_t expectedWidth = texture.width;
    std::uint32_t expectedHeight = texture.height;
    for (const TextureMipLevel& mipLevel : texture.mipLevels) {
        expectedWidth = std::max(1U, expectedWidth / 2U);
        expectedHeight = std::max(1U, expectedHeight / 2U);
        if (mipLevel.width != expectedWidth || mipLevel.height != expectedHeight) {
            return false;
        }
        if (!CalculateExpectedPixelBytes(mipLevel.width, mipLevel.height, texture.format,
                                            expectedPixelBytes) ||
            expectedPixelBytes != mipLevel.pixels.size()) {
            return false;
        }
    }
    return true;
}

bool LoadTextureAsset(const std::filesystem::path& path, TextureAsset& outTexture) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false; // ReadUveFile already logged the specific reason.
    }
    if (file->first.assetType != AssetKind::Texture) {
        UVE_ERROR("TextureAsset: \"{}\" is not a texture file (asset type {})", path.string(),
                  static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    const bool hasVersionedHeader = HasVersionedTexturePayloadMagic(payload);
    std::uint32_t payloadVersion = 0U;
    std::size_t offset = 0U;
    if (hasVersionedHeader) {
        offset += kTexturePayloadMagic.size();
        if (!Utilities::ReadUint32LeFromBuffer(payload, offset, payloadVersion)) {
            UVE_ERROR("TextureAsset: \"{}\" has a truncated payload version", path.string());
            return false;
        }
        if (payloadVersion != kTexturePayloadMetadataVersion &&
            payloadVersion != kTexturePayloadMipmapVersion && payloadVersion != kTexturePayloadVersion) {
            UVE_ERROR("TextureAsset: \"{}\" has unsupported texture payload version {}", path.string(),
                      payloadVersion);
            return false;
        }
    }

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t formatValue = 0U;
    std::uint32_t colorSpaceValue = static_cast<std::uint32_t>(TextureAssetColorSpace::Linear);
    std::uint32_t usageValue = static_cast<std::uint32_t>(TextureUsage::Generic);
    std::uint32_t encodingValue = static_cast<std::uint32_t>(TexturePayloadEncoding::RawPixels);
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, width) ||
        !Utilities::ReadUint32LeFromBuffer(payload, offset, height) ||
        !Utilities::ReadUint32LeFromBuffer(payload, offset, formatValue)) {
        UVE_ERROR("TextureAsset: \"{}\" has a truncated header", path.string());
        return false;
    }
    if (payloadVersion >= kTexturePayloadMetadataVersion &&
        (!Utilities::ReadUint32LeFromBuffer(payload, offset, colorSpaceValue) ||
         !Utilities::ReadUint32LeFromBuffer(payload, offset, usageValue))) {
        UVE_ERROR("TextureAsset: \"{}\" has truncated texture metadata", path.string());
        return false;
    }
    if (payloadVersion == kTexturePayloadVersion &&
        !Utilities::ReadUint32LeFromBuffer(payload, offset, encodingValue)) {
        UVE_ERROR("TextureAsset: \"{}\" has a truncated texture storage encoding", path.string());
        return false;
    }

    const auto format = static_cast<TextureAssetFormat>(formatValue);
    const auto colorSpace = static_cast<TextureAssetColorSpace>(colorSpaceValue);
    const auto usage = static_cast<TextureUsage>(usageValue);
    const auto payloadEncoding = static_cast<TexturePayloadEncoding>(encodingValue);
    if (BytesPerPixel(format) == 0U || !IsTextureColorSpaceValid(colorSpace) ||
        !IsTextureUsageValid(usage) || !IsTexturePayloadEncodingValid(payloadEncoding)) {
        UVE_ERROR("TextureAsset: \"{}\" has an unknown format, color space, usage, or storage encoding",
                  path.string());
        return false;
    }
    if (colorSpace == TextureAssetColorSpace::Srgb && format != TextureAssetFormat::RGBA8Unorm) {
        UVE_ERROR("TextureAsset: \"{}\" uses sRGB color space with an unsupported pixel format", path.string());
        return false;
    }

    TextureAsset candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.format = format;
    candidate.colorSpace = colorSpace;
    candidate.usage = usage;
    candidate.payloadEncoding = payloadEncoding;

    std::uint64_t byteCount = 0U;
    if (payloadEncoding == TexturePayloadEncoding::BasisUniversalKtx2) {
        if (payloadVersion != kTexturePayloadVersion ||
            !Utilities::ReadUint64LeFromBuffer(payload, offset, byteCount) ||
            !ReadPixelBytes(payload, offset, byteCount, candidate.basisKtx2Data)) {
            UVE_ERROR("TextureAsset: \"{}\" has a malformed or truncated Basis/KTX2 payload", path.string());
            return false;
        }
    } else {
        if (!Utilities::ReadUint64LeFromBuffer(payload, offset, byteCount)) {
            UVE_ERROR("TextureAsset: \"{}\" has a truncated level-0 byte count", path.string());
            return false;
        }
        std::uint64_t expectedPixelByteCount = 0U;
        if (!CalculateExpectedPixelBytes(width, height, format, expectedPixelByteCount)) {
            UVE_ERROR("TextureAsset: \"{}\" has zero or overflowing texture dimensions", path.string());
            return false;
        }
        if (byteCount != expectedPixelByteCount) {
            UVE_ERROR("TextureAsset: \"{}\" has {} pixel bytes, expected {} for a {}x{} texture",
                      path.string(), byteCount, expectedPixelByteCount, width, height);
            return false;
        }
        if (!ReadPixelBytes(payload, offset, byteCount, candidate.pixels)) {
            UVE_ERROR("TextureAsset: \"{}\" has truncated level-0 pixel data", path.string());
            return false;
        }

        if (payloadVersion == kTexturePayloadMipmapVersion || payloadVersion == kTexturePayloadVersion) {
            std::uint32_t mipLevelCount = 0U;
            if (!Utilities::ReadUint32LeFromBuffer(payload, offset, mipLevelCount)) {
                UVE_ERROR("TextureAsset: \"{}\" has a truncated mip-level count", path.string());
                return false;
            }
            const std::uint32_t maximumMipCount = MaximumMipLevelCount(width, height);
            if (mipLevelCount > maximumMipCount - 1U) {
                UVE_ERROR("TextureAsset: \"{}\" declares too many mip levels for {}x{}", path.string(),
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
                if (!Utilities::ReadUint32LeFromBuffer(payload, offset, levelWidth) ||
                    !Utilities::ReadUint32LeFromBuffer(payload, offset, levelHeight) ||
                    !Utilities::ReadUint64LeFromBuffer(payload, offset, levelByteCount)) {
                    UVE_ERROR("TextureAsset: \"{}\" has a truncated mip-level header", path.string());
                    return false;
                }
                expectedWidth = std::max(1U, expectedWidth / 2U);
                expectedHeight = std::max(1U, expectedHeight / 2U);
                if (levelWidth != expectedWidth || levelHeight != expectedHeight) {
                    UVE_ERROR("TextureAsset: \"{}\" mip level {} has invalid dimensions", path.string(),
                              levelIndex + 1U);
                    return false;
                }
                std::uint64_t expectedLevelByteCount = 0U;
                if (!CalculateExpectedPixelBytes(levelWidth, levelHeight, format, expectedLevelByteCount) ||
                    levelByteCount != expectedLevelByteCount) {
                    UVE_ERROR("TextureAsset: \"{}\" mip level {} has an invalid byte count", path.string(),
                              levelIndex + 1U);
                    return false;
                }
                TextureMipLevel mipLevel;
                mipLevel.width = levelWidth;
                mipLevel.height = levelHeight;
                if (!ReadPixelBytes(payload, offset, levelByteCount, mipLevel.pixels)) {
                    UVE_ERROR("TextureAsset: \"{}\" has truncated pixel data for mip level {}", path.string(),
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
        UVE_ERROR("TextureAsset: \"{}\" has unexpected trailing bytes after texture data", path.string());
        return false;
    }
    if (!IsTextureAssetValid(candidate)) {
        UVE_ERROR("TextureAsset: \"{}\" failed final texture, mip-chain, or KTX2 validation", path.string());
        return false;
    }
    outTexture = std::move(candidate);
    return true;
}

bool SaveTextureAsset(const TextureAsset& texture, const std::filesystem::path& path) {
    if (!IsTextureAssetValid(texture)) {
        UVE_ERROR("TextureAsset: rejected invalid texture, mip chain, or KTX2 payload before writing {}",
                  path.string());
        return false;
    }

    std::vector<std::byte> payload;
    payload.reserve(kTexturePayloadMagic.size() + 7U * sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                    texture.pixels.size() + texture.basisKtx2Data.size());
    Utilities::AppendBytes(payload, kTexturePayloadMagic.data(), kTexturePayloadMagic.size());
    Utilities::AppendUint32Le(payload, kTexturePayloadVersion);
    Utilities::AppendUint32Le(payload, texture.width);
    Utilities::AppendUint32Le(payload, texture.height);
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.payloadEncoding));
    if (texture.payloadEncoding == TexturePayloadEncoding::BasisUniversalKtx2) {
        Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(texture.basisKtx2Data.size()));
        Utilities::AppendBytes(payload, texture.basisKtx2Data.data(), texture.basisKtx2Data.size());
    } else {
        Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(texture.pixels.size()));
        Utilities::AppendBytes(payload, texture.pixels.data(), texture.pixels.size());
        Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.mipLevels.size()));
        for (const TextureMipLevel& mipLevel : texture.mipLevels) {
            Utilities::AppendUint32Le(payload, mipLevel.width);
            Utilities::AppendUint32Le(payload, mipLevel.height);
            Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(mipLevel.pixels.size()));
            Utilities::AppendBytes(payload, mipLevel.pixels.data(), mipLevel.pixels.size());
        }
    }
    return WriteUveFile(path, AssetKind::Texture, payload);
}

namespace {

/// One source image, bounded exactly as every image importer bounds it, so a load can never become
/// a way around the limit the import path holds.
constexpr std::uint64_t kMaximumSourceImageBytes = 64ULL * 1024ULL * 1024ULL;

/// The source images a texture reference may name instead of a published `.uvtex`: the same five
/// forms the four decoders below read and the Inspector's picker offers.
[[nodiscard]] bool IsSourceImageExtension(const std::string_view extension) noexcept {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga";
}

/// Moves one decoder's result into `texture` under the metadata an importer would have stamped on
/// it with default settings: RGBA8, sRGB, a colour map. A decoder that produced nothing usable
/// leaves `texture` untouched and reports failure, so a half-decoded image never reaches the GPU as
/// a texture with zero pixels in it.
template <typename Image>
[[nodiscard]] bool AdoptDecodedSourceImage(Image& image, TextureAsset& texture) {
    if (image.width == 0U || image.height == 0U || image.pixels.empty()) {
        return false;
    }
    texture.width = image.width;
    texture.height = image.height;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpace::Srgb;
    texture.usage = TextureUsage::Color;
    texture.pixels = std::move(image.pixels);
    return true;
}

} // namespace

bool LoadTextureAssetSource(const std::filesystem::path& path, TextureAsset& outTexture) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (!IsSourceImageExtension(extension)) {
        // Everything else is either the envelope or not a texture at all; the envelope reader is
        // the one that can say which, in the terms of the file it just opened.
        return LoadTextureAsset(path, outTexture);
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytes(path, "TextureAsset", kMaximumSourceImageBytes, sourceBytes)) {
        return false;
    }

    TextureAsset texture;
    bool decoded = false;
    if (extension == ".png") {
        PngRgba8Image image;
        decoded = DecodePngRgba8Image(sourceBytes, image) && AdoptDecodedSourceImage(image, texture);
    } else if (extension == ".jpg" || extension == ".jpeg") {
        JpegRgba8Image image;
        decoded = DecodeJpegRgba8Image(sourceBytes, image) && AdoptDecodedSourceImage(image, texture);
    } else if (extension == ".bmp") {
        BmpRgba8Image image;
        decoded = DecodeBmpRgba8Image(sourceBytes, image) && AdoptDecodedSourceImage(image, texture);
    } else {
        TgaRgba8Image image;
        decoded = DecodeTgaRgba8Image(sourceBytes, image) && AdoptDecodedSourceImage(image, texture);
    }
    if (!decoded) {
        UVE_ERROR("TextureAsset: source image \"{}\" could not be decoded", path.string());
        return false;
    }

    // Without a chain the map aliases badly the moment the surface is not facing the camera; the
    // importer would have built one, and the importer's defaults are the ones taken above, so the
    // two paths produce the same picture.
    if (!GenerateTextureMipmaps(texture, TextureMipmapFilter::Box, 0U) || !IsTextureAssetValid(texture)) {
        UVE_ERROR("TextureAsset: source image \"{}\" could not be completed into a texture", path.string());
        return false;
    }
    outTexture = std::move(texture);
    return true;
}

namespace {

/// Past this the slope is a wall: every edge becomes a cliff and the surface reads as embossed
/// metal rather than as the material the picture shows.
constexpr float kMaximumDerivedNormalStrength = 16.0F;

} // namespace

bool DeriveNormalTexture(const TextureAsset& source, const float strength,
                            TextureAsset& outNormal) {
    if (source.format != TextureAssetFormat::RGBA8Unorm || source.width == 0U || source.height == 0U ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4U) {
        return false;
    }
    if (!(strength >= 0.0F) || strength > kMaximumDerivedNormalStrength) {
        return false;
    }

    // Height is read from the stored bytes, so the picture keeps the even weighting it has on
    // screen; decoding a colour map to linear first would push every dark area towards flat.
    const auto heightAt = [&source](const std::uint32_t x, const std::uint32_t y) -> float {
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

    TextureAsset normal{};
    normal.width = source.width;
    normal.height = source.height;
    normal.format = TextureAssetFormat::RGBA8Unorm;
    normal.colorSpace = TextureAssetColorSpace::Linear;
    normal.usage = TextureUsage::Normal;
    normal.pixels.resize(source.pixels.size());

    for (std::uint32_t y = 0U; y < source.height; ++y) {
        for (std::uint32_t x = 0U; x < source.width; ++x) {
            // Central difference over one pixel each way. Rows run downwards while the tangent
            // space the shader samples has +Y up, which is where the green sign comes from.
            const float slopeX = 0.5F * (heightAt(x + 1U, y) - heightAt(x - 1U, y)) * strength;
            const float slopeY = 0.5F * (heightAt(x, y + 1U) - heightAt(x, y - 1U)) * strength;
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
    if (!IsTextureAssetValid(normal) || !GenerateTextureMipmaps(normal, TextureMipmapFilter::Box, 0U)) {
        UVE_ERROR("TextureAsset: could not complete a derived normal map of {}x{}", normal.width,
                  normal.height);
        return false;
    }
    outNormal = std::move(normal);
    return true;
}

bool PackOrmTexture(const TextureAsset* occlusion, const TextureAsset* roughness, const TextureAsset* metallic,
                       TextureAsset& outOrm) {
    const std::array<const TextureAsset*, 3> maps{occlusion, roughness, metallic};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    for (const TextureAsset* map : maps) {
        if (map == nullptr) {
            continue;
        }
        if (map->format != TextureAssetFormat::RGBA8Unorm || map->payloadEncoding != TexturePayloadEncoding::RawPixels ||
            map->width == 0U || map->height == 0U ||
            map->pixels.size() < static_cast<std::size_t>(map->width) * map->height * 4U) {
            return false;
        }
        width = std::max(width, map->width);
        height = std::max(height, map->height);
    }
    if (width == 0U || height == 0U) {
        return false;
    }
    TextureAsset orm;
    orm.width = width;
    orm.height = height;
    orm.format = TextureAssetFormat::RGBA8Unorm;
    orm.colorSpace = TextureAssetColorSpace::Linear;
    orm.usage = TextureUsage::Data;
    orm.pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4U;
            for (std::size_t channel = 0U; channel < maps.size(); ++channel) {
                const TextureAsset* const map = maps[channel];
                std::byte value{255};
                if (map != nullptr) {
                    // Nearest texel of a smaller map, stretched over the whole.
                    const std::uint32_t mx = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * map->width / width);
                    const std::uint32_t my = static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * map->height / height);
                    value = map->pixels[(static_cast<std::size_t>(my) * map->width + mx) * 4U];
                }
                orm.pixels[at + channel] = value;
            }
            orm.pixels[at + 3U] = std::byte{255};
        }
    }
    if (!IsTextureAssetValid(orm) || !GenerateTextureMipmaps(orm, TextureMipmapFilter::Box, 0U)) {
        return false;
    }
    outOrm = std::move(orm);
    return true;
}

} // namespace UVE::Asset
