// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_importer.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>
#include <jpeglib.h>

#include "asset_database.h"
#include "texture_asset.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> MakeOneByOneJpeg(const bool progressive = false) {
    jpeg_compress_struct compressor{};
    jpeg_error_mgr error{};
    jpeg_std_error(&error);
    compressor.err = &error;
    jpeg_create_compress(&compressor);
    unsigned char* encoded = nullptr;
    unsigned long encodedSize = 0UL;
    jpeg_mem_dest(&compressor, &encoded, &encodedSize);
    compressor.image_width = 1U;
    compressor.image_height = 1U;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    if (progressive) jpeg_simple_progression(&compressor);
    jpeg_set_quality(&compressor, 100, TRUE);
    jpeg_start_compress(&compressor, TRUE);
    JSAMPLE pixel[3] = {255U, 0U, 0U};
    JSAMPROW row = pixel;
    jpeg_write_scanlines(&compressor, &row, 1U);
    jpeg_finish_compress(&compressor);
    std::vector<std::byte> result(reinterpret_cast<std::byte*>(encoded),
                                   reinterpret_cast<std::byte*>(encoded) + encodedSize);
    std::free(encoded);
    jpeg_destroy_compress(&compressor);
    return result;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

void RemoveFiles(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) std::filesystem::remove(path);
}

TEST(JpegImporterTest, Import_ValidJpgPublishesUveTexAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_jpeg_importer_tests_red.jpg";
    const std::filesystem::path destinationPath = "uve_jpeg_importer_tests_red.uvtex";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, MakeOneByOneJpeg());

    AssetImporter importer;
    AssetDatabase database;
    const auto classification = importer.ClassifySource(sourcePath);
    EXPECT_EQ(classification.kind, AssetImportSourceKind::RawTexture);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    EXPECT_EQ(classification.diagnostic, "format-specific parser is registered");
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destinationPath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.width, 1U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.format, TextureAssetFormat::RGBA8Unorm);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    EXPECT_EQ(texture.usage, TextureUsage::Color);
    ASSERT_EQ(texture.pixels.size(), 4U);
    EXPECT_GT(std::to_integer<unsigned int>(texture.pixels[0]), 200U);
    EXPECT_LT(std::to_integer<unsigned int>(texture.pixels[1]), 50U);
    EXPECT_EQ(texture.pixels[3], std::byte{0xFF});
    RemoveFiles({sourcePath, destinationPath});
}

TEST(JpegImporterTest, Import_ValidProgressiveJpegPublishesThroughJpegExtension) {
    const std::filesystem::path sourcePath = "uve_jpeg_importer_tests_progressive.jpeg";
    const std::filesystem::path destinationPath = "uve_jpeg_importer_tests_progressive.uvtex";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, MakeOneByOneJpeg(true));

    AssetImporter importer;
    AssetDatabase database;
    const auto classification = importer.ClassifySource(sourcePath);
    EXPECT_TRUE(classification.importerRegistered);
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, texture));
    EXPECT_EQ(texture.width, 1U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.pixels.size(), 4U);
    RemoveFiles({sourcePath, destinationPath});
}

TEST(JpegImporterTest, Import_InvalidJpegPreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_jpeg_importer_tests_invalid.jpg";
    const std::filesystem::path destinationPath = "uve_jpeg_importer_tests_existing.uvtex";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, {std::byte{0xFF}, std::byte{0xD8}, std::byte{0x00}});
    TextureAsset original;
    original.width = 1U;
    original.height = 1U;
    original.format = TextureAssetFormat::RGBA8Unorm;
    original.pixels = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
    ASSERT_TRUE(SaveTextureAsset(original, destinationPath));

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_TRUE(database.Resolve(guid).empty());
    TextureAsset retained;
    ASSERT_TRUE(LoadTextureAsset(destinationPath, retained));
    EXPECT_EQ(retained.width, original.width);
    EXPECT_EQ(retained.height, original.height);
    EXPECT_EQ(retained.pixels, original.pixels);
    RemoveFiles({sourcePath, destinationPath});
}

TEST(JpegImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_jpeg_importer_tests_wrong_destination.jpg";
    const std::filesystem::path destinationPath = "uve_jpeg_importer_tests_wrong_destination.uvmodel";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, MakeOneByOneJpeg());

    AssetImporter importer;
    AssetDatabase database;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    RemoveFiles({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Asset::Tests
