#include "asset_importer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include "asset_database.h"
#include "texture_asset.h"
#include "texture_import_settings.h"
#include "texture_compression.h"

namespace UVE::Asset::Tests {
namespace {

void AppendU32BE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 24U;; shift -= 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
        if (shift == 0U) break;
    }
}

void AppendPngChunk(std::vector<std::byte>& png, const std::vector<std::byte>& type,
                       const std::vector<std::byte>& payload) {
    AppendU32BE(png, static_cast<std::uint32_t>(payload.size()));
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), payload.begin(), payload.end());
    std::vector<std::byte> crcInput(type);
    crcInput.insert(crcInput.end(), payload.begin(), payload.end());
    const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(crcInput.data()), static_cast<uInt>(crcInput.size()));
    AppendU32BE(png, static_cast<std::uint32_t>(crc));
}

[[nodiscard]] std::vector<std::byte> MakePngTwoByOneRgba8() {
    std::vector<std::byte> png{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47}, std::byte{0x0D}, std::byte{0x0A},
        std::byte{0x1A}, std::byte{0x0A}};
    AppendPngChunk(png, {std::byte{'I'}, std::byte{'H'}, std::byte{'D'}, std::byte{'R'}},
                      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0},
                       std::byte{0}, std::byte{1}, std::byte{8}, std::byte{6}, std::byte{0}, std::byte{0}, std::byte{0}});
    const std::vector<std::byte> raw{std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                     std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}};
    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> compressed(static_cast<std::size_t>(compressedSize));
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize,
                  reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(static_cast<std::size_t>(compressedSize));
    AppendPngChunk(png, {std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}}, compressed);
    AppendPngChunk(png, {std::byte{'I'}, std::byte{'E'}, std::byte{'N'}, std::byte{'D'}}, {});
    return png;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

TEST(PngImporterTest, Import_DecodesPngToTextureEnvelopeAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_png_importer_source.png";
    const std::filesystem::path destinationPath = "uve_png_importer_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakePngTwoByOneRgba8());

    AssetImporter importer;
    AssetDatabase assetDatabase;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(assetDatabase.Resolve(guid), destinationPath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.format, TextureAssetFormat::RGBA8Unorm);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(texture.usage, TextureUsage::Color);
    ASSERT_EQ(texture.mipLevels.size(), 1U);
    EXPECT_EQ(texture.mipLevels[0].width, 1U);
    EXPECT_EQ(texture.mipLevels[0].height, 1U);
    EXPECT_EQ(texture.pixels, (std::vector<std::byte>{std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF},
                                                         std::byte{0}, std::byte{0}, std::byte{0xFF}, std::byte{0xFF}}));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(PngImporterTest, Import_TextureSettingsOverrideSrgbColorDefault) {
    const std::filesystem::path sourcePath = "uve_png_importer_settings_source.png";
    const std::filesystem::path destinationPath = "uve_png_importer_settings_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakePngTwoByOneRgba8());

    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Linear;
    settings.usage = TextureUsage::Normal;
    settings.generateMipmaps = false;

    AssetImporter importer;
    AssetDatabase assetDatabase;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);
    ASSERT_NE(guid, kInvalidAssetGuid);

    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Linear);
    EXPECT_EQ(texture.usage, TextureUsage::Normal);
    EXPECT_TRUE(texture.mipLevels.empty());
    EXPECT_EQ(texture.pixels.size(), 8U);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

#if defined(UVE_HAS_BASIS_ENCODER) && UVE_HAS_BASIS_ENCODER
TEST(PngImporterTest, Import_CompressesDecodedMipChainToPortableBasisKtx2) {
    const std::filesystem::path sourcePath = "uve_png_importer_compressed_source.png";
    const std::filesystem::path destinationPath = "uve_png_importer_compressed_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakePngTwoByOneRgba8());

    TextureImportSettings settings;
    settings.compressionMode = TextureCompressionMode::BasisUASTC;
    settings.compressionQuality = 60U;
    settings.compressionEffort = 1U;
    AssetImporter importer;
    AssetDatabase assetDatabase;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase, settings);
    ASSERT_NE(guid, kInvalidAssetGuid);

    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.payloadEncoding, TexturePayloadEncoding::BasisUniversalKtx2);
    EXPECT_TRUE(texture.pixels.empty());
    EXPECT_TRUE(texture.mipLevels.empty());
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(texture.usage, TextureUsage::Color);
    TextureCompressionInfo info;
    ASSERT_TRUE(GetTextureCompressionInfo(texture, info));
    EXPECT_EQ(info.width, 2U);
    EXPECT_EQ(info.height, 1U);
    EXPECT_EQ(info.mipLevels, 2U);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}
#endif

TEST(PngImporterTest, Import_InvalidPngPreservesExistingDestination) {
    const std::filesystem::path sourcePath = "uve_png_importer_invalid_source.png";
    const std::filesystem::path destinationPath = "uve_png_importer_existing_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}});
    TextureAsset existing;
    existing.width = 1U;
    existing.height = 1U;
    existing.pixels = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
    ASSERT_TRUE(SaveTextureAsset(existing, destinationPath));

    AssetImporter importer;
    AssetDatabase assetDatabase;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, assetDatabase), kInvalidAssetGuid);
    TextureAsset retained;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, retained));
    EXPECT_EQ(retained.pixels, existing.pixels);
    EXPECT_TRUE(assetDatabase.Resolve(kInvalidAssetGuid).empty());

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
