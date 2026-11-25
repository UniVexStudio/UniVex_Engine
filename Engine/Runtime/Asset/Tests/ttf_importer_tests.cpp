// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_importer.h"
#include "font_asset.h"
#include "ttf_importer.h"

namespace UVE::Asset::Tests {
namespace {

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

void RemoveFiles(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
}

TEST(TtfImporterTest, Import_ValidTtfPublishesUvfontAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_ttf_importer_tests_face.ttf";
    const std::filesystem::path destinationPath = "uve_ttf_importer_tests_face.uvfont";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, {std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                                       std::byte{0xAA}, std::byte{0xBB}});

    AssetImporter importer;
    RegisterTtfImporter(importer);
    AssetDatabase database;
    const AssetImportSourceClassification classification = importer.ClassifySource(sourcePath);
    EXPECT_EQ(classification.kind, AssetImportSourceKind::RawFont);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destinationPath);
    FontAsset font;
    ASSERT_TRUE(LoadFontAsset(destinationPath, font));
    EXPECT_EQ(font.familyName, "uve_ttf_importer_tests_face");
    ASSERT_EQ(font.fontData.size(), 6U);
    EXPECT_EQ(font.fontData[4U], std::byte{0xAA});
    RemoveFiles({sourcePath, destinationPath});
}

TEST(TtfImporterTest, Import_NonFontSourceFailsClosedWithoutRegistering) {
    const std::filesystem::path sourcePath = "uve_ttf_importer_tests_text.ttf";
    const std::filesystem::path destinationPath = "uve_ttf_importer_tests_text.uvfont";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, {std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'a'},
                                       std::byte{'f'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'}});

    AssetImporter importer;
    RegisterTtfImporter(importer);
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_TRUE(database.Resolve(guid).empty());
    RemoveFiles({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Asset::Tests
