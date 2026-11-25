// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "asset_database.h"
#include "asset_importer.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "data_table_importer.h"
#include "data_table_pipeline.h"
#include "data_table.h"
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

TEST(DataTablePipeline, BootstrapComposesImportAndTypedLoadServices) {
    const std::filesystem::path source = ::UVE::Tests::ScratchRoot() / "uve_data_table_pipeline.csv";
    const std::filesystem::path destination =
        ::UVE::Tests::ScratchRoot() / "uve_data_table_pipeline.uvtable";
    static_cast<void>(std::filesystem::remove(source));
    static_cast<void>(std::filesystem::remove(destination));
    struct Cleanup final {
        std::filesystem::path source;
        std::filesystem::path destination;
        ~Cleanup() {
            static_cast<void>(std::filesystem::remove(source));
            static_cast<void>(std::filesystem::remove(destination));
        }
    } cleanup{source, destination};

    {
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << "id,damage\npistol,25\n";
    }

    AssetImporter importer;
    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetManager assetManager(threadPool, eventSystem);
    AssetDatabase assetDatabase;
    RegisterDataTablePipeline(importer, assetManager);

    DataTableImportSettings settings;
    settings.tableName = "weapons";
    settings.columns = {DataTableColumn{"damage", DataTableColumnType::Integer}};
    const AssetGuid guid = importer.Import(source, destination, assetDatabase, settings);
    ASSERT_NE(guid, kInvalidAssetGuid);

    const AssetHandle<DataTable> handle = assetManager.Load<DataTable>(guid, assetDatabase);
    ASSERT_TRUE(WaitForTerminalState(handle));
    ASSERT_TRUE(handle.IsReady());
    const DataTable* const table = handle.TryGet();
    ASSERT_NE(table, nullptr);
    const DataTableSnapshot snapshot = table->GetSnapshot();
    ASSERT_EQ(snapshot.name, "weapons");
    ASSERT_EQ(snapshot.rows.size(), 1U);
    ASSERT_EQ(snapshot.rows.front().values.size(), 1U);
    EXPECT_EQ(std::get<std::int64_t>(snapshot.rows.front().values.front()), 25);
}

} // namespace
} // namespace UVE::Asset::Tests
