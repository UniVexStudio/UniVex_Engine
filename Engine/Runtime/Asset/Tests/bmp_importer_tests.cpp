// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_importer.h"
#include "texture_asset.h"

namespace UVE::Asset::Tests {
namespace {

void AppendU16LittleEndian(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendU32LittleEndian(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

[[nodiscard]] std::vector<std::byte> MakeBmp24OneByOne() {
    std::vector<std::byte> bmp;
    bmp.reserve(58U);
    bmp.push_back(std::byte{'B'});
    bmp.push_back(std::byte{'M'});
    AppendU32LittleEndian(bmp, 58U);
    AppendU16LittleEndian(bmp, 0U);
    AppendU16LittleEndian(bmp, 0U);
    AppendU32LittleEndian(bmp, 54U);
    AppendU32LittleEndian(bmp, 40U);
    AppendU32LittleEndian(bmp, 1U);
    AppendU32LittleEndian(bmp, 1U);
    AppendU16LittleEndian(bmp, 1U);
    AppendU16LittleEndian(bmp, 24U);
    AppendU32LittleEndian(bmp, 0U);
    AppendU32LittleEndian(bmp, 4U);
    AppendU32LittleEndian(bmp, 2835U);
    AppendU32LittleEndian(bmp, 2835U);
    AppendU32LittleEndian(bmp, 0U);
    AppendU32LittleEndian(bmp, 0U);
    bmp.insert(bmp.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0x00}});
    return bmp;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

TEST(BmpImporterTest, Import_DecodesBmpToTextureEnvelopeAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_bmp_importer_source.bmp";
    const std::filesystem::path destinationPath = "uve_bmp_importer_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakeBmp24OneByOne());

    AssetImporter importer;
    AssetDatabase assetDatabase;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, assetDatabase);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(assetDatabase.Resolve(guid), destinationPath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.width, 1U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.format, TextureAssetFormat::RGBA8Unorm);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(texture.usage, TextureUsage::Color);
    EXPECT_EQ(texture.pixels, (std::vector<std::byte>{std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}}));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(BmpImporterTest, Import_InvalidBmpPreservesExistingDestination) {
    const std::filesystem::path sourcePath = "uve_bmp_importer_invalid_source.bmp";
    const std::filesystem::path destinationPath = "uve_bmp_importer_existing_destination.uvtex";
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
    EXPECT_TRUE(assetDatabase.GetRegisteredAssets().empty());

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(BmpImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_bmp_importer_wrong_destination.bmp";
    const std::filesystem::path destinationPath = "uve_bmp_importer_wrong_destination.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakeBmp24OneByOne());

    AssetImporter importer;
    AssetDatabase assetDatabase;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, assetDatabase), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    EXPECT_TRUE(assetDatabase.GetRegisteredAssets().empty());

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
