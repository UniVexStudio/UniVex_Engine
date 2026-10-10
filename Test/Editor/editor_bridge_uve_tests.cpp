// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/core/engine_core_uve.h"
#include "uve/editor/editor_bridge_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/object/scene_object_registry_uve.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeBridgeTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_editor_bridge_tests.log";
    config.settingsFilePath = "uve_editor_bridge_tests_settings.json";
    config.assetDatabaseFilePath = "uve_editor_bridge_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_bridge_tests_saves";
    config.shaderCachePath = "uve_editor_bridge_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

[[nodiscard]] std::vector<Scene::EntityUVE> GetRootsUVE(EditorUVE& editor) {
    return editor.GetDocumentRootsUVE();
}

TEST(EditorBridgeUVETest, ContentBrowserImportCapabilityUVE_ReportsRawParserBoundaryForSelectedEntry) {
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

    Core::EngineConfigUVE config = MakeBridgeTestConfigUVE();
    config.projectContentRootUVE = contentRoot;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_content_import_capability.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);

        const EditorBridgeSnapshotUVE initial = bridge.GetSnapshotUVE();
        EditorBridgeRequestUVE refresh{};
        refresh.protocolVersion = kEditorBridgeProtocolVersionUVE;
        refresh.requestId = 1U;
        refresh.expectedRevision = initial.revision;
        refresh.kind = EditorBridgeRequestKindUVE::RefreshContentBrowser;
        const EditorBridgeResponseUVE refreshed = bridge.DispatchUVE(refresh);
        ASSERT_TRUE(refreshed.applied);

        EditorBridgeRequestUVE select{};
        select.protocolVersion = kEditorBridgeProtocolVersionUVE;
        select.requestId = 2U;
        select.expectedRevision = refreshed.snapshot.revision;
        select.kind = EditorBridgeRequestKindUVE::SelectContentBrowserEntry;
        select.contentEntryPath = "character.dae";
        const EditorBridgeResponseUVE selected = bridge.DispatchUVE(select);
        ASSERT_TRUE(selected.applied);
        ASSERT_TRUE(selected.snapshot.contentBrowser.selectedEntry.has_value());
        EXPECT_EQ(selected.snapshot.contentBrowser.selectedEntry->relativePath, "character.dae");
        EXPECT_TRUE(selected.snapshot.contentBrowser.importAction.hasSelection);
        EXPECT_FALSE(selected.snapshot.contentBrowser.importAction.canImport);
        EXPECT_FALSE(selected.snapshot.contentBrowser.importAction.canReimport);
        EXPECT_FALSE(selected.snapshot.contentBrowser.importAction.importerRegistered);
        EXPECT_TRUE(selected.snapshot.contentBrowser.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selected.snapshot.contentBrowser.importAction.sourceKind, "rawModel");
        EXPECT_EQ(selected.snapshot.contentBrowser.importAction.diagnostic,
                  "format-specific parser is not registered");

        EditorBridgeRequestUVE selectShader{};
        selectShader.protocolVersion = kEditorBridgeProtocolVersionUVE;
        selectShader.requestId = 3U;
        selectShader.expectedRevision = selected.snapshot.revision;
        selectShader.kind = EditorBridgeRequestKindUVE::SelectContentBrowserEntry;
        selectShader.contentEntryPath = "basic.vert";
        const EditorBridgeResponseUVE selectedShader = bridge.DispatchUVE(selectShader);
        ASSERT_TRUE(selectedShader.applied);
        EXPECT_TRUE(selectedShader.snapshot.contentBrowser.importAction.hasSelection);
        EXPECT_TRUE(selectedShader.snapshot.contentBrowser.importAction.canImport);
        EXPECT_FALSE(selectedShader.snapshot.contentBrowser.importAction.canReimport);
        EXPECT_TRUE(selectedShader.snapshot.contentBrowser.importAction.importerRegistered);
        EXPECT_TRUE(selectedShader.snapshot.contentBrowser.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selectedShader.snapshot.contentBrowser.importAction.sourceKind, "rawShader");
        EXPECT_EQ(selectedShader.snapshot.contentBrowser.importAction.diagnostic,
                  "format-specific parser is registered");

        EditorBridgeRequestUVE selectPlain{};
        selectPlain.protocolVersion = kEditorBridgeProtocolVersionUVE;
        selectPlain.requestId = 4U;
        selectPlain.expectedRevision = selectedShader.snapshot.revision;
        selectPlain.kind = EditorBridgeRequestKindUVE::SelectContentBrowserEntry;
        selectPlain.contentEntryPath = "notes.txt";
        const EditorBridgeResponseUVE selectedPlain = bridge.DispatchUVE(selectPlain);
        ASSERT_TRUE(selectedPlain.applied);

        EditorBridgeRequestUVE queueImport{};
        queueImport.protocolVersion = kEditorBridgeProtocolVersionUVE;
        queueImport.requestId = 5U;
        queueImport.expectedRevision = selectedPlain.snapshot.revision;
        queueImport.kind = EditorBridgeRequestKindUVE::QueueContentBrowserImport;
        queueImport.contentEntryPath = "notes.txt";
        queueImport.contentImportDestinationPath = "notes_imported.txt";
        const EditorBridgeResponseUVE queued = bridge.DispatchUVE(queueImport);
        ASSERT_TRUE(queued.applied);
        EXPECT_EQ(queued.code, "bridge.content.import.queued");
        ASSERT_TRUE(queued.contentImportJobId.has_value());
        EXPECT_EQ(*queued.contentImportJobId, 1U);

        Asset::IAssetImportQueueUVE& importQueue = engine.GetServicesUVE().GetAssetImportQueueUVE();
        ASSERT_TRUE(importQueue.TickUVE());
        const std::vector<Asset::AssetImportJobUVE> jobs = importQueue.GetJobsUVE();
        ASSERT_EQ(jobs.size(), 1U);
        EXPECT_EQ(jobs.front().state, Asset::AssetImportJobStateUVE::Succeeded);
        EXPECT_TRUE(jobs.front().resultGuid.has_value());
        EXPECT_TRUE(std::filesystem::exists(contentRoot / "notes_imported.txt"));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove_all(contentRoot);
}

TEST(EditorBridgeUVETest, ContentBrowserAnimationEnvelopeUVE_ReportsTypedImporterAuthority) {
    const std::filesystem::path contentRoot = "uve_editor_bridge_animation_asset";
    std::filesystem::remove_all(contentRoot);
    std::filesystem::create_directories(contentRoot);
    {
        std::ofstream fixture(contentRoot / "walk.uvanim", std::ios::binary);
        ASSERT_TRUE(fixture.is_open());
        fixture << "typed animation envelope placeholder";
    }

    Core::EngineConfigUVE config = MakeBridgeTestConfigUVE();
    config.projectContentRootUVE = contentRoot;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_animation_asset.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);
        const EditorBridgeSnapshotUVE initial = bridge.GetSnapshotUVE();

        EditorBridgeRequestUVE refresh{};
        refresh.protocolVersion = kEditorBridgeProtocolVersionUVE;
        refresh.requestId = 1U;
        refresh.expectedRevision = initial.revision;
        refresh.kind = EditorBridgeRequestKindUVE::RefreshContentBrowser;
        const EditorBridgeResponseUVE refreshed = bridge.DispatchUVE(refresh);
        ASSERT_TRUE(refreshed.applied);

        EditorBridgeRequestUVE select{};
        select.protocolVersion = kEditorBridgeProtocolVersionUVE;
        select.requestId = 2U;
        select.expectedRevision = refreshed.snapshot.revision;
        select.kind = EditorBridgeRequestKindUVE::SelectContentBrowserEntry;
        select.contentEntryPath = "walk.uvanim";
        const EditorBridgeResponseUVE selected = bridge.DispatchUVE(select);
        ASSERT_TRUE(selected.applied);
        ASSERT_TRUE(selected.snapshot.contentBrowser.importAction.hasSelection);
        EXPECT_TRUE(selected.snapshot.contentBrowser.importAction.canImport);
        EXPECT_TRUE(selected.snapshot.contentBrowser.importAction.importerRegistered);
        EXPECT_FALSE(selected.snapshot.contentBrowser.importAction.requiresFormatSpecificParser);
        EXPECT_EQ(selected.snapshot.contentBrowser.importAction.sourceKind, "animationEnvelope");
        EXPECT_EQ(selected.snapshot.contentBrowser.importAction.diagnostic,
                  "built-in generic copy importer is registered");

        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove_all(contentRoot);
}

TEST(EditorBridgeUVETest, EntityRefUVE_ValidatesTheFullGenerationalIdentity) {
    const EditorBridgeEntityRefUVE invalid{};
    EXPECT_FALSE(invalid.IsValidUVE());

    const EditorBridgeEntityRefUVE sameIndexDifferentGeneration{
        Scene::kInvalidEntityUVE.index, Scene::kInvalidEntityUVE.generation + 1U};
    EXPECT_TRUE(sameIndexDifferentGeneration.IsValidUVE());

    const EditorBridgeEntityRefUVE ordinaryEntity{0U, 7U};
    EXPECT_TRUE(ordinaryEntity.IsValidUVE());
}

TEST(EditorBridgeUVETest, SnapshotUVE_ObservesNativeEditorChangesAndIncrementsRevision) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_native_state.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);

        const EditorBridgeSnapshotUVE before = bridge.GetSnapshotUVE();
        EXPECT_EQ(before.revision, 1U);
        EXPECT_TRUE(before.selectedEntities.empty());
        EXPECT_FALSE(before.sceneDirty);

        const Scene::EntityUVE created = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube);
        ASSERT_NE(created, Scene::kInvalidEntityUVE);
        const EditorBridgeSnapshotUVE after = bridge.GetSnapshotUVE();
        EXPECT_GT(after.revision, before.revision);
        EXPECT_TRUE(after.sceneDirty);
        ASSERT_EQ(after.selectedEntities.size(), 1U);
        EXPECT_EQ(after.selectedEntities.front().entity, (EditorBridgeEntityRefUVE{created.index, created.generation}));
        ASSERT_TRUE(after.activeEntity.has_value());
        EXPECT_EQ(*after.activeEntity, (EditorBridgeEntityRefUVE{created.index, created.generation}));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorBridgeUVETest, DispatchUVE_RejectsStaleMutationAfterNativeEditorChange) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_stale_request.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);
        const EditorBridgeSnapshotUVE initial = bridge.GetSnapshotUVE();

        ASSERT_NE(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Plane), Scene::kInvalidEntityUVE);
        const std::vector<Scene::EntityUVE> rootsBefore = GetRootsUVE(editor);
        const EditorBridgeRequestUVE staleRequest{
            kEditorBridgeProtocolVersionUVE, 41U, initial.revision, EditorBridgeRequestKindUVE::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKindUVE::Cube};
        const EditorBridgeResponseUVE response = bridge.DispatchUVE(staleRequest);
        EXPECT_FALSE(response.applied);
        EXPECT_EQ(response.code, "bridge.snapshot.stale");
        EXPECT_GT(response.snapshot.revision, initial.revision);
        EXPECT_EQ(GetRootsUVE(editor), rootsBefore);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorBridgeUVETest, DispatchUVE_RoutesCreateNameUndoRedoThroughNativeCommands) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_commands.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);
        EditorBridgeSnapshotUVE snapshot = bridge.GetSnapshotUVE();

        const EditorBridgeRequestUVE createRequest{
            kEditorBridgeProtocolVersionUVE, 1U, snapshot.revision, EditorBridgeRequestKindUVE::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKindUVE::UVSphere};
        const EditorBridgeResponseUVE created = bridge.DispatchUVE(createRequest);
        ASSERT_TRUE(created.applied);
        ASSERT_TRUE(created.createdEntity.has_value());
        EXPECT_TRUE(created.snapshot.sceneDirty);
        EXPECT_TRUE(created.snapshot.canUndo);
        snapshot = created.snapshot;

        const EditorBridgeRequestUVE nameRequest{
            kEditorBridgeProtocolVersionUVE, 2U, snapshot.revision, EditorBridgeRequestKindUVE::SetSelectedEntityName,
            std::nullopt, std::string{"Bridge Authored Name"}, std::nullopt};
        const EditorBridgeResponseUVE named = bridge.DispatchUVE(nameRequest);
        ASSERT_TRUE(named.applied);
        ASSERT_EQ(named.snapshot.selectedEntities.size(), 1U);
        EXPECT_EQ(named.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name");

        // The bridge routes manual names through the same collision policy as inline rename.
        const EditorBridgeRequestUVE createAgainRequest{
            kEditorBridgeProtocolVersionUVE, 3U, named.snapshot.revision,
            EditorBridgeRequestKindUVE::CreateDocumentEntity, std::nullopt, std::nullopt,
            EditorEntityKindUVE::UVSphere};
        const EditorBridgeResponseUVE createdAgain = bridge.DispatchUVE(createAgainRequest);
        ASSERT_TRUE(createdAgain.applied);
        ASSERT_TRUE(createdAgain.createdEntity.has_value());

        const EditorBridgeRequestUVE collisionNameRequest{
            kEditorBridgeProtocolVersionUVE, 4U, createdAgain.snapshot.revision,
            EditorBridgeRequestKindUVE::SetSelectedEntityName, std::nullopt,
            std::string{"Bridge Authored Name"}, std::nullopt};
        const EditorBridgeResponseUVE collisionNamed = bridge.DispatchUVE(collisionNameRequest);
        ASSERT_TRUE(collisionNamed.applied);
        ASSERT_EQ(collisionNamed.snapshot.selectedEntities.size(), 1U);
        EXPECT_EQ(collisionNamed.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name 2");

        const EditorBridgeRequestUVE undoRequest{
            kEditorBridgeProtocolVersionUVE, 5U, collisionNamed.snapshot.revision, EditorBridgeRequestKindUVE::Undo,
            std::nullopt, std::nullopt, std::nullopt};
        const EditorBridgeResponseUVE undone = bridge.DispatchUVE(undoRequest);
        ASSERT_TRUE(undone.applied);
        ASSERT_EQ(undone.snapshot.selectedEntities.size(), 1U);
        EXPECT_NE(undone.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name 2");

        const EditorBridgeRequestUVE redoRequest{
            kEditorBridgeProtocolVersionUVE, 6U, undone.snapshot.revision, EditorBridgeRequestKindUVE::Redo,
            std::nullopt, std::nullopt, std::nullopt};
        const EditorBridgeResponseUVE redone = bridge.DispatchUVE(redoRequest);
        ASSERT_TRUE(redone.applied);
        ASSERT_EQ(redone.snapshot.selectedEntities.size(), 1U);
        EXPECT_EQ(redone.snapshot.selectedEntities.front().displayLabel, "Bridge Authored Name 2");

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorBridgeUVETest, DispatchUVE_RejectsUnsupportedProtocolAndInvalidEntityWithoutMutation) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_rejection.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);
        const EditorBridgeSnapshotUVE snapshot = bridge.GetSnapshotUVE();

        const EditorBridgeRequestUVE invalidVersion{
            99U, 7U, snapshot.revision, EditorBridgeRequestKindUVE::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKindUVE::Cube};
        const EditorBridgeResponseUVE versionResponse = bridge.DispatchUVE(invalidVersion);
        EXPECT_FALSE(versionResponse.applied);
        EXPECT_EQ(versionResponse.code, "bridge.protocol.unsupported");
        // The document was not mutated: the only root is the ever-present Object.
        ASSERT_EQ(GetRootsUVE(editor).size(), 1U);
        EXPECT_EQ(GetRootsUVE(editor)[0U], editor.GetDocumentObjectUVE());

        const EditorBridgeRequestUVE invalidEntity{
            kEditorBridgeProtocolVersionUVE, 8U, snapshot.revision, EditorBridgeRequestKindUVE::SelectEntity,
            EditorBridgeEntityRefUVE{3U, 9U}, std::nullopt, std::nullopt};
        const EditorBridgeResponseUVE entityResponse = bridge.DispatchUVE(invalidEntity);
        EXPECT_FALSE(entityResponse.applied);
        EXPECT_EQ(entityResponse.code, "bridge.entity.invalid");
        EXPECT_TRUE(entityResponse.snapshot.selectedEntities.empty());
        // The document was not mutated: the only root is the ever-present Object.
        ASSERT_EQ(GetRootsUVE(editor).size(), 1U);
        EXPECT_EQ(GetRootsUVE(editor)[0U], editor.GetDocumentObjectUVE());

        const EditorBridgeRequestUVE createRequest{
            kEditorBridgeProtocolVersionUVE, 9U, snapshot.revision, EditorBridgeRequestKindUVE::CreateDocumentEntity,
            std::nullopt, std::nullopt, EditorEntityKindUVE::Cube};
        const EditorBridgeResponseUVE created = bridge.DispatchUVE(createRequest);
        ASSERT_TRUE(created.applied);
        ASSERT_TRUE(created.createdEntity.has_value());
        const Scene::EntityUVE lockedEntity{created.createdEntity->index, created.createdEntity->generation};
        ASSERT_TRUE(editor.SetEntityLockedUVE(lockedEntity, true));
        const EditorBridgeSnapshotUVE lockedSnapshot = bridge.GetSnapshotUVE();
        EXPECT_TRUE(lockedSnapshot.selectedEntities.empty());

        const EditorBridgeRequestUVE selectLockedRequest{
            kEditorBridgeProtocolVersionUVE, 10U, lockedSnapshot.revision, EditorBridgeRequestKindUVE::SelectEntity,
            created.createdEntity, std::nullopt, std::nullopt};
        const EditorBridgeResponseUVE selectLocked = bridge.DispatchUVE(selectLockedRequest);
        EXPECT_FALSE(selectLocked.applied);
        EXPECT_EQ(selectLocked.code, "bridge.entity.locked");
        EXPECT_TRUE(selectLocked.snapshot.selectedEntities.empty());

        const EditorBridgeRequestUVE toggleLockedRequest{
            kEditorBridgeProtocolVersionUVE, 11U, selectLocked.snapshot.revision,
            EditorBridgeRequestKindUVE::ToggleEntitySelection, created.createdEntity, std::nullopt, std::nullopt};
        const EditorBridgeResponseUVE toggleLocked = bridge.DispatchUVE(toggleLockedRequest);
        EXPECT_FALSE(toggleLocked.applied);
        EXPECT_EQ(toggleLocked.code, "bridge.entity.locked");
        EXPECT_TRUE(toggleLocked.snapshot.selectedEntities.empty());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorBridgeUVETest, SnapshotUVE_CopiesHierarchyInspectorAndNativePanelSessionState) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_panel_snapshot.uvscene");
        editor.InitUVE();
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyFilterKeepAncestorsUVE, true));
        EditorBridgeUVE bridge(editor);

        const Scene::EntityUVE root = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube);
        ASSERT_NE(root, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Bridge Root"));
        const Scene::EntityUVE child = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Plane);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        // No manual reparent needed anymore: creating the Plane while the Cube was selected
        // already parented the child under it (selected-or-root creation parenting).
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Bridge Child"));
        auto& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(
            child, Scene::MeshComponentUVE{Asset::AssetGuidUVE{0x1111U}, Asset::AssetGuidUVE{0x2222U}});

        EditorBridgeSnapshotUVE snapshot = bridge.GetSnapshotUVE();
        // The hierarchy leads with the document's ever-present Object, then the level's Viewport
        // and its object folder; the authored pair sits inside that folder.
        const Scene::EntityUVE rootObject = editor.GetDocumentObjectUVE();
        ASSERT_EQ(snapshot.hierarchy.entries.size(), 5U);
        EXPECT_EQ(snapshot.hierarchy.entries[0].entity,
                  (EditorBridgeEntityRefUVE{rootObject.index, rootObject.generation}));
        EXPECT_EQ(snapshot.hierarchy.entries[0].displayLabel, "Object");
        EXPECT_EQ(snapshot.hierarchy.entries[0].depth, 0U);
        EXPECT_EQ(snapshot.hierarchy.entries[0].childCount, 1U);
        EXPECT_EQ(snapshot.hierarchy.entries[1].displayLabel, "uve_editor_bridge_panel_snapshot"); // the open level's asset name
        EXPECT_EQ(snapshot.hierarchy.entries[2].displayLabel, "World");
        EXPECT_EQ(snapshot.hierarchy.entries[2].depth, 2U);
        EXPECT_EQ(snapshot.hierarchy.entries[3].entity, (EditorBridgeEntityRefUVE{root.index, root.generation}));
        ASSERT_TRUE(snapshot.hierarchy.entries[3].parent.has_value());
        EXPECT_EQ(*snapshot.hierarchy.entries[3].parent, snapshot.hierarchy.entries[2].entity);
        EXPECT_EQ(snapshot.hierarchy.entries[3].displayLabel, "Bridge Root");
        EXPECT_EQ(snapshot.hierarchy.entries[3].depth, 3U);
        EXPECT_EQ(snapshot.hierarchy.entries[3].childCount, 1U);
        EXPECT_EQ(snapshot.hierarchy.entries[4].entity, (EditorBridgeEntityRefUVE{child.index, child.generation}));
        ASSERT_TRUE(snapshot.hierarchy.entries[4].parent.has_value());
        EXPECT_EQ(*snapshot.hierarchy.entries[4].parent, (EditorBridgeEntityRefUVE{root.index, root.generation}));
        EXPECT_EQ(snapshot.hierarchy.entries[4].depth, 4U);
        ASSERT_EQ(snapshot.inspector.mode, EditorBridgeInspectorModeUVE::SingleSelection);
        ASSERT_TRUE(snapshot.inspector.activeEntity.has_value());
        EXPECT_EQ(snapshot.inspector.activeEntity->displayLabel, "Bridge Child");
        ASSERT_TRUE(snapshot.inspector.parent.has_value());
        EXPECT_EQ(snapshot.inspector.parent->displayLabel, "Bridge Root");
        // A PlaneMesh3D is a SurfaceInstance3D: its own sections (the collision it was created with
        // is drawn inside the primitive's section), its bases, Object3D, then the Object section.
        const std::vector<std::string> objectSection{"process",           "physics-interpolation", "auto-translate",
                                                   "editor-description", "script",                "object-metadata"};
        std::vector<std::string> expectedDrawers{"mesh", "primitive-mesh", "surface-instance", "render-instance",
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
        EXPECT_FALSE(snapshot.contentBrowser.initialized);
        EXPECT_EQ(snapshot.viewportSurface.state, EditorBridgeViewportSurfaceStateUVE::Unavailable);
        EXPECT_EQ(snapshot.viewportSurface.generation, 0U);
        EXPECT_EQ(snapshot.viewportSurface.width, 0U);
        EXPECT_EQ(snapshot.viewportSurface.height, 0U);
        EXPECT_TRUE(snapshot.viewportSurface.nativeRendererOwnsSurface);
        EXPECT_FALSE(snapshot.viewportSurface.managedAttachAllowed);

        EditorBridgeRequestUVE surfaceRequest{};
        surfaceRequest.protocolVersion = kEditorBridgeProtocolVersionUVE;
        surfaceRequest.requestId = 9U;
        surfaceRequest.expectedRevision = 0U;
        surfaceRequest.kind = EditorBridgeRequestKindUVE::ReadViewportSurface;
        const EditorBridgeResponseUVE surfaceResponse = bridge.DispatchUVE(surfaceRequest);
        EXPECT_FALSE(surfaceResponse.applied);
        EXPECT_EQ(surfaceResponse.code, "bridge.viewport_surface.unavailable");
        EXPECT_EQ(surfaceResponse.snapshot.viewportSurface, snapshot.viewportSurface);

        EditorBridgeRequestUVE filterRequest{};
        filterRequest.protocolVersion = kEditorBridgeProtocolVersionUVE;
        filterRequest.requestId = 10U;
        filterRequest.expectedRevision = snapshot.revision;
        filterRequest.kind = EditorBridgeRequestKindUVE::SetHierarchyFilter;
        filterRequest.hierarchyFilter = "child";
        const EditorBridgeResponseUVE filtered = bridge.DispatchUVE(filterRequest);
        ASSERT_TRUE(filtered.applied);
        EXPECT_TRUE(filtered.snapshot.hierarchy.filterActive);
        // The filter match plus its ancestor chain: Object, the Viewport and its folder above
        // the authored pair.
        ASSERT_EQ(filtered.snapshot.hierarchy.entries.size(), 5U);
        EXPECT_GT(filtered.snapshot.revision, snapshot.revision);

        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyFilterKeepAncestorsUVE, false));
        const EditorBridgeSnapshotUVE flatFiltered = bridge.GetSnapshotUVE();
        EXPECT_GT(flatFiltered.revision, filtered.snapshot.revision);
        ASSERT_EQ(flatFiltered.hierarchy.entries.size(), 1U);
        EXPECT_EQ(flatFiltered.hierarchy.entries.front().entity,
                  (EditorBridgeEntityRefUVE{child.index, child.generation}));
        EXPECT_EQ(flatFiltered.hierarchy.entries.front().displayLabel, "Bridge Child");
        EXPECT_EQ(flatFiltered.hierarchy.entries.front().depth, 0U);
        ASSERT_TRUE(flatFiltered.hierarchy.entries.front().parent.has_value());
        EXPECT_EQ(*flatFiltered.hierarchy.entries.front().parent,
                  (EditorBridgeEntityRefUVE{root.index, root.generation}));

        EditorBridgeRequestUVE toggleRequest{};
        toggleRequest.protocolVersion = kEditorBridgeProtocolVersionUVE;
        toggleRequest.requestId = 11U;
        toggleRequest.expectedRevision = flatFiltered.revision;
        toggleRequest.kind = EditorBridgeRequestKindUVE::ToggleEntitySelection;
        toggleRequest.entity = EditorBridgeEntityRefUVE{root.index, root.generation};
        const EditorBridgeResponseUVE toggled = bridge.DispatchUVE(toggleRequest);
        ASSERT_TRUE(toggled.applied);
        EXPECT_EQ(toggled.snapshot.inspector.mode, EditorBridgeInspectorModeUVE::MultiSelection);
        EXPECT_FALSE(toggled.snapshot.inspector.canEditSelectedName);

        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyFilterKeepAncestorsUVE, true));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorBridgeUVETest, SnapshotUVE_HierarchySortFollowsTheViewWithoutChangingSceneSiblingOrder) {
    Core::EngineConfigUVE config = MakeBridgeTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_hierarchy_sort.uvscene");
        editor.InitUVE();
        using Kind = Scene::Objects::SceneObjectKindUVE;
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kNewObjectsUnderSelectionUVE, true));
        ASSERT_TRUE(editor.SetEditorSettingUVE(
            EditorSettingIdUVE::kHierarchySortModeUVE, static_cast<std::int64_t>(HierarchySortModeUVE::SceneOrder)));
        const Scene::EntityUVE parent = editor.CreateDocumentSceneObjectUVE(Kind::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Sort Container"));

        const auto createChild = [&editor, parent](const Kind kind, const char* const name) {
            editor.SelectEntityUVE(parent);
            const Scene::EntityUVE child = editor.CreateDocumentSceneObjectUVE(kind);
            if (child != Scene::kInvalidEntityUVE) {
                static_cast<void>(editor.SetSelectedEntityNameUVE(name));
            }
            return child;
        };
        const Scene::EntityUVE zuluCamera = createChild(Kind::Camera3D, "Sortable Zulu");
        const Scene::EntityUVE alphaStatic = createChild(Kind::Static3D, "Sortable Alpha");
        const Scene::EntityUVE charlieCamera = createChild(Kind::Camera3D, "Sortable Charlie");
        const Scene::EntityUVE bravoBox = createChild(Kind::BoxMesh3D, "Sortable Bravo");
        ASSERT_NE(zuluCamera, Scene::kInvalidEntityUVE);
        ASSERT_NE(alphaStatic, Scene::kInvalidEntityUVE);
        ASSERT_NE(charlieCamera, Scene::kInvalidEntityUVE);
        ASSERT_NE(bravoBox, Scene::kInvalidEntityUVE);

        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        auto& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        const std::vector<Scene::EntityUVE> authoredOrder = sceneGraph.GetChildrenUVE(entityManager, parent);
        ASSERT_EQ(authoredOrder, (std::vector<Scene::EntityUVE>{zuluCamera, alphaStatic, charlieCamera, bravoBox}));
        const EditorBridgeEntityRefUVE parentRef{parent.index, parent.generation};
        const auto childLabels = [parentRef](const EditorBridgeSnapshotUVE& snapshot) {
            std::vector<std::string> labels;
            for (const EditorBridgeHierarchyEntryUVE& entry : snapshot.hierarchy.entries) {
                if (entry.parent.has_value() && *entry.parent == parentRef) {
                    labels.push_back(entry.displayLabel);
                }
            }
            return labels;
        };
        EditorBridgeUVE bridge(editor);
        EXPECT_EQ(childLabels(bridge.GetSnapshotUVE()),
                  (std::vector<std::string>{"Sortable Zulu", "Sortable Alpha", "Sortable Charlie", "Sortable Bravo"}));

        ASSERT_TRUE(editor.SetEditorSettingUVE(
            EditorSettingIdUVE::kHierarchySortModeUVE, static_cast<std::int64_t>(HierarchySortModeUVE::Alphabetical)));
        EXPECT_EQ(childLabels(bridge.GetSnapshotUVE()),
                  (std::vector<std::string>{"Sortable Alpha", "Sortable Bravo", "Sortable Charlie", "Sortable Zulu"}));
        ASSERT_TRUE(editor.SetEditorSettingUVE(
            EditorSettingIdUVE::kHierarchySortModeUVE, static_cast<std::int64_t>(HierarchySortModeUVE::ByType)));
        EXPECT_EQ(childLabels(bridge.GetSnapshotUVE()),
                  (std::vector<std::string>{"Sortable Bravo", "Sortable Charlie", "Sortable Zulu", "Sortable Alpha"}));
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), authoredOrder);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorBridgeUVETest, SnapshotUVE_BoundsCopiedPanelRowsWithoutClaimingDeletion) {
    Core::EngineCoreUVE engine(MakeBridgeTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_bridge_panel_bound.uvscene");
        editor.InitUVE();
        EditorBridgeUVE bridge(editor);
        for (std::size_t index = 0U; index < kEditorBridgeMaximumPanelEntriesUVE + 1U; ++index) {
            ASSERT_NE(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty), Scene::kInvalidEntityUVE);
        }

        const EditorBridgeSnapshotUVE snapshot = bridge.GetSnapshotUVE();
        EXPECT_EQ(snapshot.hierarchy.entries.size(), kEditorBridgeMaximumPanelEntriesUVE);
        EXPECT_TRUE(snapshot.hierarchy.truncated);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
