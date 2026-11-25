// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "texture_asset.h"
#include "texture_import_settings.h"
#include "texture_mipmap.h"
#include "texture_compression.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "uve_file_envelope.h"
#include "binary_buffer.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] TextureAsset MakeTestTexture() {
    TextureAsset texture;
    texture.width = 2;
    texture.height = 2;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpace::Srgb;
    texture.usage = TextureUsage::Color;
    texture.pixels.resize(2 * 2 * 4);
    for (std::size_t index = 0; index < texture.pixels.size(); ++index) {
        texture.pixels[index] = static_cast<std::byte>(index);
    }
    return texture;
}

[[nodiscard]] std::vector<std::byte> MakeLegacyTexturePayload(const TextureAsset& texture) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32Le(payload, texture.width);
    Utilities::AppendUint32Le(payload, texture.height);
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytes(payload, texture.pixels.data(), texture.pixels.size());
    return payload;
}

[[nodiscard]] std::vector<std::byte> MakeVersionOneTexturePayload(const TextureAsset& texture) {
    constexpr std::array<char, 4U> kMagic{'U', 'V', 'T', 'X'};
    std::vector<std::byte> payload;
    Utilities::AppendBytes(payload, kMagic.data(), kMagic.size());
    Utilities::AppendUint32Le(payload, 1U);
    Utilities::AppendUint32Le(payload, texture.width);
    Utilities::AppendUint32Le(payload, texture.height);
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytes(payload, texture.pixels.data(), texture.pixels.size());
    return payload;
}

[[nodiscard]] std::vector<std::byte> MakeVersionTwoTexturePayload(const TextureAsset& texture) {
    constexpr std::array<char, 4U> kMagic{'U', 'V', 'T', 'X'};
    std::vector<std::byte> payload;
    Utilities::AppendBytes(payload, kMagic.data(), kMagic.size());
    Utilities::AppendUint32Le(payload, 2U);
    Utilities::AppendUint32Le(payload, texture.width);
    Utilities::AppendUint32Le(payload, texture.height);
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytes(payload, texture.pixels.data(), texture.pixels.size());
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(texture.mipLevels.size()));
    for (const TextureMipLevel& mip : texture.mipLevels) {
        Utilities::AppendUint32Le(payload, mip.width);
        Utilities::AppendUint32Le(payload, mip.height);
        Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(mip.pixels.size()));
        Utilities::AppendBytes(payload, mip.pixels.data(), mip.pixels.size());
    }
    return payload;
}

TEST(TextureImportSettingsTest, CacheVersionIncludesColorSpaceUsageAndMipmapPolicy) {
    TextureImportSettings defaultSettings;
    TextureImportSettings sameSettings;
    TextureImportSettings linearNormalSettings;
    linearNormalSettings.colorSpace = TextureAssetColorSpace::Linear;
    linearNormalSettings.usage = TextureUsage::Normal;
    linearNormalSettings.generateMipmaps = false;
    TextureImportSettings nearestSettings;
    nearestSettings.mipFilter = TextureMipmapFilter::Nearest;
    TextureImportSettings cappedSettings;
    cappedSettings.maxMipLevels = 3U;
    TextureImportSettings compressedSettings;
    compressedSettings.compressionMode = TextureCompressionMode::BasisETC1S;
    TextureImportSettings differentQualitySettings = compressedSettings;
    differentQualitySettings.compressionQuality = 70U;
    TextureImportSettings differentEffortSettings = compressedSettings;
    differentEffortSettings.compressionEffort = 8U;

    EXPECT_EQ(defaultSettings.GetCacheVersion(),
              "texture-import-v3;color-space=1;usage=1;generate-mipmaps=1;mip-filter=0;max-levels=0;compression=0;compression-quality=85;compression-effort=5");
    EXPECT_EQ(defaultSettings.GetCacheVersion(), sameSettings.GetCacheVersion());
    EXPECT_NE(defaultSettings.GetCacheVersion(), linearNormalSettings.GetCacheVersion());
    EXPECT_NE(defaultSettings.GetCacheVersion(), nearestSettings.GetCacheVersion());
    EXPECT_NE(defaultSettings.GetCacheVersion(), cappedSettings.GetCacheVersion());
    EXPECT_NE(defaultSettings.GetCacheVersion(), compressedSettings.GetCacheVersion());
    EXPECT_NE(compressedSettings.GetCacheVersion(), differentQualitySettings.GetCacheVersion());
    EXPECT_NE(compressedSettings.GetCacheVersion(), differentEffortSettings.GetCacheVersion());
}

TEST(TextureAssetTest, DefaultMetadataIsLinearGenericAndRaw) {
    const TextureAsset texture{};
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Linear);
    EXPECT_EQ(texture.usage, TextureUsage::Generic);
    EXPECT_EQ(texture.payloadEncoding, TexturePayloadEncoding::RawPixels);
    EXPECT_TRUE(texture.basisKtx2Data.empty());
}

TEST(TextureMipmapTest, BoxFilterAveragesSrgbColorInLinearSpace) {
    TextureAsset srgbTexture;
    srgbTexture.width = 2U;
    srgbTexture.height = 2U;
    srgbTexture.colorSpace = TextureAssetColorSpace::Srgb;
    srgbTexture.usage = TextureUsage::Color;
    srgbTexture.pixels = {
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
    };
    ASSERT_TRUE(GenerateTextureMipmaps(srgbTexture, TextureMipmapFilter::Box));
    ASSERT_EQ(srgbTexture.mipLevels.size(), 1U);
    ASSERT_EQ(srgbTexture.mipLevels[0].pixels.size(), 4U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[0]), 188U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[1]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[2]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[3]), 255U);

    TextureAsset linearTexture = srgbTexture;
    linearTexture.colorSpace = TextureAssetColorSpace::Linear;
    linearTexture.mipLevels.clear();
    ASSERT_TRUE(GenerateTextureMipmaps(linearTexture, TextureMipmapFilter::Box));
    EXPECT_EQ(std::to_integer<std::uint8_t>(linearTexture.mipLevels[0].pixels[0]), 128U);
}

TEST(TextureMipmapTest, NearestFilterAndLevelCapAreHonored) {
    TextureAsset texture;
    texture.width = 4U;
    texture.height = 2U;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.pixels.resize(4U * 2U * 4U);
    for (std::size_t index = 0U; index < texture.pixels.size(); ++index) {
        texture.pixels[index] = static_cast<std::byte>(index);
    }

    ASSERT_TRUE(GenerateTextureMipmaps(texture, TextureMipmapFilter::Nearest, 2U));
    ASSERT_EQ(texture.mipLevels.size(), 1U);
    EXPECT_EQ(texture.mipLevels[0].width, 2U);
    EXPECT_EQ(texture.mipLevels[0].height, 1U);
    EXPECT_EQ(texture.mipLevels[0].pixels.size(), 8U);

    ASSERT_TRUE(GenerateTextureMipmaps(texture, TextureMipmapFilter::Box, 1U));
    EXPECT_TRUE(texture.mipLevels.empty());
}

TEST(TextureAssetTest, BytesPerPixel_ReturnsExpectedValues) {
    EXPECT_EQ(BytesPerPixel(TextureAssetFormat::RGBA8Unorm), 4U);
    EXPECT_EQ(BytesPerPixel(TextureAssetFormat::RGBA16Float), 8U);
}

#if defined(UVE_HAS_BASIS_ENCODER) && UVE_HAS_BASIS_ENCODER
TEST(TextureCompressionTest, EncodesBothBasisModesAndTranscodesEveryMip) {
    for (const TextureCompressionMode mode : {TextureCompressionMode::BasisETC1S,
                                                  TextureCompressionMode::BasisUASTC}) {
        TextureAsset source;
        source.width = 7U;
        source.height = 5U;
        source.format = TextureAssetFormat::RGBA8Unorm;
        source.colorSpace = TextureAssetColorSpace::Srgb;
        source.usage = TextureUsage::Color;
        source.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4U);
        for (std::size_t pixel = 0U; pixel < source.pixels.size() / 4U; ++pixel) {
            source.pixels[pixel * 4U] = static_cast<std::byte>((pixel * 19U) & 0xFFU);
            source.pixels[pixel * 4U + 1U] = static_cast<std::byte>((pixel * 37U) & 0xFFU);
            source.pixels[pixel * 4U + 2U] = static_cast<std::byte>((pixel * 53U) & 0xFFU);
            source.pixels[pixel * 4U + 3U] = std::byte{0xFF};
        }
        ASSERT_TRUE(GenerateTextureMipmaps(source, TextureMipmapFilter::Box));
        ASSERT_EQ(source.mipLevels.size(), 2U);
        ASSERT_TRUE(CompressTextureAssetWithBasis(source, mode, 60U, 2U));
        ASSERT_EQ(source.payloadEncoding, TexturePayloadEncoding::BasisUniversalKtx2);
        EXPECT_TRUE(source.pixels.empty());
        EXPECT_TRUE(source.mipLevels.empty());
        ASSERT_TRUE(IsTextureAssetValid(source));

        TextureCompressionInfo info;
        ASSERT_TRUE(GetTextureCompressionInfo(source, info));
        EXPECT_EQ(info.width, 7U);
        EXPECT_EQ(info.height, 5U);
        EXPECT_EQ(info.mipLevels, 3U);
        EXPECT_FALSE(info.hasAlpha);

        TextureTranscodedMipChain rgba;
        ASSERT_TRUE(TranscodeTextureAsset(source, TextureTranscodeTarget::Rgba8Unorm, rgba));
        EXPECT_EQ(rgba.width, 7U);
        EXPECT_EQ(rgba.height, 5U);
        EXPECT_EQ(rgba.mipLevels, 3U);
        EXPECT_EQ(rgba.pixels.size(), 168U);

        TextureTranscodedMipChain bc7;
        ASSERT_TRUE(TranscodeTextureAsset(source, TextureTranscodeTarget::Bc7Rgba, bc7));
        EXPECT_EQ(bc7.pixels.size(), 96U);

        const std::filesystem::path path = mode == TextureCompressionMode::BasisETC1S
                                               ? "uve_texture_asset_tests_etc1s.uvtex"
                                               : "uve_texture_asset_tests_uastc.uvtex";
        std::filesystem::remove(path);
        ASSERT_TRUE(SaveTextureAsset(source, path));
        TextureAsset loaded;
        ASSERT_TRUE(LoadTextureAsset(path, loaded));
        EXPECT_EQ(loaded.payloadEncoding, TexturePayloadEncoding::BasisUniversalKtx2);
        EXPECT_EQ(loaded.colorSpace, source.colorSpace);
        EXPECT_EQ(loaded.usage, source.usage);
        EXPECT_EQ(loaded.basisKtx2Data, source.basisKtx2Data);
        std::filesystem::remove(path);
    }
}

TEST(TextureCompressionTest, RejectsAlphaDroppingTargetsWithoutChangingOutput) {
    TextureAsset source;
    source.width = 4U;
    source.height = 4U;
    source.format = TextureAssetFormat::RGBA8Unorm;
    source.pixels.resize(4U * 4U * 4U);
    for (std::size_t pixel = 0U; pixel < source.pixels.size() / 4U; ++pixel) {
        source.pixels[pixel * 4U] = std::byte{0x20};
        source.pixels[pixel * 4U + 1U] = std::byte{0x80};
        source.pixels[pixel * 4U + 2U] = std::byte{0xC0};
        source.pixels[pixel * 4U + 3U] = std::byte{0x80};
    }
    ASSERT_TRUE(CompressTextureAssetWithBasis(source, TextureCompressionMode::BasisUASTC, 60U, 1U));

    TextureCompressionInfo info;
    ASSERT_TRUE(GetTextureCompressionInfo(source, info));
    EXPECT_TRUE(info.hasAlpha);
    TextureTranscodedMipChain output;
    output.width = 99U;
    output.pixels = {std::byte{0xAB}};
    EXPECT_FALSE(TranscodeTextureAsset(source, TextureTranscodeTarget::Bc1Rgb, output));
    EXPECT_EQ(output.width, 99U);
    EXPECT_EQ(output.pixels, (std::vector<std::byte>{std::byte{0xAB}}));

    ASSERT_TRUE(TranscodeTextureAsset(source, TextureTranscodeTarget::Bc3Rgba, output));
    EXPECT_EQ(output.pixels.size(), 16U);
}
#endif

TEST(TextureAssetTest, SaveThenLoad_RoundTripsByteExact) {
    const std::filesystem::path path = "uve_texture_asset_tests_round_trip.uvtex";
    std::filesystem::remove(path);
    const TextureAsset original = MakeTestTexture();
    ASSERT_TRUE(SaveTextureAsset(original, path));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));

    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.format, original.format);
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_TRUE(IsTextureAssetValid(loaded));

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, SaveThenLoad_RoundTripsAllMipLevels) {
    const std::filesystem::path path = "uve_texture_asset_tests_mip_chain.uvtex";
    std::filesystem::remove(path);
    TextureAsset original = MakeTestTexture();
    original.mipLevels.push_back(TextureMipLevel{1U, 1U,
        std::vector<std::byte>{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}}});
    ASSERT_TRUE(IsTextureAssetValid(original));
    ASSERT_TRUE(SaveTextureAsset(original, path));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));
    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_EQ(loaded.mipLevels, original.mipLevels);

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadVersionOnePayload_PreservesMetadataAndDefaultsToOneLevel) {
    const std::filesystem::path path = "uve_texture_asset_tests_v1_payload.uvtex";
    std::filesystem::remove(path);
    const TextureAsset original = MakeTestTexture();
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, MakeVersionOneTexturePayload(original)));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_TRUE(loaded.mipLevels.empty());

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadVersionTwoPayload_PreservesRawMipChain) {
    const std::filesystem::path path = "uve_texture_asset_tests_v2_payload.uvtex";
    std::filesystem::remove(path);
    TextureAsset original = MakeTestTexture();
    original.mipLevels.push_back(TextureMipLevel{
        1U, 1U, std::vector<std::byte>{std::byte{0x20}, std::byte{0x40}, std::byte{0x80}, std::byte{0xFF}}});
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, MakeVersionTwoTexturePayload(original)));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));
    EXPECT_EQ(loaded.payloadEncoding, TexturePayloadEncoding::RawPixels);
    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_EQ(loaded.mipLevels, original.mipLevels);

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadLegacyPayload_DefaultsNewMetadataWithoutChangingPixels) {
    const std::filesystem::path path = "uve_texture_asset_tests_legacy_payload.uvtex";
    std::filesystem::remove(path);
    const TextureAsset original = MakeTestTexture();
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, MakeLegacyTexturePayload(original)));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));

    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.format, original.format);
    EXPECT_EQ(loaded.colorSpace, TextureAssetColorSpace::Linear);
    EXPECT_EQ(loaded.usage, TextureUsage::Generic);
    EXPECT_EQ(loaded.pixels, original.pixels);

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadVersionedPayload_RejectsUnknownMetadataWithoutPublishingPartialOutput) {
    const std::filesystem::path path = "uve_texture_asset_tests_invalid_metadata.uvtex";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveTextureAsset(MakeTestTexture(), path));

    const auto file = ReadUveFile(path);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> malformedPayload = file->second;
    constexpr std::size_t kTextureUsageOffset = 4U + 4U + 4U + 4U + 4U + 4U;
    const std::uint32_t unknownUsage = 99U;
    ASSERT_LE(kTextureUsageOffset + sizeof(unknownUsage), malformedPayload.size());
    std::memcpy(malformedPayload.data() + kTextureUsageOffset, &unknownUsage, sizeof(unknownUsage));
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, malformedPayload));

    const TextureAsset sentinel = MakeTestTexture();
    TextureAsset output = sentinel;
    EXPECT_FALSE(LoadTextureAsset(path, output));
    EXPECT_EQ(output.width, sentinel.width);
    EXPECT_EQ(output.height, sentinel.height);
    EXPECT_EQ(output.format, sentinel.format);
    EXPECT_EQ(output.colorSpace, sentinel.colorSpace);
    EXPECT_EQ(output.usage, sentinel.usage);
    EXPECT_EQ(output.pixels, sentinel.pixels);

    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadVersionedPayload_RejectsUnsupportedVersion) {
    const std::filesystem::path path = "uve_texture_asset_tests_unsupported_version.uvtex";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveTextureAsset(MakeTestTexture(), path));

    const auto file = ReadUveFile(path);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> unsupportedPayload = file->second;
    constexpr std::size_t kTexturePayloadVersionOffset = 4U;
    const std::uint32_t unsupportedVersion = 99U;
    ASSERT_LE(kTexturePayloadVersionOffset + sizeof(unsupportedVersion), unsupportedPayload.size());
    std::memcpy(unsupportedPayload.data() + kTexturePayloadVersionOffset, &unsupportedVersion,
                sizeof(unsupportedVersion));
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, unsupportedPayload));

    TextureAsset output;
    EXPECT_FALSE(LoadTextureAsset(path, output));
    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadTextureAsset_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_texture_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    TextureAsset texture;
    EXPECT_FALSE(LoadTextureAsset(path, texture));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("not a texture file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadTextureAsset_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_texture_asset_tests_nonexistent.uvtex";
    std::filesystem::remove(path);

    TextureAsset texture;
    EXPECT_FALSE(LoadTextureAsset(path, texture));
}

TEST(TextureAssetTest, SaveTextureAsset_RejectsInvalidDescriptorBeforeReplacingDestination) {
    const std::filesystem::path path = "uve_texture_asset_tests_invalid_save.uvtex";
    std::filesystem::remove(path);
    const TextureAsset original = MakeTestTexture();
    ASSERT_TRUE(SaveTextureAsset(original, path));

    TextureAsset invalidPixelCount = original;
    invalidPixelCount.pixels.resize(3);
    EXPECT_FALSE(SaveTextureAsset(invalidPixelCount, path));

    TextureAsset invalidFormat = original;
    invalidFormat.format = static_cast<TextureAssetFormat>(99U);
    EXPECT_FALSE(SaveTextureAsset(invalidFormat, path));

    TextureAsset invalidColorSpace = original;
    invalidColorSpace.colorSpace = static_cast<TextureAssetColorSpace>(99U);
    EXPECT_FALSE(SaveTextureAsset(invalidColorSpace, path));

    TextureAsset invalidUsage = original;
    invalidUsage.usage = static_cast<TextureUsage>(99U);
    EXPECT_FALSE(SaveTextureAsset(invalidUsage, path));

    TextureAsset invalidColorSpaceFormat = original;
    invalidColorSpaceFormat.format = TextureAssetFormat::RGBA16Float;
    invalidColorSpaceFormat.pixels.resize(2U * 2U * 8U);
    EXPECT_FALSE(IsTextureAssetMetadataValid(invalidColorSpaceFormat));
    EXPECT_FALSE(IsTextureAssetValid(invalidColorSpaceFormat));
    EXPECT_FALSE(SaveTextureAsset(invalidColorSpaceFormat, path));

    TextureAsset invalidMipDimensions = original;
    invalidMipDimensions.mipLevels.push_back(
        TextureMipLevel{2U, 1U, std::vector<std::byte>(8U, std::byte{0})});
    EXPECT_FALSE(IsTextureAssetValid(invalidMipDimensions));
    EXPECT_FALSE(SaveTextureAsset(invalidMipDimensions, path));

    TextureAsset invalidMipBytes = original;
    invalidMipBytes.mipLevels.push_back(
        TextureMipLevel{1U, 1U, std::vector<std::byte>(3U, std::byte{0})});
    EXPECT_FALSE(IsTextureAssetValid(invalidMipBytes));
    EXPECT_FALSE(SaveTextureAsset(invalidMipBytes, path));

    TextureAsset invalidDimensions = original;
    invalidDimensions.width = 0U;
    invalidDimensions.height = 0U;
    invalidDimensions.pixels.clear();
    EXPECT_FALSE(SaveTextureAsset(invalidDimensions, path));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAsset(path, loaded));
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadTextureAsset_PixelByteCountMismatch_FailsAndLogsError) {
    const std::filesystem::path path = "uve_texture_asset_tests_bad_pixel_count.uvtex";
    std::filesystem::remove(path);

    std::vector<std::byte> malformedPayload(3U * sizeof(std::uint32_t) + sizeof(std::uint64_t) + 3U);
    const std::uint32_t width = 2U;
    const std::uint32_t height = 2U;
    const std::uint32_t format = static_cast<std::uint32_t>(TextureAssetFormat::RGBA8Unorm);
    const std::uint64_t pixelByteCount = 3U;
    std::size_t offset = 0U;
    std::memcpy(malformedPayload.data() + offset, &width, sizeof(width));
    offset += sizeof(width);
    std::memcpy(malformedPayload.data() + offset, &height, sizeof(height));
    offset += sizeof(height);
    std::memcpy(malformedPayload.data() + offset, &format, sizeof(format));
    offset += sizeof(format);
    std::memcpy(malformedPayload.data() + offset, &pixelByteCount, sizeof(pixelByteCount));
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, malformedPayload));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    TextureAsset loaded;
    EXPECT_FALSE(LoadTextureAsset(path, loaded));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("pixel bytes");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(TextureAssetTest, LoadTextureAsset_RejectsOverflowingPixelByteCountBeforePublication) {
    const std::filesystem::path path = "uve_texture_asset_tests_overflowing_pixel_count.uvtex";
    std::filesystem::remove(path);
    std::vector<std::byte> malformedPayload(3U * sizeof(std::uint32_t) + sizeof(std::uint64_t));
    const std::uint32_t width = 1U;
    const std::uint32_t height = 1U;
    const std::uint32_t format = static_cast<std::uint32_t>(TextureAssetFormat::RGBA8Unorm);
    const std::uint64_t pixelByteCount = std::numeric_limits<std::uint64_t>::max();
    std::size_t offset = 0U;
    std::memcpy(malformedPayload.data() + offset, &width, sizeof(width));
    offset += sizeof(width);
    std::memcpy(malformedPayload.data() + offset, &height, sizeof(height));
    offset += sizeof(height);
    std::memcpy(malformedPayload.data() + offset, &format, sizeof(format));
    offset += sizeof(format);
    std::memcpy(malformedPayload.data() + offset, &pixelByteCount, sizeof(pixelByteCount));
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Texture, malformedPayload));

    const TextureAsset sentinel = MakeTestTexture();
    TextureAsset output = sentinel;
    EXPECT_FALSE(LoadTextureAsset(path, output));
    EXPECT_EQ(output.width, sentinel.width);
    EXPECT_EQ(output.height, sentinel.height);
    EXPECT_EQ(output.format, sentinel.format);
    EXPECT_EQ(output.colorSpace, sentinel.colorSpace);
    EXPECT_EQ(output.usage, sentinel.usage);
    EXPECT_EQ(output.pixels, sentinel.pixels);
    std::filesystem::remove(path);
}

TEST(TextureAssetTest, EndToEnd_RegisterLoaderThenLoad_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_texture_asset_tests_end_to_end.uvtex";
    std::filesystem::remove(path);
    const TextureAsset original = MakeTestTexture();
    ASSERT_TRUE(SaveTextureAsset(original, path));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    AssetManager assetManager(threadPool, eventSystem);
    assetManager.RegisterLoader<TextureAsset>(&LoadTextureAsset);

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<TextureAsset> handle = assetManager.Load<TextureAsset>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReady() || handle.HasFailed();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReady());
    const TextureAsset* const loaded = handle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->colorSpace, original.colorSpace);
    EXPECT_EQ(loaded->usage, original.usage);
    EXPECT_EQ(loaded->pixels, original.pixels);

    std::filesystem::remove(path);
}

namespace {

void AppendBigEndianUint32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 24U;; shift -= 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
        if (shift == 0U) {
            break;
        }
    }
}

/// The CRC-32 a PNG chunk carries, computed here rather than pulled from zlib so this fixture
/// stands on its own: the polynomial is the standard reflected one, seeded and finished the way
/// the format states.
[[nodiscard]] std::uint32_t PngCrc32(const std::vector<std::byte>& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= static_cast<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void AppendPngChunk(std::vector<std::byte>& png, const std::array<char, 4U>& type,
                       const std::vector<std::byte>& payload) {
    AppendBigEndianUint32(png, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> crcInput;
    for (const char character : type) {
        png.push_back(std::byte{static_cast<unsigned char>(character)});
        crcInput.push_back(std::byte{static_cast<unsigned char>(character)});
    }
    png.insert(png.end(), payload.begin(), payload.end());
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    AppendBigEndianUint32(png, PngCrc32(crcInput));
}

/// A 2x2 RGBA8 PNG whose four pixels are all different, so a decode that produced the fallback -
/// or the wrong orientation, or one channel for another - cannot pass as the real picture.
/// Lines: (16,32,48), (64,80,96) / (112,128,144), (160,176,192), each opaque.
[[nodiscard]] std::vector<std::byte> MakeTwoByTwoRgba8Png() {
    std::vector<std::byte> png{std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
                               std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    AppendPngChunk(png, {'I', 'H', 'D', 'R'},
                      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0},
                       std::byte{0}, std::byte{2}, std::byte{8}, std::byte{6}, std::byte{0}, std::byte{0},
                       std::byte{0}});
    const std::array<std::uint8_t, 18U> raw{0U,
                                            16U, 32U, 48U, 255U, 64U, 80U, 96U, 255U,
                                            0U,
                                            112U, 128U, 144U, 255U, 160U, 176U, 192U, 255U};
    // One stored (uncompressed) deflate block, so the fixture needs no compressor to build: a zlib
    // stream header, the block header with its length pair, the scanlines, and the Adler-32 of the
    // data. The decoder accepts stored blocks exactly as it accepts compressed ones.
    std::uint32_t sumA = 1U;
    std::uint32_t sumB = 0U;
    for (const std::uint8_t value : raw) {
        sumA = (sumA + value) % 65521U;
        sumB = (sumB + sumA) % 65521U;
    }
    // LEN and its one's complement, little-endian. Taken as a 16-bit pair first: complementing the
    // two bytes separately is only the same thing while the length is under 256, and a fixture that
    // is quietly wrong for the one size it is used at is a trap for the next size.
    const std::uint16_t blockLength = static_cast<std::uint16_t>(raw.size());
    const auto complement = static_cast<std::uint16_t>(~blockLength);
    const auto byteAt = [](const std::uint16_t value, const unsigned int shift) {
        return std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)};
    };
    std::vector<std::byte> compressed{std::byte{0x78}, std::byte{0x01}, std::byte{0x01},
                                      byteAt(blockLength, 0U), byteAt(blockLength, 8U),
                                      byteAt(complement, 0U), byteAt(complement, 8U)};
    compressed.insert(compressed.end(), reinterpret_cast<const std::byte*>(raw.data()),
                      reinterpret_cast<const std::byte*>(raw.data()) + raw.size());
    AppendBigEndianUint32(compressed, (sumB << 16U) | sumA);
    AppendPngChunk(png, {'I', 'D', 'A', 'T'}, compressed);
    AppendPngChunk(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}

[[nodiscard]] bool WriteTestBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

} // namespace

// A texture slot may hold the source image the author picked rather than a published .uvtex: the
// Inspector's picker offers both, and a raw PNG used to reach the renderer's strict envelope reader,
// fail there, and be replaced by the 1x1 white fallback - a surface showing its flat albedo colour
// where its albedo map should be. The source-aware reader decodes it into the texture the importer
// would have written for it with default settings, mip chain included.
TEST(TextureAssetTest, SourceAwareReaderDecodesARawPngTheStrictReaderRefuses) {
    const std::filesystem::path path = "uve_texture_asset_tests_raw_source.png";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteTestBytes(path, MakeTwoByTwoRgba8Png()));

    // The envelope reader is still the strict one: this file is not a texture envelope, and saying
    // so is the behavior cook and pack rely on.
    TextureAsset strict;
    EXPECT_FALSE(LoadTextureAsset(path, strict));
    EXPECT_EQ(strict.width, 0U);

    TextureAsset decoded;
    ASSERT_TRUE(LoadTextureAssetSource(path, decoded));
    EXPECT_EQ(decoded.width, 2U);
    EXPECT_EQ(decoded.height, 2U);
    EXPECT_EQ(decoded.format, TextureAssetFormat::RGBA8Unorm);
    // What the importer stamps on a colour map by default, so a source read at load time and the
    // same source imported first render identically.
    EXPECT_EQ(decoded.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(decoded.usage, TextureUsage::Color);
    ASSERT_EQ(decoded.pixels.size(), 2U * 2U * 4U);
    EXPECT_EQ(decoded.pixels[0], std::byte{16});
    EXPECT_EQ(decoded.pixels[1], std::byte{32});
    EXPECT_EQ(decoded.pixels[2], std::byte{48});
    EXPECT_EQ(decoded.pixels[3], std::byte{255});
    EXPECT_EQ(decoded.pixels[12], std::byte{160});
    EXPECT_EQ(decoded.pixels[13], std::byte{176});
    EXPECT_EQ(decoded.pixels[14], std::byte{192});
    EXPECT_EQ(decoded.pixels[15], std::byte{255});
    // 2x2 has one level below it; a map without a chain aliases as soon as the surface is not
    // facing the camera, which is the whole reason the importer builds one.
    ASSERT_EQ(decoded.mipLevels.size(), 1U);
    EXPECT_EQ(decoded.mipLevels.front().width, 1U);
    EXPECT_EQ(decoded.mipLevels.front().height, 1U);
    EXPECT_TRUE(IsTextureAssetValid(decoded));

    std::filesystem::remove(path);
}

// A published .uvtex is what a material should hold, and the source-aware reader must not decode,
// re-stamp or otherwise rewrite it: its colour space, usage and mip chain are the importer's answer
// and are read back exactly as the envelope states them.
TEST(TextureAssetTest, SourceAwareReaderStillReadsAPublishedEnvelopeUnchanged) {
    const std::filesystem::path path = "uve_texture_asset_tests_published_source_reader.uvtex";
    std::filesystem::remove(path);
    TextureAsset published = MakeTestTexture();
    ASSERT_TRUE(GenerateTextureMipmaps(published, TextureMipmapFilter::Box));
    ASSERT_TRUE(SaveTextureAsset(published, path));

    TextureAsset loaded;
    ASSERT_TRUE(LoadTextureAssetSource(path, loaded));
    EXPECT_EQ(loaded.width, published.width);
    EXPECT_EQ(loaded.height, published.height);
    EXPECT_EQ(loaded.colorSpace, published.colorSpace);
    EXPECT_EQ(loaded.usage, published.usage);
    EXPECT_EQ(loaded.pixels, published.pixels);
    ASSERT_EQ(loaded.mipLevels.size(), published.mipLevels.size());
    EXPECT_EQ(loaded.mipLevels.front().pixels, published.mipLevels.front().pixels);

    std::filesystem::remove(path);
}

namespace {

/// A colour texture whose every channel holds the same value, so a test can state the height it
/// implies without also stating a saturation.
[[nodiscard]] TextureAsset MakeGreyTexture(const std::vector<std::uint8_t>& values,
                                                const std::uint32_t width, const std::uint32_t height) {
    TextureAsset texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpace::Srgb;
    texture.usage = TextureUsage::Color;
    texture.pixels.resize(values.size() * 4U);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        texture.pixels[index * 4U] = static_cast<std::byte>(values[index]);
        texture.pixels[index * 4U + 1U] = static_cast<std::byte>(values[index]);
        texture.pixels[index * 4U + 2U] = static_cast<std::byte>(values[index]);
        texture.pixels[index * 4U + 3U] = std::byte{0xFF};
    }
    return texture;
}

} // namespace

// A picture carries its shape as brightness, not as geometry. A material given only that picture
// shades like painted glass: the light slides over it and the surface stays flat however it turns.
// The derived normal map is what gives the same picture something for a light to catch, and it is
// derived, not measured - which is why it stays a separate file the author can replace.
TEST(TextureAssetTest, DerivedNormalMapGivesAFlatPictureSomethingToCatchTheLight) {
    // Nothing to derive from a flat colour: the neutral map is the honest answer, and it is also
    // what proves the routine leaves alone what it has nothing to say about.
    TextureAsset neutral;
    ASSERT_TRUE(DeriveNormalTexture(MakeGreyTexture(std::vector<std::uint8_t>(16U, 128U), 4U, 4U), 2.0F,
                                       neutral));
    EXPECT_EQ(neutral.width, 4U);
    EXPECT_EQ(neutral.height, 4U);
    EXPECT_EQ(neutral.format, TextureAssetFormat::RGBA8Unorm);
    // Linear and Normal, not the sRGB colour map it came from: a normal map's bytes are vectors.
    EXPECT_EQ(neutral.colorSpace, TextureAssetColorSpace::Linear);
    EXPECT_EQ(neutral.usage, TextureUsage::Normal);
    ASSERT_TRUE(IsTextureAssetValid(neutral));
    ASSERT_EQ(neutral.pixels.size(), 16U * 4U);
    for (std::size_t index = 0U; index < neutral.pixels.size(); index += 4U) {
        EXPECT_EQ(neutral.pixels[index], std::byte{128});
        EXPECT_EQ(neutral.pixels[index + 1U], std::byte{128});
        EXPECT_EQ(neutral.pixels[index + 2U], std::byte{255});
        EXPECT_EQ(neutral.pixels[index + 3U], std::byte{255});
    }
    ASSERT_FALSE(neutral.mipLevels.empty());
    EXPECT_EQ(neutral.mipLevels.front().width, 2U);

    // Dark on the left, bright on the right: the surface turns to face the dark side, so red sits
    // under the neutral 128 and blue takes up the rest of the byte. A slope of one at strength two
    // is a 45-degree turn.
    TextureAsset sloped;
    ASSERT_TRUE(DeriveNormalTexture(MakeGreyTexture(std::vector<std::uint8_t>{0U, 128U, 255U}, 3U, 1U),
                                       2.0F, sloped));
    EXPECT_EQ(sloped.pixels[4], std::byte{37});
    EXPECT_EQ(sloped.pixels[5], std::byte{128});
    EXPECT_EQ(sloped.pixels[6], std::byte{218});

    // Green is +Y in the image, the convention the lit shader samples, so bright over dark tilts
    // the surface away from the viewer and reads as green under 128 rather than over it.
    ASSERT_TRUE(DeriveNormalTexture(MakeGreyTexture(std::vector<std::uint8_t>{255U, 128U, 0U}, 1U, 3U),
                                       2.0F, sloped));
    EXPECT_EQ(sloped.pixels[4], std::byte{128});
    EXPECT_EQ(sloped.pixels[5], std::byte{37});
    EXPECT_EQ(sloped.pixels[6], std::byte{218});

    // Strength is the slope and nothing else: zero derives the same map a flat picture would.
    TextureAsset unscaled;
    ASSERT_TRUE(DeriveNormalTexture(MakeGreyTexture(std::vector<std::uint8_t>{0U, 128U, 255U}, 3U, 1U),
                                       0.0F, unscaled));
    EXPECT_EQ(unscaled.pixels[4], std::byte{128});
    EXPECT_EQ(unscaled.pixels[6], std::byte{255});
}

// The refusals matter as much as the derivation: a caller that saved the untouched output would
// write a texture with no pixels over whatever the author had.
TEST(TextureAssetTest, DerivedNormalMapRefusesWhatItCannotDeriveFromAndLeavesTheOutputAlone) {
    const TextureAsset source = MakeGreyTexture(std::vector<std::uint8_t>(4U, 128U), 2U, 2U);
    TextureAsset output;
    EXPECT_FALSE(DeriveNormalTexture(source, -1.0F, output));
    EXPECT_FALSE(DeriveNormalTexture(source, 17.0F, output));
    EXPECT_TRUE(output.pixels.empty());

    TextureAsset ragged = source;
    ragged.pixels.pop_back();
    EXPECT_FALSE(DeriveNormalTexture(ragged, 2.0F, output));

    TextureAsset halfFloat = source;
    halfFloat.format = TextureAssetFormat::RGBA16Float;
    EXPECT_FALSE(DeriveNormalTexture(halfFloat, 2.0F, output));

    TextureAsset empty = source;
    empty.width = 0U;
    empty.pixels.clear();
    EXPECT_FALSE(DeriveNormalTexture(empty, 2.0F, output));
    EXPECT_TRUE(output.pixels.empty());
}

} // namespace
} // namespace UVE::Asset::Tests
