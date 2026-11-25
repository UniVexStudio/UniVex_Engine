// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "data_table_asset_manager.h"
#include "data_table_asset_registration.h"
#include "data_table_asset.h"
#include "data_table.h"
#include "uve_file_envelope.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] bool WaitForTerminalState(const AssetHandle<DataTable>& handle,
                                           const int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        if (handle.IsReady() || handle.HasFailed()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

[[nodiscard]] DataTable MakeTable() {
    DataTable table("weapons");
    static_cast<void>(table.DefineColumn("damage", DataTableColumnType::Integer));
    static_cast<void>(table.AddRow("pistol", {std::int64_t{25}}));
    return table;
}

class TemporaryDataTableAsset final {
public:
    TemporaryDataTableAsset() : m_valid("uve_data_table_manager_valid.uvtable"),
                                   m_wrongKind("uve_data_table_manager_wrong_kind.uvtable") {
        static_cast<void>(std::filesystem::remove(m_valid));
        static_cast<void>(std::filesystem::remove(m_wrongKind));
    }
    TemporaryDataTableAsset(const TemporaryDataTableAsset&) = delete;
    TemporaryDataTableAsset& operator=(const TemporaryDataTableAsset&) = delete;
    ~TemporaryDataTableAsset() {
        static_cast<void>(std::filesystem::remove(m_valid));
        static_cast<void>(std::filesystem::remove(m_wrongKind));
    }

    [[nodiscard]] const std::filesystem::path& Valid() const noexcept { return m_valid; }
    [[nodiscard]] const std::filesystem::path& WrongKind() const noexcept { return m_wrongKind; }

private:
    std::filesystem::path m_valid;
    std::filesystem::path m_wrongKind;
};

TEST(DataTableAssetManager, RegisteredLoaderLoadsTypedTableThroughAsyncHandle) {
    const TemporaryDataTableAsset files;
    ASSERT_TRUE(SaveDataTableAsset(MakeTable(), files.Valid()));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase database;
    AssetManager manager(threadPool, eventSystem);
    RegisterDataTableAssetLoader(manager);
    const std::optional<AssetGuid> guid = RegisterDataTableAsset(database, files.Valid());
    ASSERT_TRUE(guid.has_value());

    const AssetHandle<DataTable> handle = manager.Load<DataTable>(*guid, database);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());
    const DataTable* const loaded = handle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetSnapshot(), MakeTable().GetSnapshot());
}

TEST(DataTableAssetManager, RegisteredLoaderFailsWrongEnvelopeKind) {
    const TemporaryDataTableAsset files;
    ASSERT_TRUE(WriteUveFile(files.WrongKind(), AssetKind::Blob,
                                std::vector<std::byte>{std::byte{'x'}}));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase database;
    AssetManager manager(threadPool, eventSystem);
    RegisterDataTableAssetLoader(manager);
    const AssetGuid guid = database.Register(files.WrongKind());

    const AssetHandle<DataTable> handle = manager.Load<DataTable>(guid, database);
    ASSERT_TRUE(WaitForTerminalState(handle));
    EXPECT_TRUE(handle.HasFailed());
    EXPECT_EQ(handle.TryGet(), nullptr);
}

} // namespace
} // namespace UVE::Asset::Tests
