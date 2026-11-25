#include "data_table_registry.h"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

DataTable MakeTable(const std::string& name, const std::int64_t value) {
    DataTable table(name);
    EXPECT_TRUE(table.DefineColumn("value", DataTableColumnType::Integer));
    EXPECT_TRUE(table.AddRow("row", {value}));
    return table;
}

TEST(DataTableRegistry, RegistersOwnedTablesAndRejectsDuplicatesAtomically) {
    DataTableRegistry registry;
    EXPECT_EQ(registry.Size(), 0U);
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 1U);

    ASSERT_TRUE(registry.Register(MakeTable("alpha", 1)));
    EXPECT_EQ(registry.Size(), 1U);
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 2U);
    EXPECT_FALSE(registry.Register(MakeTable("alpha", 2)));
    EXPECT_EQ(registry.Size(), 1U);
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 2U);
}

TEST(DataTableRegistry, RejectsTablesWithDiagnosticsWithoutChangingRegistry) {
    DataTableRegistry registry;
    DataTable invalid("invalid");
    ASSERT_TRUE(invalid.DefineColumn("value", DataTableColumnType::Integer));
    EXPECT_FALSE(invalid.ImportCsv("id,value\nrow,not-an-integer\n"));
    ASSERT_FALSE(invalid.GetSnapshot().diagnostics.empty());

    EXPECT_FALSE(registry.Register(std::move(invalid)));
    EXPECT_EQ(registry.Size(), 0U);
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 1U);
}

TEST(DataTableRegistry, ReturnsCopiedSnapshotsAndSortedCatalogFacts) {
    DataTableRegistry registry;
    ASSERT_TRUE(registry.Register(MakeTable("zeta", 2)));
    ASSERT_TRUE(registry.Register(MakeTable("alpha", 1)));

    DataTableSnapshot copied;
    ASSERT_TRUE(registry.TryGetSnapshot("alpha", copied));
    copied.rows.clear();
    EXPECT_TRUE(copied.rows.empty());

    DataTableSnapshot stillOwned;
    ASSERT_TRUE(registry.TryGetSnapshot("alpha", stillOwned));
    ASSERT_EQ(stillOwned.rows.size(), 1U);
    EXPECT_EQ(stillOwned.rows.front().values.front(), DataTableValue{std::int64_t{1}});

    const DataTableCatalogSnapshot catalog = registry.GetCatalogSnapshot();
    ASSERT_EQ(catalog.entries.size(), 2U);
    EXPECT_EQ(catalog.entries[0].name, "alpha");
    EXPECT_EQ(catalog.entries[1].name, "zeta");
    EXPECT_TRUE(catalog.entries[0].valid);
    EXPECT_EQ(catalog.generation, 3U);
}

TEST(DataTableRegistry, RemoveAndClearAdvanceGenerationOnlyOnMutation) {
    DataTableRegistry registry;
    ASSERT_TRUE(registry.Register(MakeTable("alpha", 1)));
    ASSERT_TRUE(registry.Remove("alpha"));
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 3U);
    EXPECT_FALSE(registry.Remove("alpha"));
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 3U);

    ASSERT_TRUE(registry.Register(MakeTable("beta", 2)));
    ASSERT_TRUE(registry.Clear());
    EXPECT_EQ(registry.Size(), 0U);
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 5U);
    EXPECT_FALSE(registry.Clear());
    EXPECT_EQ(registry.GetCatalogSnapshot().generation, 5U);
}

TEST(DataTableRegistry, EnforcesMaximumTableBound) {
    DataTableRegistry registry;
    for (std::size_t index = 0U; index < DataTableRegistry::kMaximumTables; ++index) {
        ASSERT_TRUE(registry.Register(MakeTable("table_" + std::to_string(index), static_cast<std::int64_t>(index))));
    }
    EXPECT_EQ(registry.Size(), DataTableRegistry::kMaximumTables);
    EXPECT_FALSE(registry.Register(MakeTable("overflow", 0)));
    EXPECT_EQ(registry.Size(), DataTableRegistry::kMaximumTables);
}

} // namespace
} // namespace UVE::Asset::Tests
