// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "engine_core.h"
#include "editor_bridge.h"

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeConsoleBridgeConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_developer_console_bridge_tests.log";
    config.settingsFilePath = "uve_developer_console_bridge_tests_settings.json";
    config.assetDatabaseFilePath = "uve_developer_console_bridge_tests_assets.json";
    config.saveDirectoryPath = "uve_developer_console_bridge_tests_saves";
    config.shaderCachePath = "uve_developer_console_bridge_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

TEST(EditorDeveloperConsoleBridge, RoutesBoundedCommandsAndCopiesGeneration) {
    Core::EngineCore engine(MakeConsoleBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_developer_console_bridge.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeSnapshot initial = bridge.GetSnapshot();
        EXPECT_FALSE(initial.developerConsole.console.outputTruncated);
        EXPECT_TRUE(initial.developerConsole.console.generation > 0U);

        EditorBridgeRequest help{};
        help.protocolVersion = kEditorBridgeProtocolVersion;
        help.requestId = 1U;
        help.expectedRevision = initial.revision;
        help.kind = EditorBridgeRequestKind::SubmitDeveloperConsoleCommand;
        help.developerConsoleCommand = "help";
        const EditorBridgeResponse helpResponse = bridge.Dispatch(help);
        ASSERT_TRUE(helpResponse.applied);
        ASSERT_FALSE(helpResponse.snapshot.developerConsole.console.output.empty());
        EXPECT_GT(helpResponse.snapshot.developerConsole.console.generation,
                  initial.developerConsole.console.generation);

        EditorBridgeRequest unknown = help;
        unknown.requestId = 2U;
        unknown.expectedRevision = helpResponse.snapshot.revision;
        unknown.developerConsoleCommand = "notRegistered";
        const EditorBridgeResponse unknownResponse = bridge.Dispatch(unknown);
        EXPECT_FALSE(unknownResponse.applied);
        EXPECT_EQ(unknownResponse.code, "bridge.developer_console.command.rejected");
        ASSERT_FALSE(unknownResponse.snapshot.developerConsole.console.output.empty());
        EXPECT_EQ(unknownResponse.snapshot.developerConsole.console.output.back().severity,
                  DeveloperConsoleSeverity::Error);

        EditorBridgeRequest stale = help;
        stale.requestId = 3U;
        stale.expectedRevision = initial.revision;
        const EditorBridgeResponse staleResponse = bridge.Dispatch(stale);
        EXPECT_FALSE(staleResponse.applied);
        EXPECT_EQ(staleResponse.code, "bridge.snapshot.stale");

        EditorBridgeRequest clear{};
        clear.protocolVersion = kEditorBridgeProtocolVersion;
        clear.requestId = 4U;
        clear.expectedRevision = unknownResponse.snapshot.revision;
        clear.kind = EditorBridgeRequestKind::ClearDeveloperConsole;
        const EditorBridgeResponse clearResponse = bridge.Dispatch(clear);
        ASSERT_TRUE(clearResponse.applied);
        EXPECT_TRUE(clearResponse.snapshot.developerConsole.console.output.empty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDeveloperConsoleBridge, CopiesReadOnlyDataTableCatalogAndAdvancesRevision) {
    Core::EngineCore engine(MakeConsoleBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_catalog_bridge.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        Asset::DataTable table("weapons");
        ASSERT_TRUE(table.DefineColumn("damage", Asset::DataTableColumnType::Integer));
        ASSERT_TRUE(table.AddRow("pistol", {std::int64_t{25}}));
        Asset::DataTableCatalog catalog;
        ASSERT_TRUE(catalog.Upsert(table.GetSnapshot()));
        bridge.SetDataTableCatalogSnapshot(catalog.GetSnapshot());

        const EditorBridgeSnapshot copied = bridge.GetSnapshot();
        ASSERT_GT(copied.revision, initial.revision);
        ASSERT_EQ(copied.dataTableCatalog.entries.size(), 1U);
        EXPECT_EQ(copied.dataTableCatalog.entries.front().name, "weapons");
        EXPECT_EQ(copied.dataTableCatalog.entries.front().columnCount, 1U);
        EXPECT_EQ(copied.dataTableCatalog.entries.front().rowCount, 1U);
        EXPECT_TRUE(copied.dataTableCatalog.entries.front().valid);

        EditorBridgeSnapshot mutatedCopy = copied;
        mutatedCopy.dataTableCatalog.entries.clear();
        const EditorBridgeSnapshot stillNative = bridge.GetSnapshot();
        ASSERT_EQ(stillNative.dataTableCatalog.entries.size(), 1U);
        EXPECT_EQ(stillNative.dataTableCatalog.entries.front().name, "weapons");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDeveloperConsoleBridge, CopiesBoundedDataTablePreviewAsReadOnlyFacts) {
    Core::EngineCore engine(MakeConsoleBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_preview_bridge.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        Asset::DataTable table("weapons");
        ASSERT_TRUE(table.DefineColumn("damage", Asset::DataTableColumnType::Integer));
        ASSERT_TRUE(table.DefineColumn("weight", Asset::DataTableColumnType::Number));
        ASSERT_TRUE(table.AddRow("pistol", {std::int64_t{25}, 1.5}));
        bridge.SetDataTablePreviewSnapshot(table.GetSnapshot());

        const EditorBridgeSnapshot copied = bridge.GetSnapshot();
        ASSERT_GT(copied.revision, initial.revision);
        ASSERT_TRUE(copied.dataTablePreview.available);
        EXPECT_EQ(copied.dataTablePreview.name, "weapons");
        EXPECT_EQ(copied.dataTablePreview.totalColumnCount, 2U);
        EXPECT_EQ(copied.dataTablePreview.totalRowCount, 1U);
        ASSERT_EQ(copied.dataTablePreview.columns.size(), 2U);
        EXPECT_EQ(copied.dataTablePreview.columns[0].name, "damage");
        EXPECT_EQ(copied.dataTablePreview.columns[0].type, Asset::DataTableColumnType::Integer);
        ASSERT_EQ(copied.dataTablePreview.rows.size(), 1U);
        EXPECT_EQ(copied.dataTablePreview.rows[0].identifier, "pistol");
        EXPECT_EQ(copied.dataTablePreview.rows[0].values, (std::vector<std::string>{"25", "1.5"}));

        EditorBridgeSnapshot mutatedCopy = copied;
        mutatedCopy.dataTablePreview.rows.clear();
        EXPECT_EQ(bridge.GetSnapshot().dataTablePreview.rows.size(), 1U);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDeveloperConsoleBridge, RoutesDiscoveryFilterAndHistoryThroughNamedRequests) {
    Core::EngineCore engine(MakeConsoleBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_developer_console_discovery_bridge.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeSnapshot snapshot = bridge.GetSnapshot();

        EditorBridgeRequest prefix{};
        prefix.protocolVersion = kEditorBridgeProtocolVersion;
        prefix.requestId = 10U;
        prefix.expectedRevision = snapshot.revision;
        prefix.kind = EditorBridgeRequestKind::SetDeveloperConsoleCompletionPrefix;
        prefix.developerConsoleCompletionPrefix = "he";
        const EditorBridgeResponse prefixResponse = bridge.Dispatch(prefix);
        ASSERT_TRUE(prefixResponse.applied);
        ASSERT_EQ(prefixResponse.snapshot.developerConsole.console.completions.size(), 1U);
        EXPECT_EQ(prefixResponse.snapshot.developerConsole.console.completions.front().identifier, "help");

        EditorBridgeRequest help{};
        help.protocolVersion = kEditorBridgeProtocolVersion;
        help.requestId = 11U;
        help.expectedRevision = prefixResponse.snapshot.revision;
        help.kind = EditorBridgeRequestKind::SubmitDeveloperConsoleCommand;
        help.developerConsoleCommand = "help";
        const EditorBridgeResponse helpResponse = bridge.Dispatch(help);
        ASSERT_TRUE(helpResponse.applied);

        EditorBridgeRequest unknown = help;
        unknown.requestId = 12U;
        unknown.expectedRevision = helpResponse.snapshot.revision;
        unknown.developerConsoleCommand = "notRegistered";
        const EditorBridgeResponse unknownResponse = bridge.Dispatch(unknown);
        EXPECT_FALSE(unknownResponse.applied);
        ASSERT_FALSE(unknownResponse.snapshot.developerConsole.console.output.empty());

        EditorBridgeRequest filter{};
        filter.protocolVersion = kEditorBridgeProtocolVersion;
        filter.requestId = 13U;
        filter.expectedRevision = unknownResponse.snapshot.revision;
        filter.kind = EditorBridgeRequestKind::SetDeveloperConsoleSeverityFilter;
        filter.developerConsoleSeverityFilter = DeveloperConsoleSeverityFilter::Error;
        const EditorBridgeResponse filterResponse = bridge.Dispatch(filter);
        ASSERT_TRUE(filterResponse.applied);
        ASSERT_FALSE(filterResponse.snapshot.developerConsole.console.output.empty());
        for (const DeveloperConsoleEntry& entry : filterResponse.snapshot.developerConsole.console.output) {
            EXPECT_EQ(entry.severity, DeveloperConsoleSeverity::Error);
        }

        EditorBridgeRequest history{};
        history.protocolVersion = kEditorBridgeProtocolVersion;
        history.requestId = 14U;
        history.expectedRevision = filterResponse.snapshot.revision;
        history.kind = EditorBridgeRequestKind::MoveDeveloperConsoleHistory;
        history.developerConsoleHistoryDelta = -1;
        const EditorBridgeResponse historyResponse = bridge.Dispatch(history);
        ASSERT_TRUE(historyResponse.applied);
        EXPECT_EQ(historyResponse.snapshot.developerConsole.console.historyCursor, 1);
        EXPECT_EQ(historyResponse.snapshot.developerConsole.console.historyEntry, "notRegistered");

        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
