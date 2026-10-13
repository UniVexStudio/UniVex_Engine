// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/font_asset_uve.h"
#include "uve/asset/ttf_importer_uve.h"

namespace UVE::Asset::Tests {
namespace {

void WriteBytesUVE(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

void RemoveFilesUVE(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) {
        std::filesystem::remove(path);
    }
}

TEST(TtfImporterUVETest, ImportUVE_ValidTtfPublishesUvfontAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_ttf_importer_tests_face.ttf";
    const std::filesystem::path destinationPath = "uve_ttf_importer_tests_face.uvfont";
    RemoveFilesUVE({sourcePath, destinationPath});
    WriteBytesUVE(sourcePath, {std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
                                       std::byte{0xAA}, std::byte{0xBB}});

    AssetImporterUVE importer;
    RegisterTtfImporterUVE(importer);
    AssetDatabaseUVE database;
    const AssetImportSourceClassificationUVE classification = importer.ClassifySourceUVE(sourcePath);
    EXPECT_EQ(classification.kind, AssetImportSourceKindUVE::RawFont);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destinationPath);
    FontAssetUVE font;
    ASSERT_TRUE(LoadFontAssetUVE(destinationPath, font));
    EXPECT_EQ(font.familyName, "uve_ttf_importer_tests_face");
    ASSERT_EQ(font.fontData.size(), 6U);
    EXPECT_EQ(font.fontData[4U], std::byte{0xAA});
    RemoveFilesUVE({sourcePath, destinationPath});
}

TEST(TtfImporterUVETest, ImportUVE_NonFontSourceFailsClosedWithoutRegistering) {
    const std::filesystem::path sourcePath = "uve_ttf_importer_tests_text.ttf";
    const std::filesystem::path destinationPath = "uve_ttf_importer_tests_text.uvfont";
    RemoveFilesUVE({sourcePath, destinationPath});
    WriteBytesUVE(sourcePath, {std::byte{'n'}, std::byte{'o'}, std::byte{'t'}, std::byte{'a'},
                                       std::byte{'f'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'}});

    AssetImporterUVE importer;
    RegisterTtfImporterUVE(importer);
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuidUVE);
    EXPECT_TRUE(database.ResolveUVE(guid).empty());
    RemoveFilesUVE({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Asset::Tests
