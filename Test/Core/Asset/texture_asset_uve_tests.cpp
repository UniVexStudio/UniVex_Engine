// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_import_settings_uve.h"
#include "uve/asset/texture_mipmap_uve.h"
#include "uve/asset/texture_compression_uve.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
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

} // namespace
} // namespace UVE::Asset::Tests
