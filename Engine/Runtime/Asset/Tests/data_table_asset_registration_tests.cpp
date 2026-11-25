// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "asset_database.h"
#include "data_table_asset_registration.h"
#include "data_table_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] DataTable MakeTable() {
    DataTable table("weapons");
    static_cast<void>(table.DefineColumn("damage", DataTableColumnType::Integer));
    static_cast<void>(table.AddRow("pistol", {std::int64_t{25}}));
    return table;
}

class TemporaryRegistrationFiles final {
public:
    TemporaryRegistrationFiles() {
        for (const std::string name : {"valid.uvtable", "wrong_kind.uvtable", "wrong_extension.txt"}) {
            m_paths.emplace_back(::UVE::Tests::ScratchRoot() / ("uve_data_table_registration_" + name));
            static_cast<void>(std::filesystem::remove(m_paths.back()));
        }
    }
    TemporaryRegistrationFiles(const TemporaryRegistrationFiles&) = delete;
    TemporaryRegistrationFiles& operator=(const TemporaryRegistrationFiles&) = delete;
    ~TemporaryRegistrationFiles() {
        for (const std::filesystem::path& path : m_paths) {
            static_cast<void>(std::filesystem::remove(path));
        }
    }

    [[nodiscard]] const std::filesystem::path& Valid() const noexcept { return m_paths[0]; }
    [[nodiscard]] const std::filesystem::path& WrongKind() const noexcept { return m_paths[1]; }
    [[nodiscard]] const std::filesystem::path& WrongExtension() const noexcept { return m_paths[2]; }

private:
    std::vector<std::filesystem::path> m_paths;
};

TEST(DataTableAssetRegistration, ValidatesAndRegistersIdempotentlyWithLexicalResolution) {
    const TemporaryRegistrationFiles files;
    ASSERT_TRUE(SaveDataTableAsset(MakeTable(), files.Valid()));
    AssetDatabase database;

    const std::optional<AssetGuid> first = RegisterDataTableAsset(database, files.Valid());
    ASSERT_TRUE(first.has_value());
    EXPECT_NE(*first, kInvalidAssetGuid);
    EXPECT_TRUE(database.HasGuid(*first));
    EXPECT_EQ(database.Resolve(*first), files.Valid().lexically_normal());
    ASSERT_EQ(database.GetRegisteredAssets().size(), 1U);

    const std::optional<AssetGuid> second = RegisterDataTableAsset(database, files.Valid());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, *first);
    EXPECT_EQ(database.GetRegisteredAssets().size(), 1U);
}

TEST(DataTableAssetRegistration, RejectsInvalidFilesBeforeChangingDatabase) {
    const TemporaryRegistrationFiles files;
    ASSERT_TRUE(WriteUveFile(files.WrongKind(), AssetKind::Blob,
                                std::vector<std::byte>{std::byte{'x'}}));
    ASSERT_TRUE(SaveDataTableAsset(MakeTable(), files.WrongExtension()));
    AssetDatabase database;

    EXPECT_FALSE(RegisterDataTableAsset(database, files.WrongKind()).has_value());
    EXPECT_FALSE(RegisterDataTableAsset(database, files.WrongExtension()).has_value());
    EXPECT_FALSE(RegisterDataTableAsset(database,
                                           ::UVE::Tests::ScratchRoot() /
                                               "uve_data_table_registration_missing.uvtable").has_value());
    EXPECT_TRUE(database.GetRegisteredAssets().empty());
}

} // namespace
} // namespace UVE::Asset::Tests
