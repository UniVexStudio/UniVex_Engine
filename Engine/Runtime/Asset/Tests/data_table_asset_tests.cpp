// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "data_table_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::filesystem::path TestPath(const std::string& suffix) {
    return ::UVE::Tests::ScratchRoot() / ("uve_data_table_asset_" + suffix + ".uvtable");
}

[[nodiscard]] DataTable MakeTable(const std::string& name, const std::string& rowId,
                                        const std::int64_t value) {
    DataTable table(name);
    static_cast<void>(table.DefineColumn("damage", DataTableColumnType::Integer));
    static_cast<void>(table.DefineColumn("label", DataTableColumnType::String));
    static_cast<void>(table.AddRow(rowId, {value, std::string{"pistol"}}));
    return table;
}

class TemporaryAssetPath final {
public:
    explicit TemporaryAssetPath(std::filesystem::path path) : m_path(std::move(path)) {
        static_cast<void>(std::filesystem::remove(m_path));
    }
    TemporaryAssetPath(const TemporaryAssetPath&) = delete;
    TemporaryAssetPath& operator=(const TemporaryAssetPath&) = delete;
    ~TemporaryAssetPath() { static_cast<void>(std::filesystem::remove(m_path)); }

    [[nodiscard]] const std::filesystem::path& Get() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

TEST(DataTableAsset, SaveAndLoadRoundTripUsesUniversalDataTableAssetKind) {
    const TemporaryAssetPath path(TestPath("round_trip"));
    const DataTable source = MakeTable("weapons", "pistol", 25);
    ASSERT_TRUE(SaveDataTableAsset(source, path.Get()));

    const auto file = ReadUveFile(path.Get());
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->first.assetType, AssetKind::DataTable);

    DataTable loaded("replacement");
    ASSERT_TRUE(loaded.DefineColumn("old", DataTableColumnType::String));
    ASSERT_TRUE(LoadDataTableAsset(path.Get(), loaded));
    EXPECT_EQ(loaded.GetSnapshot(), source.GetSnapshot());
}

TEST(DataTableAsset, RejectsWrongKindAndMalformedPayloadWithoutChangingDestination) {
    const TemporaryAssetPath wrongKindPath(TestPath("wrong_kind"));
    const TemporaryAssetPath malformedPath(TestPath("malformed"));
    const DataTable original = MakeTable("original", "row", 1);
    DataTable destination = MakeTable("destination", "keep", 7);
    const DataTableSnapshot before = destination.GetSnapshot();

    const std::vector<std::byte> wrongKindPayload{std::byte{'x'}};
    ASSERT_TRUE(WriteUveFile(wrongKindPath.Get(), AssetKind::Blob, wrongKindPayload));
    EXPECT_FALSE(LoadDataTableAsset(wrongKindPath.Get(), destination));
    EXPECT_EQ(destination.GetSnapshot(), before);

    ASSERT_TRUE(WriteUveFile(malformedPath.Get(), AssetKind::DataTable,
                                std::vector<std::byte>{std::byte{'n'}, std::byte{'o'}, std::byte{'t'}}));
    EXPECT_FALSE(LoadDataTableAsset(malformedPath.Get(), destination));
    EXPECT_EQ(destination.GetSnapshot(), before);
    EXPECT_NE(destination.GetSnapshot(), original.GetSnapshot());
}

TEST(DataTableAsset, RejectsOversizedEnvelopeBeforeDocumentConstruction) {
    const TemporaryAssetPath path(TestPath("oversized_envelope"));
    std::vector<std::byte> oversizedPayload(DataTable::kMaximumDocumentBytes + 1U, std::byte{'x'});
    ASSERT_TRUE(WriteUveFile(path.Get(), AssetKind::DataTable, oversizedPayload));

    DataTable destination = MakeTable("destination", "keep", 7);
    const DataTableSnapshot before = destination.GetSnapshot();
    EXPECT_FALSE(LoadDataTableAsset(path.Get(), destination));
    EXPECT_EQ(destination.GetSnapshot(), before);
}

TEST(DataTableAsset, RejectsInvalidSourceAndMissingFile) {
    const TemporaryAssetPath invalidPath(TestPath("invalid_source"));
    DataTable invalid("contains space");
    ASSERT_TRUE(invalid.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(invalid.AddRow("row", {std::int64_t{1}}));
    EXPECT_FALSE(SaveDataTableAsset(invalid, invalidPath.Get()));
    EXPECT_FALSE(std::filesystem::exists(invalidPath.Get()));

    DataTable unchanged = MakeTable("unchanged", "row", 3);
    const DataTableSnapshot before = unchanged.GetSnapshot();
    EXPECT_FALSE(LoadDataTableAsset(TestPath("does_not_exist"), unchanged));
    EXPECT_EQ(unchanged.GetSnapshot(), before);
}

} // namespace
} // namespace UVE::Asset::Tests
