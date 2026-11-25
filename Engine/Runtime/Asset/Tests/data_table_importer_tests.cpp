// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "asset_database.h"
#include "asset_guid.h"
#include "asset_importer.h"
#include "data_table_asset.h"
#include "data_table_importer.h"

namespace UVE::Asset::Tests {
namespace {

void WriteText(const std::filesystem::path& path, const std::string& document) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << document;
}

class TemporaryImporterFiles final {
public:
    TemporaryImporterFiles() {
        m_paths = {
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer.csv",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer.tsv",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer.json",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer_csv.uvtable",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer_tsv.uvtable",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer_json.uvtable",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer_bad.uvtable",
            ::UVE::Tests::ScratchRoot() / "uve_data_table_importer_wrong.txt",
        };
        for (const std::filesystem::path& path : m_paths) {
            static_cast<void>(std::filesystem::remove(path));
        }
    }
    TemporaryImporterFiles(const TemporaryImporterFiles&) = delete;
    TemporaryImporterFiles& operator=(const TemporaryImporterFiles&) = delete;
    ~TemporaryImporterFiles() {
        for (const std::filesystem::path& path : m_paths) {
            static_cast<void>(std::filesystem::remove(path));
        }
    }

    [[nodiscard]] const std::filesystem::path& Csv() const noexcept { return m_paths[0]; }
    [[nodiscard]] const std::filesystem::path& Tsv() const noexcept { return m_paths[1]; }
    [[nodiscard]] const std::filesystem::path& Json() const noexcept { return m_paths[2]; }
    [[nodiscard]] const std::filesystem::path& CsvDestination() const noexcept { return m_paths[3]; }
    [[nodiscard]] const std::filesystem::path& TsvDestination() const noexcept { return m_paths[4]; }
    [[nodiscard]] const std::filesystem::path& JsonDestination() const noexcept { return m_paths[5]; }
    [[nodiscard]] const std::filesystem::path& BadDestination() const noexcept { return m_paths[6]; }
    [[nodiscard]] const std::filesystem::path& WrongDestination() const noexcept { return m_paths[7]; }

private:
    std::vector<std::filesystem::path> m_paths;
};

[[nodiscard]] DataTableImportSettings MakeSettings() {
    DataTableImportSettings settings;
    settings.tableName = "weapons";
    settings.columns = {
        DataTableColumn{"damage", DataTableColumnType::Integer},
        DataTableColumn{"enabled", DataTableColumnType::Boolean},
        DataTableColumn{"label", DataTableColumnType::String},
    };
    return settings;
}

TEST(DataTableImporter, RegistersCsvTsvAndJsonImportersWithTypedDestinations) {
    const TemporaryImporterFiles files;
    WriteText(files.Csv(), "id,damage,enabled,label\npistol,25,true,Pistol\n");
    WriteText(files.Tsv(), "id\tdamage\tenabled\tlabel\npistol\t25\ttrue\tPistol\n");
    WriteText(files.Json(), R"([{"label":"Pistol","enabled":true,"damage":25,"id":"pistol"}])");

    AssetImporter importer;
    RegisterDataTableImporters(importer);
    AssetDatabase database;
    const DataTableImportSettings settings = MakeSettings();

    const AssetGuid csvGuid = importer.Import(files.Csv(), files.CsvDestination(), database, settings);
    const AssetGuid tsvGuid = importer.Import(files.Tsv(), files.TsvDestination(), database, settings);
    const AssetGuid jsonGuid = importer.Import(files.Json(), files.JsonDestination(), database, settings);
    ASSERT_NE(csvGuid, kInvalidAssetGuid);
    ASSERT_NE(tsvGuid, kInvalidAssetGuid);
    ASSERT_NE(jsonGuid, kInvalidAssetGuid);

    DataTable csv;
    DataTable tsv;
    DataTable json;
    ASSERT_TRUE(LoadDataTableAsset(files.CsvDestination(), csv));
    ASSERT_TRUE(LoadDataTableAsset(files.TsvDestination(), tsv));
    ASSERT_TRUE(LoadDataTableAsset(files.JsonDestination(), json));
    EXPECT_EQ(csv.GetSnapshot().rows.front().values, tsv.GetSnapshot().rows.front().values);
    EXPECT_EQ(tsv.GetSnapshot().rows.front().values, json.GetSnapshot().rows.front().values);
    EXPECT_EQ(database.Resolve(csvGuid), files.CsvDestination().lexically_normal());
    EXPECT_EQ(database.GetRegisteredAssets().size(), 3U);
}

TEST(DataTableImporter, RejectsOverCapSourceBeforeTypedParseAndPublish) {
    const TemporaryImporterFiles files;
    const std::string oversized(DataTable::kMaximumDocumentBytes + 1U, 'x');
    WriteText(files.Csv(), oversized);

    AssetImporter importer;
    RegisterDataTableImporters(importer);
    AssetDatabase database;

    EXPECT_EQ(importer.Import(files.Csv(), files.CsvDestination(), database, MakeSettings()),
              kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(files.CsvDestination()));
    EXPECT_TRUE(database.GetRegisteredAssets().empty());
}

TEST(DataTableImporter, RejectsMalformedSourceMissingSchemaAndWrongDestinationWithoutRegistration) {
    const TemporaryImporterFiles files;
    WriteText(files.Csv(), "id,damage,enabled,label\npistol,not-an-integer,true,Pistol\n");
    AssetImporter importer;
    RegisterDataTableImporters(importer);
    AssetDatabase database;
    const AssetImportSettings genericSettings;
    const DataTableImportSettings settings = MakeSettings();

    EXPECT_EQ(importer.Import(files.Csv(), files.BadDestination(), database, settings), kInvalidAssetGuid);
    EXPECT_EQ(importer.Import(files.Csv(), files.CsvDestination(), database, genericSettings),
              kInvalidAssetGuid);
    EXPECT_EQ(importer.Import(files.Csv(), files.WrongDestination(), database, settings),
              kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(files.BadDestination()));
    EXPECT_FALSE(std::filesystem::exists(files.CsvDestination()));
    EXPECT_TRUE(database.GetRegisteredAssets().empty());
}

} // namespace
} // namespace UVE::Asset::Tests


namespace UVE::Asset::Tests {

TEST(DataTableImportSettings, CacheVersionIsDeterministicAndSchemaSensitive) {
    DataTableImportSettings first;
    first.tableName = "items";
    first.columns = {
        DataTableColumn{"label", DataTableColumnType::String},
        DataTableColumn{"count", DataTableColumnType::Integer},
    };
    const DataTableImportSettings same = first;
    DataTableImportSettings reordered = first;
    std::swap(reordered.columns[0], reordered.columns[1]);
    DataTableImportSettings renamed = first;
    renamed.tableName = "other_items";
    DataTableImportSettings retyped = first;
    retyped.columns[1].type = DataTableColumnType::Number;

    EXPECT_EQ(first.GetCacheVersion(), same.GetCacheVersion());
    EXPECT_NE(first.GetCacheVersion(), reordered.GetCacheVersion());
    EXPECT_NE(first.GetCacheVersion(), renamed.GetCacheVersion());
    EXPECT_NE(first.GetCacheVersion(), retyped.GetCacheVersion());
    EXPECT_EQ(first.GetCacheVersion().size(), std::string{"data-table-import-v2-"}.size() + 16U);
}

} // namespace UVE::Asset::Tests
