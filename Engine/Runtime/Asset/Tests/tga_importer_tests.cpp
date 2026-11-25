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

[[nodiscard]] std::vector<std::byte> MakeTga24OneByOne() {
    std::vector<std::byte> tga;
    tga.reserve(21U);
    tga.insert(tga.end(), 12U, std::byte{0});
    tga[2] = std::byte{2};
    AppendU16LittleEndian(tga, 1U);
    AppendU16LittleEndian(tga, 1U);
    tga.push_back(std::byte{24});
    tga.push_back(std::byte{0});
    tga.insert(tga.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}});
    return tga;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

TEST(TgaImporterTest, Import_DecodesTgaToTextureEnvelopeAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_tga_importer_source.tga";
    const std::filesystem::path destinationPath = "uve_tga_importer_destination.uvtex";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakeTga24OneByOne());

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

TEST(TgaImporterTest, Import_InvalidTgaPreservesExistingDestination) {
    const std::filesystem::path sourcePath = "uve_tga_importer_invalid_source.tga";
    const std::filesystem::path destinationPath = "uve_tga_importer_existing_destination.uvtex";
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

TEST(TgaImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_tga_importer_wrong_destination.tga";
    const std::filesystem::path destinationPath = "uve_tga_importer_wrong_destination.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteBytes(sourcePath, MakeTga24OneByOne());

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
