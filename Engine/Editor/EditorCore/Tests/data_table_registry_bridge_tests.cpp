// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "data_table_registry.h"
#include "engine_core.h"
#include "editor_bridge.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeRegistryBridgeConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_data_table_registry_bridge_tests.log";
    config.settingsFilePath = "uve_data_table_registry_bridge_tests_settings.json";
    config.assetDatabaseFilePath = "uve_data_table_registry_bridge_tests_assets.json";
    config.saveDirectoryPath = "uve_data_table_registry_bridge_tests_saves";
    config.shaderCachePath = "uve_data_table_registry_bridge_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

[[nodiscard]] Asset::DataTable MakeTable(std::string name, std::string identifier,
                                               std::int64_t value) {
    Asset::DataTable table(std::move(name));
    static_cast<void>(table.DefineColumn("value", Asset::DataTableColumnType::Integer));
    static_cast<void>(table.AddRow(std::move(identifier), {value}));
    return table;
}

TEST(EditorDataTableRegistryBridge, RegistryIsAuthoritativeForCatalogAndPreview) {
    Core::EngineCore engine(MakeRegistryBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_registry_authority.uvscene");
        editor.Init();
        Asset::DataTableRegistry registry;
        EditorBridge bridge(editor, &registry);

        const EditorBridgeSnapshot initial = bridge.GetSnapshot();
        ASSERT_TRUE(initial.dataTableCatalog.entries.empty());
        EXPECT_FALSE(initial.dataTablePreview.available);

        ASSERT_TRUE(registry.Register(MakeTable("zeta", "last", 9)));
        ASSERT_TRUE(registry.Register(MakeTable("alpha", "first", 1)));
        const EditorBridgeSnapshot catalog = bridge.GetSnapshot();
        ASSERT_GT(catalog.revision, initial.revision);
        ASSERT_EQ(catalog.dataTableCatalog.entries.size(), 2U);
        EXPECT_EQ(catalog.dataTableCatalog.entries[0].name, "alpha");
        EXPECT_EQ(catalog.dataTableCatalog.entries[1].name, "zeta");
        EXPECT_EQ(catalog.dataTableCatalog.generation, registry.GetCatalogSnapshot().generation);

        Asset::DataTable legacyTable = MakeTable("legacy", "ignored", 77);
        Asset::DataTableCatalog legacyCatalog;
        ASSERT_TRUE(legacyCatalog.Upsert(legacyTable.GetSnapshot()));
        bridge.SetDataTableCatalogSnapshot(legacyCatalog.GetSnapshot());
        const EditorBridgeSnapshot registryStillWins = bridge.GetSnapshot();
        ASSERT_EQ(registryStillWins.dataTableCatalog.entries.size(), 2U);
        EXPECT_EQ(registryStillWins.dataTableCatalog.entries.front().name, "alpha");

        ASSERT_TRUE(bridge.SetPreviewTable("alpha"));
        const EditorBridgeSnapshot preview = bridge.GetSnapshot();
        ASSERT_TRUE(preview.dataTablePreview.available);
        EXPECT_EQ(preview.dataTablePreview.name, "alpha");
        ASSERT_EQ(preview.dataTablePreview.rows.size(), 1U);
        EXPECT_EQ(preview.dataTablePreview.rows.front().identifier, "first");
        EXPECT_EQ(preview.dataTablePreview.rows.front().values, (std::vector<std::string>{"1"}));

        Asset::DataTableSnapshot legacyPreview = MakeTable("legacy-preview", "wrong", 88).GetSnapshot();
        bridge.SetDataTablePreviewSnapshot(std::move(legacyPreview));
        const EditorBridgeSnapshot previewStillWins = bridge.GetSnapshot();
        EXPECT_EQ(previewStillWins.dataTablePreview.name, "alpha");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDataTableRegistryBridge, PreviewSelectionRejectsUnknownAndReflectsRemoval) {
    Core::EngineCore engine(MakeRegistryBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_registry_selection.uvscene");
        editor.Init();
        Asset::DataTableRegistry registry;
        EditorBridge bridge(editor, &registry);
        ASSERT_TRUE(registry.Register(MakeTable("weapons", "pistol", 25)));

        const EditorBridgeSnapshot before = bridge.GetSnapshot();
        EXPECT_FALSE(bridge.SetPreviewTable("missing"));
        EXPECT_EQ(bridge.GetSnapshot().revision, before.revision);
        EXPECT_FALSE(bridge.GetSnapshot().dataTablePreview.available);

        ASSERT_TRUE(bridge.SetPreviewTable("weapons"));
        const EditorBridgeSnapshot selected = bridge.GetSnapshot();
        ASSERT_TRUE(selected.dataTablePreview.available);
        EXPECT_EQ(selected.dataTablePreview.name, "weapons");

        ASSERT_TRUE(registry.Remove("weapons"));
        const EditorBridgeSnapshot removed = bridge.GetSnapshot();
        EXPECT_TRUE(removed.dataTableCatalog.entries.empty());
        EXPECT_FALSE(removed.dataTablePreview.available);
        EXPECT_EQ(removed.dataTablePreview.reason,
                  "The selected native data-table preview is no longer available.");

        ASSERT_TRUE(bridge.SetPreviewTable(""));
        const EditorBridgeSnapshot cleared = bridge.GetSnapshot();
        EXPECT_FALSE(cleared.dataTablePreview.available);
        EXPECT_EQ(cleared.dataTablePreview.reason,
                  "No native data-table preview is selected in this bridge session.");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDataTableRegistryBridge, RegistryMutationAdvancesBridgeRevisionOnlyWhenObservable) {
    Core::EngineCore engine(MakeRegistryBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_registry_revision.uvscene");
        editor.Init();
        Asset::DataTableRegistry registry;
        EditorBridge bridge(editor, &registry);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        ASSERT_TRUE(registry.Register(MakeTable("weapons", "pistol", 25)));
        const EditorBridgeSnapshot registered = bridge.GetSnapshot();
        EXPECT_GT(registered.revision, initial.revision);
        EXPECT_EQ(bridge.GetSnapshot().revision, registered.revision);

        ASSERT_TRUE(registry.Remove("weapons"));
        const EditorBridgeSnapshot removed = bridge.GetSnapshot();
        EXPECT_GT(removed.revision, registered.revision);

        EXPECT_FALSE(registry.Remove("weapons"));
        EXPECT_EQ(bridge.GetSnapshot().revision, removed.revision);

        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace

TEST(EditorDataTableRegistryBridge, DispatchRoutesRevisionCheckedPreviewSelection) {
    Core::EngineCore engine(MakeRegistryBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_registry_dispatch.uvscene");
        editor.Init();
        Asset::DataTableRegistry registry;
        ASSERT_TRUE(registry.Register(MakeTable("weapons", "pistol", 25)));
        EditorBridge bridge(editor, &registry);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        EditorBridgeRequest select{};
        select.protocolVersion = kEditorBridgeProtocolVersion;
        select.requestId = 1U;
        select.expectedRevision = initial.revision;
        select.kind = EditorBridgeRequestKind::SelectDataTablePreview;
        select.dataTableName = "weapons";
        const EditorBridgeResponse selected = bridge.Dispatch(select);
        ASSERT_TRUE(selected.applied);
        EXPECT_EQ(selected.code, "bridge.command.applied");
        EXPECT_EQ(selected.snapshot.dataTablePreview.name, "weapons");

        EditorBridgeRequest unknown = select;
        unknown.requestId = 2U;
        unknown.expectedRevision = selected.snapshot.revision;
        unknown.dataTableName = "missing";
        const EditorBridgeResponse rejected = bridge.Dispatch(unknown);
        EXPECT_FALSE(rejected.applied);
        EXPECT_EQ(rejected.code, "bridge.data_table.preview.invalid");
        EXPECT_EQ(rejected.snapshot.revision, selected.snapshot.revision);
        EXPECT_EQ(rejected.snapshot.dataTablePreview.name, "weapons");

        EditorBridgeRequest stale = select;
        stale.requestId = 3U;
        stale.expectedRevision = initial.revision;
        stale.dataTableName = "weapons";
        const EditorBridgeResponse staleResponse = bridge.Dispatch(stale);
        EXPECT_FALSE(staleResponse.applied);
        EXPECT_EQ(staleResponse.code, "bridge.snapshot.stale");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorDataTableRegistryBridge, DispatchRejectsPreviewSelectionWithoutRegistryAuthority) {
    Core::EngineCore engine(MakeRegistryBridgeConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_data_table_registry_legacy_dispatch.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        EditorBridgeRequest request{};
        request.protocolVersion = kEditorBridgeProtocolVersion;
        request.requestId = 4U;
        request.expectedRevision = initial.revision;
        request.kind = EditorBridgeRequestKind::SelectDataTablePreview;
        request.dataTableName = "weapons";
        const EditorBridgeResponse response = bridge.Dispatch(request);
        EXPECT_FALSE(response.applied);
        EXPECT_EQ(response.code, "bridge.data_table.registry.unavailable");
        EXPECT_EQ(response.snapshot.revision, initial.revision);

        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace UVE::Editor::Tests
