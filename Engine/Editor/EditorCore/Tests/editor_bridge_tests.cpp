// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "engine_core.h"
#include "editor_bridge.h"
#include "mesh_component.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeBridgeTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_editor_bridge_tests.log";
    config.settingsFilePath = "uve_editor_bridge_tests_settings.json";
    config.assetDatabaseFilePath = "uve_editor_bridge_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_bridge_tests_saves";
    config.shaderCachePath = "uve_editor_bridge_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

[[nodiscard]] std::vector<Scene::Entity> GetRoots(Editor& editor) {
    return editor.GetDocumentRoots();
}

TEST(EditorBridgeTest, AssetTableImportCapability_ReportsRawParserBoundaryForSelectedEntry) {
    const std::filesystem::path contentRoot = "uve_editor_bridge_content_import_capability";
    std::filesystem::remove_all(contentRoot);
    std::filesystem::create_directories(contentRoot);
    {
        std::ofstream fixture(contentRoot / "character.dae", std::ios::binary);
        ASSERT_TRUE(fixture.is_open());
        fixture << "raw model source";
    }
    {
        std::ofstream fixture(contentRoot / "notes.txt", std::ios::binary);
        ASSERT_TRUE(fixture.is_open());
        fixture << "plain source document";
    }
    {
        std::ofstream fixture(contentRoot / "basic.vert", std::ios::binary);
        ASSERT_TRUE(fixture.is_open());
        fixture << "#version 450\nvoid main() {}\n";
    }

    Core::EngineConfig config = MakeBridgeTestConfig();
    config.projectContentRoot = contentRoot;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_content_import_capability.uvscene");
        editor.Init();
        EditorBridge bridge(editor);

        const EditorBridgeSnapshot initial = bridge.GetSnapshot();
        EditorBridgeRequest refresh{};
        refresh.protocolVersion = kEditorBridgeProtocolVersion;
        refresh.requestId = 1U;
        refresh.expectedRevision = initial.revision;
        refresh.kind = EditorBridgeRequestKind::RefreshAssetTable;
        const EditorBridgeResponse refreshed = bridge.Dispatch(refresh);
        ASSERT_TRUE(refreshed.applied);

        EditorBridgeRequest select{};
        select.protocolVersion = kEditorBridgeProtocolVersion;
        select.requestId = 2U;
        select.expectedRevision = refreshed.snapshot.revision;
        select.kind = EditorBridgeRequestKind::SelectAssetTableEntry;
        select.contentEntryPath = "character.dae";
        const EditorBridgeResponse selected = bridge.Dispatch(select);
        ASSERT_TRUE(selected.applied);
        ASSERT_TRUE(selected.snapshot.assetTable.selectedEntry.has_value());
        EXPECT_EQ(selected.snapshot.assetTable.selectedEntry->relativePath, "character.dae");
        EXPECT_TRUE(selected.snapshot.assetTable.importAction.hasSelection);
        EXPECT_FALSE(selected.snapshot.assetTable.importAction.canImport);
        EXPECT_FALSE(selected.snapshot.assetTable.importAction.canReimport);
        EXPECT_FALSE(selected.snapshot.assetTable.importAction.importerRegistered);
        EXPECT_TRUE(selected.snapshot.assetTable.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selected.snapshot.assetTable.importAction.sourceKind, "rawModel");
        EXPECT_EQ(selected.snapshot.assetTable.importAction.diagnostic,
                  "format-specific parser is not registered");

        EditorBridgeRequest selectShader{};
        selectShader.protocolVersion = kEditorBridgeProtocolVersion;
        selectShader.requestId = 3U;
        selectShader.expectedRevision = selected.snapshot.revision;
        selectShader.kind = EditorBridgeRequestKind::SelectAssetTableEntry;
        selectShader.contentEntryPath = "basic.vert";
        const EditorBridgeResponse selectedShader = bridge.Dispatch(selectShader);
        ASSERT_TRUE(selectedShader.applied);
        EXPECT_TRUE(selectedShader.snapshot.assetTable.importAction.hasSelection);
        EXPECT_TRUE(selectedShader.snapshot.assetTable.importAction.canImport);
        EXPECT_FALSE(selectedShader.snapshot.assetTable.importAction.canReimport);
        EXPECT_TRUE(selectedShader.snapshot.assetTable.importAction.importerRegistered);
        EXPECT_TRUE(selectedShader.snapshot.assetTable.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selectedShader.snapshot.assetTable.importAction.sourceKind, "rawShader");
        EXPECT_EQ(selectedShader.snapshot.assetTable.importAction.diagnostic,
                  "format-specific parser is registered");

        EditorBridgeRequest selectPlain{};
        selectPlain.protocolVersion = kEditorBridgeProtocolVersion;
        selectPlain.requestId = 4U;
        selectPlain.expectedRevision = selectedShader.snapshot.revision;
        selectPlain.kind = EditorBridgeRequestKind::SelectAssetTableEntry;
        selectPlain.contentEntryPath = "notes.txt";
        const EditorBridgeResponse selectedPlain = bridge.Dispatch(selectPlain);
        ASSERT_TRUE(selectedPlain.applied);

        EditorBridgeRequest queueImport{};
        queueImport.protocolVersion = kEditorBridgeProtocolVersion;
        queueImport.requestId = 5U;
        queueImport.expectedRevision = selectedPlain.snapshot.revision;
        queueImport.kind = EditorBridgeRequestKind::QueueAssetTableImport;
        queueImport.contentEntryPath = "notes.txt";
        queueImport.contentImportDestinationPath = "notes_imported.txt";
        const EditorBridgeResponse queued = bridge.Dispatch(queueImport);
        ASSERT_TRUE(queued.applied);
        EXPECT_EQ(queued.code, "bridge.content.import.queued");
        ASSERT_TRUE(queued.contentImportJobId.has_value());
        EXPECT_EQ(*queued.contentImportJobId, 1U);

        Asset::IAssetImportQueue& importQueue = engine.GetServices().GetAssetImportQueue();
        ASSERT_TRUE(importQueue.Tick());
        const std::vector<Asset::AssetImportJob> jobs = importQueue.GetJobs();
        ASSERT_EQ(jobs.size(), 1U);
        EXPECT_EQ(jobs.front().state, Asset::AssetImportJobState::Succeeded);
        EXPECT_TRUE(jobs.front().resultGuid.has_value());
        EXPECT_TRUE(std::filesystem::exists(contentRoot / "notes_imported.txt"));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(contentRoot);
}

TEST(EditorBridgeTest, AssetTableAnimationEnvelope_ReportsTypedImporterAuthority) {
    const std::filesystem::path contentRoot = "uve_editor_bridge_animation_asset";
    std::filesystem::remove_all(contentRoot);
    std::filesystem::create_directories(contentRoot);
    {
        std::ofstream fixture(contentRoot / "walk.uvanim", std::ios::binary);
        ASSERT_TRUE(fixture.is_open());
        fixture << "typed animation envelope placeholder";
    }

    Core::EngineConfig config = MakeBridgeTestConfig();
    config.projectContentRoot = contentRoot;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_animation_asset.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        EditorBridgeRequest refresh{};
        refresh.protocolVersion = kEditorBridgeProtocolVersion;
        refresh.requestId = 1U;
        refresh.expectedRevision = initial.revision;
        refresh.kind = EditorBridgeRequestKind::RefreshAssetTable;
        const EditorBridgeResponse refreshed = bridge.Dispatch(refresh);
        ASSERT_TRUE(refreshed.applied);

        EditorBridgeRequest select{};
        select.protocolVersion = kEditorBridgeProtocolVersion;
        select.requestId = 2U;
        select.expectedRevision = refreshed.snapshot.revision;
        select.kind = EditorBridgeRequestKind::SelectAssetTableEntry;
        select.contentEntryPath = "walk.uvanim";
        const EditorBridgeResponse selected = bridge.Dispatch(select);
        ASSERT_TRUE(selected.applied);
        ASSERT_TRUE(selected.snapshot.assetTable.importAction.hasSelection);
        EXPECT_TRUE(selected.snapshot.assetTable.importAction.canImport);
        EXPECT_TRUE(selected.snapshot.assetTable.importAction.importerRegistered);
        EXPECT_FALSE(selected.snapshot.assetTable.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selected.snapshot.assetTable.importAction.sourceKind, "animationEnvelope");
        EXPECT_EQ(selected.snapshot.assetTable.importAction.diagnostic,
                  "built-in generic copy importer is registered");

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(contentRoot);
}

TEST(EditorBridgeTest, EntityRef_ValidatesTheFullGenerationalIdentity) {
    const EditorBridgeEntityRef invalid{};
    EXPECT_FALSE(invalid.IsValid());

    const EditorBridgeEntityRef sameIndexDifferentGeneration{
        Scene::kInvalidEntity.index, Scene::kInvalidEntity.generation + 1U};
    EXPECT_TRUE(sameIndexDifferentGeneration.IsValid());

    const EditorBridgeEntityRef ordinaryEntity{0U, 7U};
    EXPECT_TRUE(ordinaryEntity.IsValid());
}

TEST(EditorBridgeTest, Snapshot_ObservesNativeEditorChangesAndIncrementsRevision) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_native_state.uvscene");
        editor.Init();
        EditorBridge bridge(editor);

        const EditorBridgeSnapshot before = bridge.GetSnapshot();
        EXPECT_EQ(before.revision, 1U);
        EXPECT_TRUE(before.selectedEntities.empty());
        EXPECT_FALSE(before.sceneDirty);

        const Scene::Entity created = editor.CreateDocumentEntity(EditorEntityKind::Cube);
        ASSERT_NE(created, Scene::kInvalidEntity);
        const EditorBridgeSnapshot after = bridge.GetSnapshot();
        EXPECT_GT(after.revision, before.revision);
        EXPECT_TRUE(after.sceneDirty);
        ASSERT_EQ(after.selectedEntities.size(), 1U);
        EXPECT_EQ(after.selectedEntities.front().entity, (EditorBridgeEntityRef{created.index, created.generation}));
        ASSERT_TRUE(after.activeEntity.has_value());
        EXPECT_EQ(*after.activeEntity, (EditorBridgeEntityRef{created.index, created.generation}));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeTest, Dispatch_RejectsStaleMutationAfterNativeEditorChange) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_stale_request.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot initial = bridge.GetSnapshot();

        ASSERT_NE(editor.CreateDocumentEntity(EditorEntityKind::Plane), Scene::kInvalidEntity);
        const std::vector<Scene::Entity> rootsBefore = GetRoots(editor);
        const EditorBridgeRequest staleRequest{
            kEditorBridgeProtocolVersion, 41U, initial.revision, EditorBridgeRequestKind::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKind::Cube};
        const EditorBridgeResponse response = bridge.Dispatch(staleRequest);
        EXPECT_FALSE(response.applied);
        EXPECT_EQ(response.code, "bridge.snapshot.stale");
        EXPECT_GT(response.snapshot.revision, initial.revision);
        EXPECT_EQ(GetRoots(editor), rootsBefore);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeTest, Dispatch_RoutesCreateNameUndoRedoThroughNativeCommands) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_commands.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        EditorBridgeSnapshot snapshot = bridge.GetSnapshot();

        const EditorBridgeRequest createRequest{
            kEditorBridgeProtocolVersion, 1U, snapshot.revision, EditorBridgeRequestKind::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKind::UVSphere};
        const EditorBridgeResponse created = bridge.Dispatch(createRequest);
        ASSERT_TRUE(created.applied);
        ASSERT_TRUE(created.createdEntity.has_value());
        EXPECT_TRUE(created.snapshot.sceneDirty);
        EXPECT_TRUE(created.snapshot.canUndo);
        snapshot = created.snapshot;

        const EditorBridgeRequest nameRequest{
            kEditorBridgeProtocolVersion, 2U, snapshot.revision, EditorBridgeRequestKind::SetSelectedEntityName,
            std::nullopt, std::string{"Bridge Authored Name"}, std::nullopt};
        const EditorBridgeResponse named = bridge.Dispatch(nameRequest);
        ASSERT_TRUE(named.applied);
        ASSERT_EQ(named.snapshot.selectedEntities.size(), 1U);
        EXPECT_EQ(named.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name");

        const EditorBridgeRequest undoRequest{
            kEditorBridgeProtocolVersion, 3U, named.snapshot.revision, EditorBridgeRequestKind::Undo,
            std::nullopt, std::nullopt, std::nullopt};
        const EditorBridgeResponse undone = bridge.Dispatch(undoRequest);
        ASSERT_TRUE(undone.applied);
        ASSERT_EQ(undone.snapshot.selectedEntities.size(), 1U);
        EXPECT_NE(undone.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name");

        const EditorBridgeRequest redoRequest{
            kEditorBridgeProtocolVersion, 4U, undone.snapshot.revision, EditorBridgeRequestKind::Redo,
            std::nullopt, std::nullopt, std::nullopt};
        const EditorBridgeResponse redone = bridge.Dispatch(redoRequest);
        ASSERT_TRUE(redone.applied);
        ASSERT_EQ(redone.snapshot.selectedEntities.size(), 1U);
        EXPECT_EQ(redone.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name");

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeTest, Dispatch_RejectsUnsupportedProtocolAndInvalidEntityWithoutMutation) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_rejection.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        const EditorBridgeSnapshot snapshot = bridge.GetSnapshot();

        const EditorBridgeRequest invalidVersion{
            99U, 7U, snapshot.revision, EditorBridgeRequestKind::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKind::Cube};
        const EditorBridgeResponse versionResponse = bridge.Dispatch(invalidVersion);
        EXPECT_FALSE(versionResponse.applied);
        EXPECT_EQ(versionResponse.code, "bridge.protocol.unsupported");
        // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(GetRoots(editor).size(), 1U);
        EXPECT_EQ(GetRoots(editor)[0U], editor.GetDocumentSceneRoot());

        const EditorBridgeRequest invalidEntity{
            kEditorBridgeProtocolVersion, 8U, snapshot.revision, EditorBridgeRequestKind::SelectEntity,
            EditorBridgeEntityRef{3U, 9U}, std::nullopt, std::nullopt};
        const EditorBridgeResponse entityResponse = bridge.Dispatch(invalidEntity);
        EXPECT_FALSE(entityResponse.applied);
        EXPECT_EQ(entityResponse.code, "bridge.entity.invalid");
        EXPECT_TRUE(entityResponse.snapshot.selectedEntities.empty());
        // The document was not mutated: the only root is the ever-present scene root.
        ASSERT_EQ(GetRoots(editor).size(), 1U);
        EXPECT_EQ(GetRoots(editor)[0U], editor.GetDocumentSceneRoot());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeTest, Snapshot_CopiesHierarchyInspectorAndNativePanelSessionState) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_panel_snapshot.uvscene");
        editor.Init();
        EditorBridge bridge(editor);

        const Scene::Entity root = editor.CreateDocumentEntity(EditorEntityKind::Cube);
        ASSERT_NE(root, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.SetSelectedEntityName("Bridge Root"));
        const Scene::Entity child = editor.CreateDocumentEntity(EditorEntityKind::Plane);
        ASSERT_NE(child, Scene::kInvalidEntity);
        // No manual reparent needed anymore: creating the Plane while the Cube was selected
        // already parented the child under it (selected-or-root creation parenting).
        ASSERT_TRUE(editor.SetSelectedEntityName("Bridge Child"));
        auto& entityManager = engine.GetServices().GetEntityManager();
        entityManager.AddComponent<Scene::MeshComponent>(
            child, Scene::MeshComponent{Asset::AssetGuid{0x1111U}, Asset::AssetGuid{0x2222U}});

        EditorBridgeSnapshot snapshot = bridge.GetSnapshot();
        // The hierarchy leads with the document's ever-present SceneRoot, then the level's Viewport
        // and its object folder; the authored pair sits inside that folder.
        const Scene::Entity sceneRoot = editor.GetDocumentSceneRoot();
        ASSERT_EQ(snapshot.hierarchy.entries.size(), 5U);
        EXPECT_EQ(snapshot.hierarchy.entries[0].entity,
                  (EditorBridgeEntityRef{sceneRoot.index, sceneRoot.generation}));
        EXPECT_EQ(snapshot.hierarchy.entries[0].displayLabel, "SceneRoot");
        EXPECT_EQ(snapshot.hierarchy.entries[0].depth, 0U);
        EXPECT_EQ(snapshot.hierarchy.entries[0].childCount, 1U);
        EXPECT_EQ(snapshot.hierarchy.entries[1].displayLabel, "uve_editor_bridge_panel_snapshot"); // the open level's asset name
        EXPECT_EQ(snapshot.hierarchy.entries[2].displayLabel, "World");
        EXPECT_EQ(snapshot.hierarchy.entries[2].depth, 2U);
        EXPECT_EQ(snapshot.hierarchy.entries[3].entity, (EditorBridgeEntityRef{root.index, root.generation}));
        ASSERT_TRUE(snapshot.hierarchy.entries[3].parent.has_value());
        EXPECT_EQ(*snapshot.hierarchy.entries[3].parent, snapshot.hierarchy.entries[2].entity);
        EXPECT_EQ(snapshot.hierarchy.entries[3].displayLabel, "Bridge Root");
        EXPECT_EQ(snapshot.hierarchy.entries[3].depth, 3U);
        EXPECT_EQ(snapshot.hierarchy.entries[3].childCount, 1U);
        EXPECT_EQ(snapshot.hierarchy.entries[4].entity, (EditorBridgeEntityRef{child.index, child.generation}));
        ASSERT_TRUE(snapshot.hierarchy.entries[4].parent.has_value());
        EXPECT_EQ(*snapshot.hierarchy.entries[4].parent, (EditorBridgeEntityRef{root.index, root.generation}));
        EXPECT_EQ(snapshot.hierarchy.entries[4].depth, 4U);
        ASSERT_EQ(snapshot.inspector.mode, EditorBridgeInspectorMode::SingleSelection);
        ASSERT_TRUE(snapshot.inspector.activeEntity.has_value());
        EXPECT_EQ(snapshot.inspector.activeEntity->displayLabel, "Bridge Child");
        ASSERT_TRUE(snapshot.inspector.parent.has_value());
        EXPECT_EQ(snapshot.inspector.parent->displayLabel, "Bridge Root");
        // A PlaneMesh3D is a SurfaceInstance3D: its own sections (the collision it was created with
        // is drawn inside the mesh's section - there is no separate PrimitiveMesh3D fold any more),
        // its bases, Object3D, then the Object section.
        const std::vector<std::string> objectSection{"process",           "physics-interpolation", "auto-translate",
                                                   "editor-description", "script",                "object-metadata"};
        std::vector<std::string> expectedDrawers{"mesh", "surface-instance", "render-instance",
                                                 "transform", "visibility"};
        expectedDrawers.insert(expectedDrawers.end(), objectSection.begin(), objectSection.end());
        EXPECT_EQ(snapshot.inspector.eligibleDrawerIds, expectedDrawers);
        std::vector<std::string> expectedAttached{"mesh", "surface-instance", "render-instance", "visibility"};
        expectedAttached.insert(expectedAttached.end(), objectSection.begin(), objectSection.end());
        EXPECT_EQ(snapshot.inspector.attachedComponentIds, expectedAttached);
        ASSERT_TRUE(snapshot.inspector.assetBinding.has_value());
        ASSERT_TRUE(snapshot.inspector.assetBinding->meshGuid.has_value());
        ASSERT_TRUE(snapshot.inspector.assetBinding->materialGuid.has_value());
        EXPECT_EQ(*snapshot.inspector.assetBinding->meshGuid, 0x1111U);
        EXPECT_EQ(*snapshot.inspector.assetBinding->materialGuid, 0x2222U);
        EXPECT_TRUE(snapshot.inspector.canEditSelectedName);
        EXPECT_FALSE(snapshot.assetTable.initialized);
        EXPECT_EQ(snapshot.viewportSurface.state, EditorBridgeViewportSurfaceState::Unavailable);
        EXPECT_EQ(snapshot.viewportSurface.generation, 0U);
        EXPECT_EQ(snapshot.viewportSurface.width, 0U);
        EXPECT_EQ(snapshot.viewportSurface.height, 0U);
        EXPECT_TRUE(snapshot.viewportSurface.nativeRendererOwnsSurface);
        EXPECT_FALSE(snapshot.viewportSurface.managedAttachAllowed);

        EditorBridgeRequest surfaceRequest{};
        surfaceRequest.protocolVersion = kEditorBridgeProtocolVersion;
        surfaceRequest.requestId = 9U;
        surfaceRequest.expectedRevision = 0U;
        surfaceRequest.kind = EditorBridgeRequestKind::ReadViewportSurface;
        const EditorBridgeResponse surfaceResponse = bridge.Dispatch(surfaceRequest);
        EXPECT_FALSE(surfaceResponse.applied);
        EXPECT_EQ(surfaceResponse.code, "bridge.viewport_surface.unavailable");
        EXPECT_EQ(surfaceResponse.snapshot.viewportSurface, snapshot.viewportSurface);

        EditorBridgeRequest filterRequest{};
        filterRequest.protocolVersion = kEditorBridgeProtocolVersion;
        filterRequest.requestId = 10U;
        filterRequest.expectedRevision = snapshot.revision;
        filterRequest.kind = EditorBridgeRequestKind::SetHierarchyFilter;
        filterRequest.hierarchyFilter = "child";
        const EditorBridgeResponse filtered = bridge.Dispatch(filterRequest);
        ASSERT_TRUE(filtered.applied);
        EXPECT_TRUE(filtered.snapshot.hierarchy.filterActive);
        // The filter match plus its ancestor chain: SceneRoot, the Viewport and its folder above
        // the authored pair.
        ASSERT_EQ(filtered.snapshot.hierarchy.entries.size(), 5U);
        EXPECT_GT(filtered.snapshot.revision, snapshot.revision);

        EditorBridgeRequest toggleRequest{};
        toggleRequest.protocolVersion = kEditorBridgeProtocolVersion;
        toggleRequest.requestId = 11U;
        toggleRequest.expectedRevision = filtered.snapshot.revision;
        toggleRequest.kind = EditorBridgeRequestKind::ToggleEntitySelection;
        toggleRequest.entity = EditorBridgeEntityRef{root.index, root.generation};
        const EditorBridgeResponse toggled = bridge.Dispatch(toggleRequest);
        ASSERT_TRUE(toggled.applied);
        EXPECT_EQ(toggled.snapshot.inspector.mode, EditorBridgeInspectorMode::MultiSelection);
        EXPECT_FALSE(toggled.snapshot.inspector.canEditSelectedName);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorBridgeTest, Snapshot_BoundsCopiedPanelRowsWithoutClaimingDeletion) {
    Core::EngineCore engine(MakeBridgeTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_bridge_panel_bound.uvscene");
        editor.Init();
        EditorBridge bridge(editor);
        for (std::size_t index = 0U; index < kEditorBridgeMaximumPanelEntries + 1U; ++index) {
            ASSERT_NE(editor.CreateDocumentEntity(EditorEntityKind::Empty), Scene::kInvalidEntity);
        }

        const EditorBridgeSnapshot snapshot = bridge.GetSnapshot();
        EXPECT_EQ(snapshot.hierarchy.entries.size(), kEditorBridgeMaximumPanelEntries);
        EXPECT_TRUE(snapshot.hierarchy.truncated);

        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
