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

[[nodiscard]] bool WaitForTerminalStateUVE(const AssetHandleUVE<DataTableUVE>& handle,
                                           const int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        if (handle.IsReadyUVE() || handle.HasFailedUVE()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

[[nodiscard]] DataTableUVE MakeTableUVE() {
    DataTableUVE table("weapons");
    static_cast<void>(table.DefineColumnUVE("damage", DataTableColumnTypeUVE::Integer));
    static_cast<void>(table.AddRowUVE("pistol", {std::int64_t{25}}));
    return table;
}

class TemporaryDataTableAssetUVE final {
public:
    TemporaryDataTableAssetUVE() : m_valid("uve_data_table_manager_valid.uvtable"),
                                   m_wrongKind("uve_data_table_manager_wrong_kind.uvtable") {
        static_cast<void>(std::filesystem::remove(m_valid));
        static_cast<void>(std::filesystem::remove(m_wrongKind));
    }
    TemporaryDataTableAssetUVE(const TemporaryDataTableAssetUVE&) = delete;
    TemporaryDataTableAssetUVE& operator=(const TemporaryDataTableAssetUVE&) = delete;
    ~TemporaryDataTableAssetUVE() {
        static_cast<void>(std::filesystem::remove(m_valid));
        static_cast<void>(std::filesystem::remove(m_wrongKind));
    }

    [[nodiscard]] const std::filesystem::path& ValidUVE() const noexcept { return m_valid; }
    [[nodiscard]] const std::filesystem::path& WrongKindUVE() const noexcept { return m_wrongKind; }

private:
    std::filesystem::path m_valid;
    std::filesystem::path m_wrongKind;
};

TEST(DataTableAssetManagerUVE, RegisteredLoaderLoadsTypedTableThroughAsyncHandle) {
    const TemporaryDataTableAssetUVE files;
    ASSERT_TRUE(SaveDataTableAssetUVE(MakeTableUVE(), files.ValidUVE()));

    Threading::ThreadPoolUVE threadPool(2);
    Events::EventSystemUVE eventSystem;
    AssetDatabaseUVE database;
    AssetManagerUVE manager(threadPool, eventSystem);
    RegisterDataTableAssetLoaderUVE(manager);
    const std::optional<AssetGuidUVE> guid = RegisterDataTableAssetUVE(database, files.ValidUVE());
    ASSERT_TRUE(guid.has_value());

    const AssetHandleUVE<DataTableUVE> handle = manager.LoadUVE<DataTableUVE>(*guid, database);
    ASSERT_TRUE(WaitForTerminalStateUVE(handle));
    ASSERT_TRUE(handle.IsReadyUVE());
    const DataTableUVE* const loaded = handle.TryGetUVE();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetSnapshotUVE(), MakeTableUVE().GetSnapshotUVE());
}

TEST(DataTableAssetManagerUVE, RegisteredLoaderFailsWrongEnvelopeKind) {
    const TemporaryDataTableAssetUVE files;
    ASSERT_TRUE(WriteUveFileUVE(files.WrongKindUVE(), AssetKindUVE::Blob,
                                std::vector<std::byte>{std::byte{'x'}}));

    Threading::ThreadPoolUVE threadPool(2);
    Events::EventSystemUVE eventSystem;
    AssetDatabaseUVE database;
    AssetManagerUVE manager(threadPool, eventSystem);
    RegisterDataTableAssetLoaderUVE(manager);
    const AssetGuidUVE guid = database.RegisterUVE(files.WrongKindUVE());

    const AssetHandleUVE<DataTableUVE> handle = manager.LoadUVE<DataTableUVE>(guid, database);
    ASSERT_TRUE(WaitForTerminalStateUVE(handle));
    EXPECT_TRUE(handle.HasFailedUVE());
    EXPECT_EQ(handle.TryGetUVE(), nullptr);
}

} // namespace
} // namespace UVE::Asset::Tests
