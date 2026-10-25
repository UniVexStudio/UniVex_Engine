// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_import_settings_uve.h"
#include "uve/asset/texture_mipmap_uve.h"
#include "uve/asset/texture_compression_uve.h"

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

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/utilities/binary_buffer_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] TextureAssetUVE MakeTestTextureUVE() {
    TextureAssetUVE texture;
    texture.width = 2;
    texture.height = 2;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    texture.usage = TextureUsageUVE::Color;
    texture.pixels.resize(2 * 2 * 4);
    for (std::size_t index = 0; index < texture.pixels.size(); ++index) {
        texture.pixels[index] = static_cast<std::byte>(index);
    }
    return texture;
}

[[nodiscard]] std::vector<std::byte> MakeLegacyTexturePayloadUVE(const TextureAssetUVE& texture) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32LeUVE(payload, texture.width);
    Utilities::AppendUint32LeUVE(payload, texture.height);
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());
    return payload;
}

[[nodiscard]] std::vector<std::byte> MakeVersionOneTexturePayloadUVE(const TextureAssetUVE& texture) {
    constexpr std::array<char, 4U> kMagic{'U', 'V', 'T', 'X'};
    std::vector<std::byte> payload;
    Utilities::AppendBytesUVE(payload, kMagic.data(), kMagic.size());
    Utilities::AppendUint32LeUVE(payload, 1U);
    Utilities::AppendUint32LeUVE(payload, texture.width);
    Utilities::AppendUint32LeUVE(payload, texture.height);
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());
    return payload;
}

[[nodiscard]] std::vector<std::byte> MakeVersionTwoTexturePayloadUVE(const TextureAssetUVE& texture) {
    constexpr std::array<char, 4U> kMagic{'U', 'V', 'T', 'X'};
    std::vector<std::byte> payload;
    Utilities::AppendBytesUVE(payload, kMagic.data(), kMagic.size());
    Utilities::AppendUint32LeUVE(payload, 2U);
    Utilities::AppendUint32LeUVE(payload, texture.width);
    Utilities::AppendUint32LeUVE(payload, texture.height);
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.colorSpace));
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.usage));
    Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(texture.mipLevels.size()));
    for (const TextureMipLevelUVE& mip : texture.mipLevels) {
        Utilities::AppendUint32LeUVE(payload, mip.width);
        Utilities::AppendUint32LeUVE(payload, mip.height);
        Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(mip.pixels.size()));
        Utilities::AppendBytesUVE(payload, mip.pixels.data(), mip.pixels.size());
    }
    return payload;
}

TEST(TextureImportSettingsUVETest, CacheVersionIncludesColorSpaceUsageAndMipmapPolicy) {
    TextureImportSettingsUVE defaultSettings;
    TextureImportSettingsUVE sameSettings;
    TextureImportSettingsUVE linearNormalSettings;
    linearNormalSettings.colorSpace = TextureAssetColorSpaceUVE::Linear;
    linearNormalSettings.usage = TextureUsageUVE::Normal;
    linearNormalSettings.generateMipmaps = false;
    TextureImportSettingsUVE nearestSettings;
    nearestSettings.mipFilter = TextureMipmapFilterUVE::Nearest;
    TextureImportSettingsUVE cappedSettings;
    cappedSettings.maxMipLevels = 3U;
    TextureImportSettingsUVE compressedSettings;
    compressedSettings.compressionMode = TextureCompressionModeUVE::BasisETC1S;
    TextureImportSettingsUVE differentQualitySettings = compressedSettings;
    differentQualitySettings.compressionQuality = 70U;
    TextureImportSettingsUVE differentEffortSettings = compressedSettings;
    differentEffortSettings.compressionEffort = 8U;

    EXPECT_EQ(defaultSettings.GetCacheVersionUVE(),
              "texture-import-v3;color-space=1;usage=1;generate-mipmaps=1;mip-filter=0;max-levels=0;compression=0;compression-quality=85;compression-effort=5");
    EXPECT_EQ(defaultSettings.GetCacheVersionUVE(), sameSettings.GetCacheVersionUVE());
    EXPECT_NE(defaultSettings.GetCacheVersionUVE(), linearNormalSettings.GetCacheVersionUVE());
    EXPECT_NE(defaultSettings.GetCacheVersionUVE(), nearestSettings.GetCacheVersionUVE());
    EXPECT_NE(defaultSettings.GetCacheVersionUVE(), cappedSettings.GetCacheVersionUVE());
    EXPECT_NE(defaultSettings.GetCacheVersionUVE(), compressedSettings.GetCacheVersionUVE());
    EXPECT_NE(compressedSettings.GetCacheVersionUVE(), differentQualitySettings.GetCacheVersionUVE());
    EXPECT_NE(compressedSettings.GetCacheVersionUVE(), differentEffortSettings.GetCacheVersionUVE());
}

TEST(TextureAssetUVETest, DefaultMetadataIsLinearGenericAndRaw) {
    const TextureAssetUVE texture{};
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpaceUVE::Linear);
    EXPECT_EQ(texture.usage, TextureUsageUVE::Generic);
    EXPECT_EQ(texture.payloadEncoding, TexturePayloadEncodingUVE::RawPixels);
    EXPECT_TRUE(texture.basisKtx2Data.empty());
}

TEST(TextureMipmapUVETest, BoxFilterAveragesSrgbColorInLinearSpace) {
    TextureAssetUVE srgbTexture;
    srgbTexture.width = 2U;
    srgbTexture.height = 2U;
    srgbTexture.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    srgbTexture.usage = TextureUsageUVE::Color;
    srgbTexture.pixels = {
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255},
        std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
    };
    ASSERT_TRUE(GenerateTextureMipmapsUVE(srgbTexture, TextureMipmapFilterUVE::Box));
    ASSERT_EQ(srgbTexture.mipLevels.size(), 1U);
    ASSERT_EQ(srgbTexture.mipLevels[0].pixels.size(), 4U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[0]), 188U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[1]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[2]), 0U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(srgbTexture.mipLevels[0].pixels[3]), 255U);

    TextureAssetUVE linearTexture = srgbTexture;
    linearTexture.colorSpace = TextureAssetColorSpaceUVE::Linear;
    linearTexture.mipLevels.clear();
    ASSERT_TRUE(GenerateTextureMipmapsUVE(linearTexture, TextureMipmapFilterUVE::Box));
    EXPECT_EQ(std::to_integer<std::uint8_t>(linearTexture.mipLevels[0].pixels[0]), 128U);
}

TEST(TextureMipmapUVETest, NearestFilterAndLevelCapAreHonored) {
    TextureAssetUVE texture;
    texture.width = 4U;
    texture.height = 2U;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.pixels.resize(4U * 2U * 4U);
    for (std::size_t index = 0U; index < texture.pixels.size(); ++index) {
        texture.pixels[index] = static_cast<std::byte>(index);
    }

    ASSERT_TRUE(GenerateTextureMipmapsUVE(texture, TextureMipmapFilterUVE::Nearest, 2U));
    ASSERT_EQ(texture.mipLevels.size(), 1U);
    EXPECT_EQ(texture.mipLevels[0].width, 2U);
    EXPECT_EQ(texture.mipLevels[0].height, 1U);
    EXPECT_EQ(texture.mipLevels[0].pixels.size(), 8U);

    ASSERT_TRUE(GenerateTextureMipmapsUVE(texture, TextureMipmapFilterUVE::Box, 1U));
    EXPECT_TRUE(texture.mipLevels.empty());
}

TEST(TextureAssetUVETest, BytesPerPixelUVE_ReturnsExpectedValues) {
    EXPECT_EQ(BytesPerPixelUVE(TextureAssetFormatUVE::RGBA8Unorm), 4U);
    EXPECT_EQ(BytesPerPixelUVE(TextureAssetFormatUVE::RGBA16Float), 8U);
}

#if defined(UVE_HAS_BASIS_ENCODER) && UVE_HAS_BASIS_ENCODER
TEST(TextureCompressionUVETest, EncodesBothBasisModesAndTranscodesEveryMip) {
    for (const TextureCompressionModeUVE mode : {TextureCompressionModeUVE::BasisETC1S,
                                                  TextureCompressionModeUVE::BasisUASTC}) {
        TextureAssetUVE source;
        source.width = 7U;
        source.height = 5U;
        source.format = TextureAssetFormatUVE::RGBA8Unorm;
        source.colorSpace = TextureAssetColorSpaceUVE::Srgb;
        source.usage = TextureUsageUVE::Color;
        source.pixels.resize(static_cast<std::size_t>(source.width) * source.height * 4U);
        for (std::size_t pixel = 0U; pixel < source.pixels.size() / 4U; ++pixel) {
            source.pixels[pixel * 4U] = static_cast<std::byte>((pixel * 19U) & 0xFFU);
            source.pixels[pixel * 4U + 1U] = static_cast<std::byte>((pixel * 37U) & 0xFFU);
            source.pixels[pixel * 4U + 2U] = static_cast<std::byte>((pixel * 53U) & 0xFFU);
            source.pixels[pixel * 4U + 3U] = std::byte{0xFF};
        }
        ASSERT_TRUE(GenerateTextureMipmapsUVE(source, TextureMipmapFilterUVE::Box));
        ASSERT_EQ(source.mipLevels.size(), 2U);
        ASSERT_TRUE(CompressTextureAssetWithBasisUVE(source, mode, 60U, 2U));
        ASSERT_EQ(source.payloadEncoding, TexturePayloadEncodingUVE::BasisUniversalKtx2);
        EXPECT_TRUE(source.pixels.empty());
        EXPECT_TRUE(source.mipLevels.empty());
        ASSERT_TRUE(IsTextureAssetValidUVE(source));

        TextureCompressionInfoUVE info;
        ASSERT_TRUE(GetTextureCompressionInfoUVE(source, info));
        EXPECT_EQ(info.width, 7U);
        EXPECT_EQ(info.height, 5U);
        EXPECT_EQ(info.mipLevels, 3U);
        EXPECT_FALSE(info.hasAlpha);

        TextureTranscodedMipChainUVE rgba;
        ASSERT_TRUE(TranscodeTextureAssetUVE(source, TextureTranscodeTargetUVE::Rgba8Unorm, rgba));
        EXPECT_EQ(rgba.width, 7U);
        EXPECT_EQ(rgba.height, 5U);
        EXPECT_EQ(rgba.mipLevels, 3U);
        EXPECT_EQ(rgba.pixels.size(), 168U);

        TextureTranscodedMipChainUVE bc7;
        ASSERT_TRUE(TranscodeTextureAssetUVE(source, TextureTranscodeTargetUVE::Bc7Rgba, bc7));
        EXPECT_EQ(bc7.pixels.size(), 96U);

        const std::filesystem::path path = mode == TextureCompressionModeUVE::BasisETC1S
                                               ? "uve_texture_asset_tests_etc1s.uvtex"
                                               : "uve_texture_asset_tests_uastc.uvtex";
        std::filesystem::remove(path);
        ASSERT_TRUE(SaveTextureAssetUVE(source, path));
        TextureAssetUVE loaded;
        ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));
        EXPECT_EQ(loaded.payloadEncoding, TexturePayloadEncodingUVE::BasisUniversalKtx2);
        EXPECT_EQ(loaded.colorSpace, source.colorSpace);
        EXPECT_EQ(loaded.usage, source.usage);
        EXPECT_EQ(loaded.basisKtx2Data, source.basisKtx2Data);
        std::filesystem::remove(path);
    }
}

TEST(TextureCompressionUVETest, RejectsAlphaDroppingTargetsWithoutChangingOutput) {
    TextureAssetUVE source;
    source.width = 4U;
    source.height = 4U;
    source.format = TextureAssetFormatUVE::RGBA8Unorm;
    source.pixels.resize(4U * 4U * 4U);
    for (std::size_t pixel = 0U; pixel < source.pixels.size() / 4U; ++pixel) {
        source.pixels[pixel * 4U] = std::byte{0x20};
        source.pixels[pixel * 4U + 1U] = std::byte{0x80};
        source.pixels[pixel * 4U + 2U] = std::byte{0xC0};
        source.pixels[pixel * 4U + 3U] = std::byte{0x80};
    }
    ASSERT_TRUE(CompressTextureAssetWithBasisUVE(source, TextureCompressionModeUVE::BasisUASTC, 60U, 1U));

    TextureCompressionInfoUVE info;
    ASSERT_TRUE(GetTextureCompressionInfoUVE(source, info));
    EXPECT_TRUE(info.hasAlpha);
    TextureTranscodedMipChainUVE output;
    output.width = 99U;
    output.pixels = {std::byte{0xAB}};
    EXPECT_FALSE(TranscodeTextureAssetUVE(source, TextureTranscodeTargetUVE::Bc1Rgb, output));
    EXPECT_EQ(output.width, 99U);
    EXPECT_EQ(output.pixels, (std::vector<std::byte>{std::byte{0xAB}}));

    ASSERT_TRUE(TranscodeTextureAssetUVE(source, TextureTranscodeTargetUVE::Bc3Rgba, output));
    EXPECT_EQ(output.pixels.size(), 16U);
}
#endif

TEST(TextureAssetUVETest, SaveThenLoad_RoundTripsByteExact) {
    const std::filesystem::path path = "uve_texture_asset_tests_round_trip.uvtex";
    std::filesystem::remove(path);
    const TextureAssetUVE original = MakeTestTextureUVE();
    ASSERT_TRUE(SaveTextureAssetUVE(original, path));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));

    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.format, original.format);
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_TRUE(IsTextureAssetValidUVE(loaded));

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, SaveThenLoad_RoundTripsAllMipLevels) {
    const std::filesystem::path path = "uve_texture_asset_tests_mip_chain.uvtex";
    std::filesystem::remove(path);
    TextureAssetUVE original = MakeTestTextureUVE();
    original.mipLevels.push_back(TextureMipLevelUVE{1U, 1U,
        std::vector<std::byte>{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}}});
    ASSERT_TRUE(IsTextureAssetValidUVE(original));
    ASSERT_TRUE(SaveTextureAssetUVE(original, path));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));
    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_EQ(loaded.mipLevels, original.mipLevels);

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadVersionOnePayload_PreservesMetadataAndDefaultsToOneLevel) {
    const std::filesystem::path path = "uve_texture_asset_tests_v1_payload.uvtex";
    std::filesystem::remove(path);
    const TextureAssetUVE original = MakeTestTextureUVE();
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, MakeVersionOneTexturePayloadUVE(original)));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_TRUE(loaded.mipLevels.empty());

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadVersionTwoPayload_PreservesRawMipChain) {
    const std::filesystem::path path = "uve_texture_asset_tests_v2_payload.uvtex";
    std::filesystem::remove(path);
    TextureAssetUVE original = MakeTestTextureUVE();
    original.mipLevels.push_back(TextureMipLevelUVE{
        1U, 1U, std::vector<std::byte>{std::byte{0x20}, std::byte{0x40}, std::byte{0x80}, std::byte{0xFF}}});
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, MakeVersionTwoTexturePayloadUVE(original)));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));
    EXPECT_EQ(loaded.payloadEncoding, TexturePayloadEncodingUVE::RawPixels);
    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    EXPECT_EQ(loaded.mipLevels, original.mipLevels);

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadLegacyPayload_DefaultsNewMetadataWithoutChangingPixels) {
    const std::filesystem::path path = "uve_texture_asset_tests_legacy_payload.uvtex";
    std::filesystem::remove(path);
    const TextureAssetUVE original = MakeTestTextureUVE();
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, MakeLegacyTexturePayloadUVE(original)));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));

    EXPECT_EQ(loaded.width, original.width);
    EXPECT_EQ(loaded.height, original.height);
    EXPECT_EQ(loaded.format, original.format);
    EXPECT_EQ(loaded.colorSpace, TextureAssetColorSpaceUVE::Linear);
    EXPECT_EQ(loaded.usage, TextureUsageUVE::Generic);
    EXPECT_EQ(loaded.pixels, original.pixels);

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadVersionedPayload_RejectsUnknownMetadataWithoutPublishingPartialOutput) {
    const std::filesystem::path path = "uve_texture_asset_tests_invalid_metadata.uvtex";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveTextureAssetUVE(MakeTestTextureUVE(), path));

    const auto file = ReadUveFileUVE(path);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> malformedPayload = file->second;
    constexpr std::size_t kTextureUsageOffsetUVE = 4U + 4U + 4U + 4U + 4U + 4U;
    const std::uint32_t unknownUsage = 99U;
    ASSERT_LE(kTextureUsageOffsetUVE + sizeof(unknownUsage), malformedPayload.size());
    std::memcpy(malformedPayload.data() + kTextureUsageOffsetUVE, &unknownUsage, sizeof(unknownUsage));
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, malformedPayload));

    const TextureAssetUVE sentinel = MakeTestTextureUVE();
    TextureAssetUVE output = sentinel;
    EXPECT_FALSE(LoadTextureAssetUVE(path, output));
    EXPECT_EQ(output.width, sentinel.width);
    EXPECT_EQ(output.height, sentinel.height);
    EXPECT_EQ(output.format, sentinel.format);
    EXPECT_EQ(output.colorSpace, sentinel.colorSpace);
    EXPECT_EQ(output.usage, sentinel.usage);
    EXPECT_EQ(output.pixels, sentinel.pixels);

    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadVersionedPayload_RejectsUnsupportedVersion) {
    const std::filesystem::path path = "uve_texture_asset_tests_unsupported_version.uvtex";
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveTextureAssetUVE(MakeTestTextureUVE(), path));

    const auto file = ReadUveFileUVE(path);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> unsupportedPayload = file->second;
    constexpr std::size_t kTexturePayloadVersionOffsetUVE = 4U;
    const std::uint32_t unsupportedVersion = 99U;
    ASSERT_LE(kTexturePayloadVersionOffsetUVE + sizeof(unsupportedVersion), unsupportedPayload.size());
    std::memcpy(unsupportedPayload.data() + kTexturePayloadVersionOffsetUVE, &unsupportedVersion,
                sizeof(unsupportedVersion));
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, unsupportedPayload));

    TextureAssetUVE output;
    EXPECT_FALSE(LoadTextureAssetUVE(path, output));
    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadTextureAssetUVE_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_texture_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Blob, {}));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    TextureAssetUVE texture;
    EXPECT_FALSE(LoadTextureAssetUVE(path, texture));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("not a texture file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadTextureAssetUVE_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_texture_asset_tests_nonexistent.uvtex";
    std::filesystem::remove(path);

    TextureAssetUVE texture;
    EXPECT_FALSE(LoadTextureAssetUVE(path, texture));
}

TEST(TextureAssetUVETest, SaveTextureAssetUVE_RejectsInvalidDescriptorBeforeReplacingDestination) {
    const std::filesystem::path path = "uve_texture_asset_tests_invalid_save.uvtex";
    std::filesystem::remove(path);
    const TextureAssetUVE original = MakeTestTextureUVE();
    ASSERT_TRUE(SaveTextureAssetUVE(original, path));

    TextureAssetUVE invalidPixelCount = original;
    invalidPixelCount.pixels.resize(3);
    EXPECT_FALSE(SaveTextureAssetUVE(invalidPixelCount, path));

    TextureAssetUVE invalidFormat = original;
    invalidFormat.format = static_cast<TextureAssetFormatUVE>(99U);
    EXPECT_FALSE(SaveTextureAssetUVE(invalidFormat, path));

    TextureAssetUVE invalidColorSpace = original;
    invalidColorSpace.colorSpace = static_cast<TextureAssetColorSpaceUVE>(99U);
    EXPECT_FALSE(SaveTextureAssetUVE(invalidColorSpace, path));

    TextureAssetUVE invalidUsage = original;
    invalidUsage.usage = static_cast<TextureUsageUVE>(99U);
    EXPECT_FALSE(SaveTextureAssetUVE(invalidUsage, path));

    TextureAssetUVE invalidColorSpaceFormat = original;
    invalidColorSpaceFormat.format = TextureAssetFormatUVE::RGBA16Float;
    invalidColorSpaceFormat.pixels.resize(2U * 2U * 8U);
    EXPECT_FALSE(IsTextureAssetMetadataValidUVE(invalidColorSpaceFormat));
    EXPECT_FALSE(IsTextureAssetValidUVE(invalidColorSpaceFormat));
    EXPECT_FALSE(SaveTextureAssetUVE(invalidColorSpaceFormat, path));

    TextureAssetUVE invalidMipDimensions = original;
    invalidMipDimensions.mipLevels.push_back(
        TextureMipLevelUVE{2U, 1U, std::vector<std::byte>(8U, std::byte{0})});
    EXPECT_FALSE(IsTextureAssetValidUVE(invalidMipDimensions));
    EXPECT_FALSE(SaveTextureAssetUVE(invalidMipDimensions, path));

    TextureAssetUVE invalidMipBytes = original;
    invalidMipBytes.mipLevels.push_back(
        TextureMipLevelUVE{1U, 1U, std::vector<std::byte>(3U, std::byte{0})});
    EXPECT_FALSE(IsTextureAssetValidUVE(invalidMipBytes));
    EXPECT_FALSE(SaveTextureAssetUVE(invalidMipBytes, path));

    TextureAssetUVE invalidDimensions = original;
    invalidDimensions.width = 0U;
    invalidDimensions.height = 0U;
    invalidDimensions.pixels.clear();
    EXPECT_FALSE(SaveTextureAssetUVE(invalidDimensions, path));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetUVE(path, loaded));
    EXPECT_EQ(loaded.colorSpace, original.colorSpace);
    EXPECT_EQ(loaded.usage, original.usage);
    EXPECT_EQ(loaded.pixels, original.pixels);
    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadTextureAssetUVE_PixelByteCountMismatch_FailsAndLogsError) {
    const std::filesystem::path path = "uve_texture_asset_tests_bad_pixel_count.uvtex";
    std::filesystem::remove(path);

    std::vector<std::byte> malformedPayload(3U * sizeof(std::uint32_t) + sizeof(std::uint64_t) + 3U);
    const std::uint32_t width = 2U;
    const std::uint32_t height = 2U;
    const std::uint32_t format = static_cast<std::uint32_t>(TextureAssetFormatUVE::RGBA8Unorm);
    const std::uint64_t pixelByteCount = 3U;
    std::size_t offset = 0U;
    std::memcpy(malformedPayload.data() + offset, &width, sizeof(width));
    offset += sizeof(width);
    std::memcpy(malformedPayload.data() + offset, &height, sizeof(height));
    offset += sizeof(height);
    std::memcpy(malformedPayload.data() + offset, &format, sizeof(format));
    offset += sizeof(format);
    std::memcpy(malformedPayload.data() + offset, &pixelByteCount, sizeof(pixelByteCount));
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, malformedPayload));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    TextureAssetUVE loaded;
    EXPECT_FALSE(LoadTextureAssetUVE(path, loaded));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("pixel bytes");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, LoadTextureAssetUVE_RejectsOverflowingPixelByteCountBeforePublication) {
    const std::filesystem::path path = "uve_texture_asset_tests_overflowing_pixel_count.uvtex";
    std::filesystem::remove(path);
    std::vector<std::byte> malformedPayload(3U * sizeof(std::uint32_t) + sizeof(std::uint64_t));
    const std::uint32_t width = 1U;
    const std::uint32_t height = 1U;
    const std::uint32_t format = static_cast<std::uint32_t>(TextureAssetFormatUVE::RGBA8Unorm);
    const std::uint64_t pixelByteCount = std::numeric_limits<std::uint64_t>::max();
    std::size_t offset = 0U;
    std::memcpy(malformedPayload.data() + offset, &width, sizeof(width));
    offset += sizeof(width);
    std::memcpy(malformedPayload.data() + offset, &height, sizeof(height));
    offset += sizeof(height);
    std::memcpy(malformedPayload.data() + offset, &format, sizeof(format));
    offset += sizeof(format);
    std::memcpy(malformedPayload.data() + offset, &pixelByteCount, sizeof(pixelByteCount));
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Texture, malformedPayload));

    const TextureAssetUVE sentinel = MakeTestTextureUVE();
    TextureAssetUVE output = sentinel;
    EXPECT_FALSE(LoadTextureAssetUVE(path, output));
    EXPECT_EQ(output.width, sentinel.width);
    EXPECT_EQ(output.height, sentinel.height);
    EXPECT_EQ(output.format, sentinel.format);
    EXPECT_EQ(output.colorSpace, sentinel.colorSpace);
    EXPECT_EQ(output.usage, sentinel.usage);
    EXPECT_EQ(output.pixels, sentinel.pixels);
    std::filesystem::remove(path);
}

TEST(TextureAssetUVETest, EndToEnd_RegisterLoaderThenLoadUVE_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_texture_asset_tests_end_to_end.uvtex";
    std::filesystem::remove(path);
    const TextureAssetUVE original = MakeTestTextureUVE();
    ASSERT_TRUE(SaveTextureAssetUVE(original, path));

    Threading::ThreadPoolUVE threadPool(2);
    Events::EventSystemUVE eventSystem;
    AssetDatabaseUVE assetDatabase;
    AssetManagerUVE assetManager(threadPool, eventSystem);
    assetManager.RegisterLoaderUVE<TextureAssetUVE>(&LoadTextureAssetUVE);

    const AssetGuidUVE guid = assetDatabase.RegisterUVE(path);
    const AssetHandleUVE<TextureAssetUVE> handle = assetManager.LoadUVE<TextureAssetUVE>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReadyUVE() || handle.HasFailedUVE();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReadyUVE());
    const TextureAssetUVE* const loaded = handle.TryGetUVE();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->colorSpace, original.colorSpace);
    EXPECT_EQ(loaded->usage, original.usage);
    EXPECT_EQ(loaded->pixels, original.pixels);

    std::filesystem::remove(path);
}

namespace {

void AppendBigEndianUint32UVE(std::vector<std::byte>& bytes, const std::uint32_t value) {
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
[[nodiscard]] std::uint32_t PngCrc32UVE(const std::vector<std::byte>& bytes) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= static_cast<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void AppendPngChunkUVE(std::vector<std::byte>& png, const std::array<char, 4U>& type,
                       const std::vector<std::byte>& payload) {
    AppendBigEndianUint32UVE(png, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> crcInput;
    for (const char character : type) {
        png.push_back(std::byte{static_cast<unsigned char>(character)});
        crcInput.push_back(std::byte{static_cast<unsigned char>(character)});
    }
    png.insert(png.end(), payload.begin(), payload.end());
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    AppendBigEndianUint32UVE(png, PngCrc32UVE(crcInput));
}

/// A 2x2 RGBA8 PNG whose four pixels are all different, so a decode that produced the fallback -
/// or the wrong orientation, or one channel for another - cannot pass as the real picture.
/// Lines: (16,32,48), (64,80,96) / (112,128,144), (160,176,192), each opaque.
[[nodiscard]] std::vector<std::byte> MakeTwoByTwoRgba8PngUVE() {
    std::vector<std::byte> png{std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
                               std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    AppendPngChunkUVE(png, {'I', 'H', 'D', 'R'},
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
    const auto lengthLow = static_cast<unsigned char>(raw.size() & 0xFFU);
    const auto lengthHigh = static_cast<unsigned char>((raw.size() >> 8U) & 0xFFU);
    std::vector<std::byte> compressed{std::byte{0x78}, std::byte{0x01}, std::byte{0x01},
                                      std::byte{lengthLow}, std::byte{lengthHigh},
                                      std::byte{static_cast<unsigned char>(~lengthLow & 0xFFU)},
                                      std::byte{static_cast<unsigned char>(~lengthHigh & 0xFFU)}};
    compressed.insert(compressed.end(), reinterpret_cast<const std::byte*>(raw.data()),
                      reinterpret_cast<const std::byte*>(raw.data()) + raw.size());
    AppendBigEndianUint32UVE(compressed, (sumB << 16U) | sumA);
    AppendPngChunkUVE(png, {'I', 'D', 'A', 'T'}, compressed);
    AppendPngChunkUVE(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}

[[nodiscard]] bool WriteTestBytesUVE(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
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
TEST(TextureAssetUVETest, SourceAwareReaderDecodesARawPngTheStrictReaderRefuses) {
    const std::filesystem::path path = "uve_texture_asset_tests_raw_source.png";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteTestBytesUVE(path, MakeTwoByTwoRgba8PngUVE()));

    // The envelope reader is still the strict one: this file is not a texture envelope, and saying
    // so is the behavior cook and pack rely on.
    TextureAssetUVE strict;
    EXPECT_FALSE(LoadTextureAssetUVE(path, strict));
    EXPECT_EQ(strict.width, 0U);

    TextureAssetUVE decoded;
    ASSERT_TRUE(LoadTextureAssetSourceUVE(path, decoded));
    EXPECT_EQ(decoded.width, 2U);
    EXPECT_EQ(decoded.height, 2U);
    EXPECT_EQ(decoded.format, TextureAssetFormatUVE::RGBA8Unorm);
    // What the importer stamps on a colour map by default, so a source read at load time and the
    // same source imported first render identically.
    EXPECT_EQ(decoded.colorSpace, TextureAssetColorSpaceUVE::Srgb);
    EXPECT_EQ(decoded.usage, TextureUsageUVE::Color);
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
    EXPECT_TRUE(IsTextureAssetValidUVE(decoded));

    std::filesystem::remove(path);
}

// A published .uvtex is what a material should hold, and the source-aware reader must not decode,
// re-stamp or otherwise rewrite it: its colour space, usage and mip chain are the importer's answer
// and are read back exactly as the envelope states them.
TEST(TextureAssetUVETest, SourceAwareReaderStillReadsAPublishedEnvelopeUnchanged) {
    const std::filesystem::path path = "uve_texture_asset_tests_published_source_reader.uvtex";
    std::filesystem::remove(path);
    TextureAssetUVE published = MakeTestTextureUVE();
    ASSERT_TRUE(GenerateTextureMipmapsUVE(published, TextureMipmapFilterUVE::Box));
    ASSERT_TRUE(SaveTextureAssetUVE(published, path));

    TextureAssetUVE loaded;
    ASSERT_TRUE(LoadTextureAssetSourceUVE(path, loaded));
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
[[nodiscard]] TextureAssetUVE MakeGreyTextureUVE(const std::vector<std::uint8_t>& values,
                                                const std::uint32_t width, const std::uint32_t height) {
    TextureAssetUVE texture;
    texture.width = width;
    texture.height = height;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    texture.usage = TextureUsageUVE::Color;
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
TEST(TextureAssetUVETest, DerivedNormalMapGivesAFlatPictureSomethingToCatchTheLight) {
    // Nothing to derive from a flat colour: the neutral map is the honest answer, and it is also
    // what proves the routine leaves alone what it has nothing to say about.
    TextureAssetUVE neutral;
    ASSERT_TRUE(DeriveNormalTextureUVE(MakeGreyTextureUVE(std::vector<std::uint8_t>(16U, 128U), 4U, 4U), 2.0F,
                                       neutral));
    EXPECT_EQ(neutral.width, 4U);
    EXPECT_EQ(neutral.height, 4U);
    EXPECT_EQ(neutral.format, TextureAssetFormatUVE::RGBA8Unorm);
    // Linear and Normal, not the sRGB colour map it came from: a normal map's bytes are vectors.
    EXPECT_EQ(neutral.colorSpace, TextureAssetColorSpaceUVE::Linear);
    EXPECT_EQ(neutral.usage, TextureUsageUVE::Normal);
    ASSERT_TRUE(IsTextureAssetValidUVE(neutral));
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
    TextureAssetUVE sloped;
    ASSERT_TRUE(DeriveNormalTextureUVE(MakeGreyTextureUVE(std::vector<std::uint8_t>{0U, 128U, 255U}, 3U, 1U),
                                       2.0F, sloped));
    EXPECT_EQ(sloped.pixels[4], std::byte{37});
    EXPECT_EQ(sloped.pixels[5], std::byte{128});
    EXPECT_EQ(sloped.pixels[6], std::byte{218});

    // Green is +Y in the image, the convention the lit shader samples, so bright over dark tilts
    // the surface away from the viewer and reads as green under 128 rather than over it.
    ASSERT_TRUE(DeriveNormalTextureUVE(MakeGreyTextureUVE(std::vector<std::uint8_t>{255U, 128U, 0U}, 1U, 3U),
                                       2.0F, sloped));
    EXPECT_EQ(sloped.pixels[4], std::byte{128});
    EXPECT_EQ(sloped.pixels[5], std::byte{37});
    EXPECT_EQ(sloped.pixels[6], std::byte{218});

    // Strength is the slope and nothing else: zero derives the same map a flat picture would.
    TextureAssetUVE unscaled;
    ASSERT_TRUE(DeriveNormalTextureUVE(MakeGreyTextureUVE(std::vector<std::uint8_t>{0U, 128U, 255U}, 3U, 1U),
                                       0.0F, unscaled));
    EXPECT_EQ(unscaled.pixels[4], std::byte{128});
    EXPECT_EQ(unscaled.pixels[6], std::byte{255});
}

// The refusals matter as much as the derivation: a caller that saved the untouched output would
// write a texture with no pixels over whatever the author had.
TEST(TextureAssetUVETest, DerivedNormalMapRefusesWhatItCannotDeriveFromAndLeavesTheOutputAlone) {
    const TextureAssetUVE source = MakeGreyTextureUVE(std::vector<std::uint8_t>(4U, 128U), 2U, 2U);
    TextureAssetUVE output;
    EXPECT_FALSE(DeriveNormalTextureUVE(source, -1.0F, output));
    EXPECT_FALSE(DeriveNormalTextureUVE(source, 17.0F, output));
    EXPECT_TRUE(output.pixels.empty());

    TextureAssetUVE ragged = source;
    ragged.pixels.pop_back();
    EXPECT_FALSE(DeriveNormalTextureUVE(ragged, 2.0F, output));

    TextureAssetUVE halfFloat = source;
    halfFloat.format = TextureAssetFormatUVE::RGBA16Float;
    EXPECT_FALSE(DeriveNormalTextureUVE(halfFloat, 2.0F, output));

    TextureAssetUVE empty = source;
    empty.width = 0U;
    empty.pixels.clear();
    EXPECT_FALSE(DeriveNormalTextureUVE(empty, 2.0F, output));
    EXPECT_TRUE(output.pixels.empty());
}

} // namespace
} // namespace UVE::Asset::Tests
