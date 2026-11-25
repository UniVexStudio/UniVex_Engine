// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {

TEST(DataTable, SchemaAndRowsExposeDeterministicTypedSnapshot) {
    DataTable table("weapons");
    ASSERT_TRUE(table.DefineColumn("damage", DataTableColumnType::Integer));
    ASSERT_TRUE(table.DefineColumn("rate", DataTableColumnType::Number));
    ASSERT_TRUE(table.DefineColumn("enabled", DataTableColumnType::Boolean));
    ASSERT_TRUE(table.DefineColumn("display", DataTableColumnType::String));
    ASSERT_TRUE(table.AddRow("pistol", {std::int64_t{25}, 0.3, true, std::string{"Pistol"}}));

    const DataTableSnapshot snapshot = table.GetSnapshot();
    EXPECT_EQ(snapshot.name, "weapons");
    EXPECT_EQ(snapshot.generation, 6U);
    ASSERT_EQ(snapshot.columns.size(), 4U);
    ASSERT_EQ(snapshot.rows.size(), 1U);
    ASSERT_EQ(snapshot.rows.front().values.size(), 4U);
    EXPECT_EQ(std::get<std::int64_t>(snapshot.rows.front().values[0]), 25);
    EXPECT_DOUBLE_EQ(std::get<double>(snapshot.rows.front().values[1]), 0.3);
    EXPECT_TRUE(std::get<bool>(snapshot.rows.front().values[2]));
    EXPECT_EQ(std::get<std::string>(snapshot.rows.front().values[3]), "Pistol");
    ASSERT_NE(table.FindRow("pistol"), nullptr);
    EXPECT_EQ(table.FindRow("missing"), nullptr);
}

TEST(DataTable, InvalidMutationsAreFailureAtomic) {
    DataTable table("stable");
    ASSERT_TRUE(table.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(table.AddRow("one", {std::int64_t{1}}));
    const DataTableSnapshot before = table.GetSnapshot();

    EXPECT_FALSE(table.DefineColumn("value", DataTableColumnType::String));
    EXPECT_FALSE(table.DefineColumn("invalid", static_cast<DataTableColumnType>(0xFFU)));
    EXPECT_FALSE(table.AddRow("one", {std::int64_t{2}}));
    EXPECT_FALSE(table.AddRow("two", {std::string{"wrong type"}}));
    EXPECT_FALSE(table.AddRow("bad id!", {std::int64_t{3}}));

    const DataTableSnapshot after = table.GetSnapshot();
    EXPECT_EQ(after, before);
}

TEST(DataTable, CsvImportSupportsQuotedFieldsAndCrLf) {
    DataTable table("dialogue");
    ASSERT_TRUE(table.DefineColumn("speaker", DataTableColumnType::String));
    ASSERT_TRUE(table.DefineColumn("line", DataTableColumnType::String));
    ASSERT_TRUE(table.DefineColumn("next", DataTableColumnType::Integer));
    ASSERT_TRUE(table.DefineColumn("enabled", DataTableColumnType::Boolean));

    const std::string csv = "id,speaker,line,next,enabled\r\n"
                            "intro,NPC,\"Hello, \"\"traveler\"\"?\",2,true\r\n";
    ASSERT_TRUE(table.ImportCsv(csv));
    const DataTableSnapshot snapshot = table.GetSnapshot();
    ASSERT_EQ(snapshot.rows.size(), 1U);
    EXPECT_EQ(std::get<std::string>(snapshot.rows[0].values[1]), "Hello, \"traveler\"?");
    EXPECT_EQ(std::get<std::int64_t>(snapshot.rows[0].values[2]), 2);
    EXPECT_TRUE(std::get<bool>(snapshot.rows[0].values[3]));
    EXPECT_TRUE(snapshot.diagnostics.empty());
}

TEST(DataTable, CsvImportRejectsTypedErrorsWithoutReplacingRows) {
    DataTable table("numbers");
    ASSERT_TRUE(table.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(table.ImportCsv("id,value\nvalid,7\n"));
    const DataTableSnapshot before = table.GetSnapshot();

    EXPECT_FALSE(table.ImportCsv("id,value\ninvalid,not-a-number\n"));
    const DataTableSnapshot after = table.GetSnapshot();
    EXPECT_EQ(after.rows, before.rows);
    ASSERT_EQ(after.diagnostics.size(), 1U);
    EXPECT_EQ(after.diagnostics.front().code, DataTableDiagnosticCode::InvalidValue);
    EXPECT_EQ(after.diagnostics.front().line, 2U);
    EXPECT_EQ(after.diagnostics.front().column, 9U);
}

TEST(DataTable, CsvImportRejectsHeaderAndDuplicateRowsDeterministically) {
    DataTable table("items");
    ASSERT_TRUE(table.DefineColumn("label", DataTableColumnType::String));

    const std::uint64_t beforeHeaderMismatch = table.GetSnapshot().generation;
    EXPECT_FALSE(table.ImportCsv("id,wrong\na,one\n"));
    const DataTableSnapshot afterHeaderMismatch = table.GetSnapshot();
    EXPECT_GT(afterHeaderMismatch.generation, beforeHeaderMismatch);
    ASSERT_EQ(afterHeaderMismatch.diagnostics.size(), 1U);
    EXPECT_EQ(afterHeaderMismatch.diagnostics.front().code, DataTableDiagnosticCode::HeaderMismatch);

    EXPECT_FALSE(table.ImportCsv("id,label\na,one\na,two\n"));
    const DataTableSnapshot snapshot = table.GetSnapshot();
    ASSERT_EQ(snapshot.diagnostics.size(), 1U);
    EXPECT_EQ(snapshot.diagnostics.front().code, DataTableDiagnosticCode::DuplicateRow);
    EXPECT_TRUE(snapshot.rows.empty());
}

TEST(DataTable, TsvImportSupportsQuotedFieldsAndTabs) {
    DataTable table("dialogue");
    ASSERT_TRUE(table.DefineColumn("speaker", DataTableColumnType::String));
    ASSERT_TRUE(table.DefineColumn("line", DataTableColumnType::String));
    ASSERT_TRUE(table.DefineColumn("next", DataTableColumnType::Integer));

    ASSERT_TRUE(table.ImportTsv("id\tspeaker\tline\tnext\r\n"
                                   "intro\tNPC\t\"Hello\ttraveler\"\t2\r\n"));
    const DataTableSnapshot snapshot = table.GetSnapshot();
    ASSERT_EQ(snapshot.rows.size(), 1U);
    EXPECT_EQ(std::get<std::string>(snapshot.rows[0].values[1]), "Hello\ttraveler");
    EXPECT_EQ(std::get<std::int64_t>(snapshot.rows[0].values[2]), 2);
}

TEST(DataTable, JsonImportUsesSchemaTypesAndIgnoresObjectMemberOrder) {
    DataTable table("items");
    ASSERT_TRUE(table.DefineColumn("label", DataTableColumnType::String));
    ASSERT_TRUE(table.DefineColumn("count", DataTableColumnType::Integer));
    ASSERT_TRUE(table.DefineColumn("enabled", DataTableColumnType::Boolean));
    ASSERT_TRUE(table.ImportJson(R"([{"enabled":true,"id":"item_a","count":7,"label":"Potion"}])"));

    const DataTableSnapshot snapshot = table.GetSnapshot();
    ASSERT_EQ(snapshot.rows.size(), 1U);
    EXPECT_EQ(std::get<std::string>(snapshot.rows[0].values[0]), "Potion");
    EXPECT_EQ(std::get<std::int64_t>(snapshot.rows[0].values[1]), 7);
    EXPECT_TRUE(std::get<bool>(snapshot.rows[0].values[2]));
}

TEST(DataTable, JsonImportRejectsWrongTypesAndDuplicateRowsAtomically) {
    DataTable table("items");
    ASSERT_TRUE(table.DefineColumn("count", DataTableColumnType::Integer));
    ASSERT_TRUE(table.ImportJson(R"([{"id":"stable","count":3}])"));
    const DataTableSnapshot before = table.GetSnapshot();

    EXPECT_FALSE(table.ImportJson(R"([{"id":"new","count":"wrong"}])"));
    EXPECT_EQ(table.GetSnapshot().rows, before.rows);
    ASSERT_EQ(table.GetSnapshot().diagnostics.size(), 1U);
    EXPECT_EQ(table.GetSnapshot().diagnostics.front().code, DataTableDiagnosticCode::InvalidValue);

    EXPECT_FALSE(table.ImportJson(R"([{"id":"duplicate","count":1},{"id":"duplicate","count":2}])"));
    EXPECT_TRUE(table.GetSnapshot().rows == before.rows);
    ASSERT_EQ(table.GetSnapshot().diagnostics.size(), 1U);
    EXPECT_EQ(table.GetSnapshot().diagnostics.front().code, DataTableDiagnosticCode::DuplicateRow);
}

TEST(DataTable, BoundsAndDiagnosticGenerationAreExplicit) {
    DataTable table("bounded");
    for (std::size_t index = 0U; index < DataTable::kMaximumColumns; ++index) {
        ASSERT_TRUE(table.DefineColumn("column_" + std::to_string(index), DataTableColumnType::String));
    }
    EXPECT_FALSE(table.DefineColumn("overflow", DataTableColumnType::String));
    const std::uint64_t before = table.GetSnapshot().generation;
    EXPECT_FALSE(table.ImportCsv("id,column_0\nrow,\"unterminated\n"));
    const DataTableSnapshot after = table.GetSnapshot();
    EXPECT_GT(after.generation, before);
    EXPECT_FALSE(after.diagnostics.empty());
    EXPECT_TRUE(after.rows.empty());
}

TEST(DataTable, CsvImportRejectsInvalidBooleanAndNonFiniteNumber) {
    DataTable table("flags");
    ASSERT_TRUE(table.DefineColumn("enabled", DataTableColumnType::Boolean));
    ASSERT_TRUE(table.DefineColumn("weight", DataTableColumnType::Number));

    EXPECT_FALSE(table.ImportCsv("id,enabled,weight\na,TRUE,1\n"));
    ASSERT_EQ(table.GetSnapshot().diagnostics.size(), 1U);
    EXPECT_EQ(table.GetSnapshot().diagnostics.front().code, DataTableDiagnosticCode::InvalidValue);
    EXPECT_FALSE(table.ImportCsv("id,enabled,weight\nb,true,nan\n"));
    ASSERT_EQ(table.GetSnapshot().diagnostics.size(), 1U);
    EXPECT_EQ(table.GetSnapshot().diagnostics.front().code, DataTableDiagnosticCode::InvalidValue);
}

TEST(DataTable, AssetEnvelopeRoundTripIsDeterministicAndTyped) {
    DataTable source("weapons");
    ASSERT_TRUE(source.DefineColumn("damage", DataTableColumnType::Integer));
    ASSERT_TRUE(source.DefineColumn("weight", DataTableColumnType::Number));
    ASSERT_TRUE(source.DefineColumn("name", DataTableColumnType::String));
    ASSERT_TRUE(source.AddRow("pistol", {std::int64_t{25}, 1.5, std::string{"Pistol"}}));

    std::string first;
    std::string second;
    ASSERT_TRUE(DataTableAssetSerializer::Serialize(source, first));
    ASSERT_TRUE(DataTableAssetSerializer::Serialize(source, second));
    EXPECT_EQ(first, second);

    DataTable restored("placeholder");
    ASSERT_TRUE(DataTableAssetSerializer::Deserialize(first, restored));
    EXPECT_EQ(restored.GetSnapshot().columns, source.GetSnapshot().columns);
    EXPECT_EQ(restored.GetSnapshot().rows, source.GetSnapshot().rows);
}

TEST(DataTable, AssetEnvelopeRejectsInvalidInputWithoutReplacingDestination) {
    DataTable table("stable");
    ASSERT_TRUE(table.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(table.AddRow("one", {std::int64_t{1}}));
    const DataTableSnapshot before = table.GetSnapshot();

    EXPECT_FALSE(DataTableAssetSerializer::Deserialize(
        R"({"format":"uve.data_table","version":2,"name":"new","columns":[],"rows":[]})", table));
    EXPECT_EQ(table.GetSnapshot().columns, before.columns);
    EXPECT_EQ(table.GetSnapshot().rows, before.rows);
}

TEST(DataTable, DeserializeRejectsFiveKeyEnvelopeWithWrongKeyNamesInsteadOfThrowing) {
    DataTable table("stable");
    ASSERT_TRUE(table.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(table.AddRow("one", {std::int64_t{1}}));
    const DataTableSnapshot before = table.GetSnapshot();

    // Exactly 5 top-level keys (matching envelope.size() != 5U), but "name"/"columns"/"rows" are
    // absent - before the fix, envelope.at("name") throws nlohmann::json::out_of_range uncaught.
    EXPECT_FALSE(DataTableAssetSerializer::Deserialize(
        R"({"format":"uve.data_table","version":1,"title":"x","cols":[],"data":[]})", table));
    EXPECT_EQ(table.GetSnapshot().columns, before.columns);
    EXPECT_EQ(table.GetSnapshot().rows, before.rows);
}

TEST(DataTable, DeserializeNeverThrowsOnMalformedOrTypeMismatchedDocuments) {
    DataTable table("stable");

    const std::vector<std::string> malformedDocuments{
        "",
        "not json at all",
        "{",
        "[]",
        "null",
        R"({"format":"uve.data_table","version":1,"name":123,"columns":[],"rows":[]})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":"nope","rows":[]})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":[],"rows":"nope"})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":[1,2,3],"rows":[]})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":[{"name":"a"}],"rows":[]})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":[{"name":"a","type":"integer"}],"rows":[{"id":1,"values":[]}]})",
        R"({"format":"uve.data_table","version":1,"name":"n","columns":[{"name":"a","type":"integer"}],"rows":[{"id":"r","values":[{"type":42,"value":1}]}]})",
    };
    for (const std::string& document : malformedDocuments) {
        EXPECT_FALSE(DataTableAssetSerializer::Deserialize(document, table)) << "document: " << document;
    }
}

TEST(DataTable, CatalogSnapshotIsSortedCopiedAndGenerationCounted) {
    DataTable zebra("zebra");
    ASSERT_TRUE(zebra.DefineColumn("value", DataTableColumnType::Integer));
    ASSERT_TRUE(zebra.AddRow("row", {std::int64_t{1}}));
    DataTable alpha("alpha");
    ASSERT_TRUE(alpha.DefineColumn("value", DataTableColumnType::Integer));

    DataTableCatalog catalog;
    ASSERT_TRUE(catalog.Upsert(zebra.GetSnapshot()));
    ASSERT_TRUE(catalog.Upsert(alpha.GetSnapshot()));
    const DataTableCatalogSnapshot snapshot = catalog.GetSnapshot();
    ASSERT_EQ(snapshot.entries.size(), 2U);
    EXPECT_EQ(snapshot.entries[0].name, "alpha");
    EXPECT_EQ(snapshot.entries[1].name, "zebra");
    EXPECT_TRUE(snapshot.entries[1].valid);
    EXPECT_EQ(snapshot.entries[0].rowCount, 0U);
    EXPECT_TRUE(catalog.Contains("alpha"));

    EXPECT_FALSE(catalog.Upsert(alpha.GetSnapshot()));
    ASSERT_TRUE(catalog.Remove("alpha"));
    EXPECT_FALSE(catalog.Contains("alpha"));
    EXPECT_FALSE(catalog.Remove("alpha"));
}

} // namespace UVE::Asset::Tests
