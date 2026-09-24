// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "editor_icon_set_uve.h"

#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/nodes/3d/decal_3d_uve.h"
#include "uve/nodes/3d/fog_volume_3d_uve.h"
#include "uve/nodes/3d/marker_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/nodes/3d/spawn_point_3d_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor::Tests {

struct EditorUVEAccessUVE final {
    [[nodiscard]] static std::string GetOutlinerTypeTagUVE(const EditorUVE& editor,
                                                            const Scene::EntityUVE entity) {
        return editor.GetOutlinerTypeTagUVE(entity);
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> GetDocumentAncestryUVE(const EditorUVE& editor,
                                                                                const Scene::EntityUVE entity) {
        return editor.GetDocumentAncestryUVE(entity);
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> GetEligibleReparentParentsUVE(EditorUVE& editor,
                                                                                        const Scene::EntityUVE entity) {
        return editor.GetEligibleReparentParentsUVE(entity);
    }

    [[nodiscard]] static bool HasInspectorDrawerUVE(const EditorUVE& editor, const std::string_view id) {
        return editor.m_inspectorDrawerRegistry.HasDrawerUVE(id);
    }

    [[nodiscard]] static std::filesystem::path GetImportedModelPathUVE(const EditorUVE& editor,
                                                                        const std::filesystem::path& source) {
        return editor.GetImportedModelPathUVE(source);
    }
    [[nodiscard]] static const EditorModelSourceInfoUVE* FindModelSourceInfoUVE(const EditorUVE& editor,
                                                                                 const std::filesystem::path& source) {
        return editor.FindModelSourceInfoUVE(source);
    }

    [[nodiscard]] static bool IsModelImportQueuedUVE(const EditorUVE& editor, const std::string& source) {
        return editor.m_modelImportJobs.contains(source);
    }

    [[nodiscard]] static bool IsRiggedModelSourceUVE(const EditorUVE& editor, const std::filesystem::path& source) {
        return editor.IsRiggedModelSourceUVE(source);
    }
    [[nodiscard]] static bool BindSelectedSkeletonSourceUVE(EditorUVE& editor, const std::filesystem::path& source) {
        return editor.BindSelectedSkeletonSourceUVE(source);
    }
    [[nodiscard]] static bool SetSelectedComponentPropertyUVE(EditorUVE& editor, const Core::TypeMetadataEntryUVE& entry,
                                                              const Core::TypeMetadataPropertyUVE& property,
                                                              const void* value) {
        return editor.SetSelectedComponentPropertyUVE(entry, property, value);
    }
    [[nodiscard]] static bool PreviewSelectedComponentPropertyUVE(EditorUVE& editor,
                                                                  const Core::TypeMetadataEntryUVE& entry,
                                                                  const Core::TypeMetadataPropertyUVE& property,
                                                                  const void* value) {
        return editor.PreviewSelectedComponentPropertyUVE(entry, property, value);
    }
    [[nodiscard]] static bool CommitComponentPropertyPreviewUVE(EditorUVE& editor) {
        return editor.CommitComponentPropertyPreviewUVE();
    }
    static bool CancelComponentPropertyPreviewUVE(EditorUVE& editor) {
        return editor.CancelComponentPropertyPreviewUVE();
    }
    [[nodiscard]] static bool CommitComponentPropertyPreviewForUVE(EditorUVE& editor,
                                                                   const Core::TypeMetadataEntryUVE& entry,
                                                                   const Core::TypeMetadataPropertyUVE& property) {
        return editor.CommitComponentPropertyPreviewForUVE(entry, property);
    }
    [[nodiscard]] static std::vector<std::string> GetEligibleInspectorDrawerIdsUVE(const EditorUVE& editor,
                                                                                  const Scene::EntityUVE entity) {
        return editor.m_inspectorDrawerRegistry.GetEligibleDrawerIdsUVE(entity);
    }
    [[nodiscard]] static std::string GetScriptNodeTitleUVE(const EditorUVE& editor) {
        return editor.GetScriptNodeTitleUVE("scene.self", "Scene Node");
    }
    [[nodiscard]] static std::size_t GetInspectorDrawerCountUVE(const EditorUVE& editor) {
        return editor.m_inspectorDrawerRegistry.GetDrawerCountUVE();
    }

    [[nodiscard]] static std::vector<std::string> GetEveryContentBrowserTypeLabelUVE() {
        using Type = EditorUVE::ContentBrowserItemTypeUVE;
        std::vector<std::string> labels;
        for (const Type type : {Type::Folder, Type::Scene, Type::Prefab, Type::Bundle, Type::Mesh, Type::Model,
                                Type::Texture, Type::Shader, Type::Material, Type::Save, Type::Animation, Type::Script, Type::Audio,
                                Type::Font, Type::File}) {
            labels.emplace_back(EditorUVE::GetContentBrowserItemTypeLabelUVE(type));
        }
        return labels;
    }

    [[nodiscard]] static std::string GetContentBrowserItemTypeLabelUVE(const Asset::ProjectFileEntryUVE& entry) {
        return EditorUVE::GetContentBrowserItemTypeLabelUVE(EditorUVE::ClassifyContentBrowserEntryUVE(entry));
    }

    static void SelectContentBrowserMeshFocusUVE(EditorUVE& editor) {
        editor.m_contentBrowserTypeFocus = EditorUVE::ContentBrowserTypeFocusUVE::Mesh;
    }

    static void SelectContentBrowserRegisteredFocusUVE(EditorUVE& editor) {
        editor.m_contentBrowserTypeFocus = EditorUVE::ContentBrowserTypeFocusUVE::Registered;
    }

    [[nodiscard]] static bool DoesContentBrowserEntryMatchFocusUVE(const EditorUVE& editor,
                                                                     const Asset::ProjectFileEntryUVE& entry) {
        return editor.DoesContentBrowserEntryMatchFocusUVE(entry);
    }

    static void SetContentBrowserDirectoryUVE(EditorUVE& editor, std::filesystem::path directory) {
        editor.m_contentBrowserDirectory = std::move(directory);
    }

    [[nodiscard]] static const std::filesystem::path& GetContentBrowserDirectoryUVE(const EditorUVE& editor) {
        return editor.m_contentBrowserDirectory;
    }

    static void SetAssetFilterUVE(EditorUVE& editor, std::string filter) { editor.m_assetFilter = std::move(filter); }

    [[nodiscard]] static std::string GetContentBrowserTypeFocusLabelUVE(const EditorUVE& editor) {
        return EditorUVE::GetContentBrowserFocusLabelUVE(editor.m_contentBrowserTypeFocus);
    }

    static void ReconcileContentBrowserDirectoryUVE(EditorUVE& editor, const Asset::ProjectFileSnapshotUVE& snapshot) {
        editor.ReconcileContentBrowserDirectoryUVE(snapshot);
    }

    static void LoadSessionSettingsUVE(EditorUVE& editor) { editor.LoadSessionSettingsUVE(); }
    [[nodiscard]] static bool SaveSessionSettingsUVE(EditorUVE& editor) { return editor.SaveSessionSettingsUVE(); }
    static void ApplyDefaultLayoutPresetUVE(EditorUVE& editor) {
        editor.ApplyLayoutPresetUVE(EditorUVE::EditorLayoutPresetUVE::Default);
    }
    [[nodiscard]] static bool IsScenePanelVisibleUVE(const EditorUVE& editor) noexcept { return editor.m_scenePanelVisible; }
    [[nodiscard]] static bool IsInspectorPanelVisibleUVE(const EditorUVE& editor) noexcept { return editor.m_inspectorPanelVisible; }
    [[nodiscard]] static bool IsBottomDockVisibleUVE(const EditorUVE& editor) noexcept { return editor.m_bottomDockVisible; }
    [[nodiscard]] static bool IsProjectPathFavoritedUVE(const EditorUVE& editor,
                                                         const std::filesystem::path& relativePath) {
        return editor.IsProjectPathFavoritedUVE(relativePath);
    }
    static void ToggleProjectPathFavoriteUVE(EditorUVE& editor, const std::filesystem::path& relativePath) {
        editor.ToggleProjectPathFavoriteUVE(relativePath);
    }
    [[nodiscard]] static std::uintptr_t GetTextureThumbnailUVE(EditorUVE& editor,
                                                                const std::filesystem::path& relativePath) {
        return editor.GetTextureThumbnailUVE(relativePath);
    }
    [[nodiscard]] static std::size_t GetTextureThumbnailCacheSizeUVE(const EditorUVE& editor) noexcept {
        return editor.m_textureThumbnailCache.size();
    }
    [[nodiscard]] static std::uintptr_t GetMeshThumbnailUVE(EditorUVE& editor,
                                                             const std::filesystem::path& relativePath) {
        return editor.GetMeshThumbnailUVE(relativePath);
    }
    [[nodiscard]] static std::size_t GetMeshThumbnailCacheSizeUVE(const EditorUVE& editor) noexcept {
        return editor.m_meshThumbnailCache.size();
    }

    static void CompileVisualScriptUVE(EditorUVE& editor) { editor.CompileVisualScriptUVE(); }

    [[nodiscard]] static bool IsVisualScriptCompileSuccessfulUVE(const EditorUVE& editor) noexcept {
        return editor.m_scriptCompileSucceeded;
    }

    [[nodiscard]] static std::uint64_t GetLastCompiledVisualScriptGraphRevisionUVE(
        const EditorUVE& editor) noexcept {
        return editor.m_scriptLastCompiledGraphRevision;
    }

    [[nodiscard]] static std::size_t GetVisualScriptCompileInstructionCountUVE(
        const EditorUVE& editor) noexcept {
        return editor.m_scriptCompileInstructionCount;
    }

    [[nodiscard]] static bool IsLibraryWorkspaceActiveUVE(const EditorUVE& editor) noexcept {
        return editor.m_activeWorkspace == EditorUVE::EditorWorkspaceUVE::Library;
    }
    static void ActivateGameWorkspaceUVE(EditorUVE& editor) noexcept {
        editor.m_activeWorkspace = EditorUVE::EditorWorkspaceUVE::Game;
    }
    [[nodiscard]] static bool IsScriptingWorkspaceActiveUVE(const EditorUVE& editor) noexcept {
        return editor.m_activeWorkspace == EditorUVE::EditorWorkspaceUVE::Scripting;
    }

    [[nodiscard]] static Scene::EntityUVE GetActiveVisualScriptBranchOwnerUVE(const EditorUVE& editor) noexcept {
        return editor.m_visualScriptBranches[editor.m_activeVisualScriptBranch].ownerEntity;
    }
};

namespace {

[[nodiscard]] Core::EngineConfigUVE MakeEditorTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_editor_tests.log";
    config.settingsFilePath = "uve_editor_tests_settings.json";
    config.projectSettingsFilePath = "uve_editor_tests_project_settings.json";
    config.inputMapFilePath = "uve_editor_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_tests_saves";
    config.shaderCachePath = "uve_editor_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

void AttachRootUVE(Core::EngineCoreUVE& engine, const Scene::EntityUVE entity,
                   const Scene::TransformComponentUVE& transform) {
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    services.GetSceneGraphUVE().AttachTransformUVE(services.GetEntityManagerUVE(), entity, transform);
}

struct UnregisteredEditorLifecycleComponentUVE final {
    int value = 0;
};

TEST(EditorUVETest, InitUVE_StartsRunningWithEmptyDocumentRootsAndSupportsHeadlessLifecycle) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lifecycle.uvescene");
        editor.InitUVE();

        EXPECT_EQ(editor.GetStateUVE(), EditorStateUVE::Running);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());

        editor.ShutdownUVE();
        EXPECT_EQ(editor.GetStateUVE(), EditorStateUVE::Shutdown);
    }

    engine.Shutdown();
}

TEST(EditorUVETest, VisualScriptBranchesAreEditorOnlyAndPersisted) {
    const std::filesystem::path scenePath = "uve_editor_tests_script_branches.uvescene";
    const std::filesystem::path scriptPath = scenePath.parent_path() /
                                             (scenePath.stem().string() + ".scripting");
    std::error_code error;
    std::filesystem::remove(scriptPath, error);

    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        ASSERT_EQ(editor.GetVisualScriptBranchNamesUVE(), (std::vector<std::string>{"Type 1 Scene"}));

        ASSERT_TRUE(editor.GetVisualScriptCanvasUVE().AddNodeTypeUVE("engine.log", {8.0F, 12.0F}).IsAppliedUVE());
        ASSERT_TRUE(editor.CreateVisualScriptBranchUVE("Type 2 Scene"));
        ASSERT_EQ(editor.GetActiveVisualScriptBranchNameUVE(), "Type 2 Scene");
        EXPECT_TRUE(editor.GetVisualScriptCanvasUVE().GetSnapshotUVE().nodes.empty());
        ASSERT_TRUE(editor.RenameActiveVisualScriptBranchUVE("Boss Scene"));
        ASSERT_TRUE(editor.SelectVisualScriptBranchUVE("Type 1 Scene"));
        EXPECT_EQ(editor.GetVisualScriptCanvasUVE().GetSnapshotUVE().nodes.size(), 1U);
        ASSERT_TRUE(editor.SaveVisualScriptWorkspaceUVE());
        editor.ShutdownUVE();
    }
    {
        EditorUVE restored(engine.GetServicesUVE(), scenePath);
        restored.InitUVE();
        EXPECT_EQ(restored.GetVisualScriptBranchNamesUVE(),
                  (std::vector<std::string>{"Type 1 Scene", "Boss Scene"}));
        ASSERT_TRUE(restored.SelectVisualScriptBranchUVE("Boss Scene"));
        EXPECT_TRUE(restored.GetVisualScriptCanvasUVE().GetSnapshotUVE().nodes.empty());
        restored.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(scriptPath, error);
}

TEST(EditorUVETest, OpenScriptGraphForEntity_NoScriptComponent_ReturnsFalse) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_none.uvescene");
    editor.InitUVE();
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});

    EXPECT_FALSE(editor.OpenScriptGraphForEntityUVE(entity));
    EXPECT_FALSE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));

    editor.ShutdownUVE();
    engine.Shutdown();
}

TEST(EditorUVETest, OpenScriptGraphForEntity_NoBranchYet_CreatesOneNamedFromAssetPath) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_create.uvescene");
    editor.InitUVE();
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(
        entity, Scene::ScriptComponentUVE{"Scripts/Boss.scripting"});

    ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(entity));
    EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
    // The raw path contains '/', which CreateVisualScriptBranchUVE would reject as a branch name -
    // the branch is named for the script itself, and found again by its asset path, not by name.
    EXPECT_EQ(editor.GetActiveVisualScriptBranchNameUVE(), "Boss");
    EXPECT_EQ(EditorUVEAccessUVE::GetActiveVisualScriptBranchOwnerUVE(editor), entity);

    editor.ShutdownUVE();
    engine.Shutdown();
}

TEST(EditorUVETest, OpenScriptGraphForEntity_EmptyScriptAssetPath_NamesBranchFromEntityName) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_empty_path.uvescene");
    editor.InitUVE();
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(entity, Scene::NameComponentUVE{"Boss"});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(entity, Scene::ScriptComponentUVE{});

    ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(entity));
    EXPECT_EQ(editor.GetActiveVisualScriptBranchNameUVE(), "Boss Script");

    editor.ShutdownUVE();
    engine.Shutdown();
}

TEST(EditorUVETest, OpenScriptGraphForEntity_NameCollision_DeduplicatesBranchName) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_collision.uvescene");
    editor.InitUVE();
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

    ASSERT_TRUE(editor.CreateVisualScriptBranchUVE("Boss Script"));
    ASSERT_TRUE(editor.SelectVisualScriptBranchUVE("Type 1 Scene"));

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::NameComponentUVE>(entity, Scene::NameComponentUVE{"Boss"});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(entity, Scene::ScriptComponentUVE{});

    ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(entity));
    EXPECT_EQ(editor.GetActiveVisualScriptBranchNameUVE(), "Boss Script (2)");

    editor.ShutdownUVE();
    engine.Shutdown();
}

TEST(EditorUVETest, OpenScriptGraphForEntity_ExistingOwnedBranch_SelectsItWithoutCreatingAnother) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_reopen.uvescene");
    editor.InitUVE();
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(
        entity, Scene::ScriptComponentUVE{"Scripts/Boss.scripting"});

    ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(entity));
    const std::size_t branchCountAfterFirstOpen = editor.GetVisualScriptBranchNamesUVE().size();
    ASSERT_TRUE(editor.GetVisualScriptCanvasUVE().AddNodeTypeUVE("engine.log", {4.0F, 4.0F}).IsAppliedUVE());

    ASSERT_TRUE(editor.SelectVisualScriptBranchUVE("Type 1 Scene"));
    ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(entity));

    EXPECT_EQ(editor.GetVisualScriptBranchNamesUVE().size(), branchCountAfterFirstOpen);
    EXPECT_EQ(editor.GetActiveVisualScriptBranchNameUVE(), "Boss");
    EXPECT_EQ(editor.GetVisualScriptCanvasUVE().GetSnapshotUVE().nodes.size(), 1U);

    editor.ShutdownUVE();
    engine.Shutdown();
}

TEST(EditorUVETest, InitUVE_DoesNotCreateAutomaticPreviewLighting) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_no_preview_light.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        std::size_t lightCount = 0U;
        entityManager.ForEachUVE<Scene::LightComponentUVE>(
            [&lightCount](Scene::EntityUVE, Scene::LightComponentUVE&) { ++lightCount; });
        EXPECT_EQ(lightCount, 0U);
        std::size_t meshCount = 0U;
        entityManager.ForEachUVE<Scene::MeshComponentUVE>(
            [&meshCount](Scene::EntityUVE, Scene::MeshComponentUVE&) { ++meshCount; });
        entityManager.ForEachUVE<Scene::PrimitiveMeshComponentUVE>(
            [&meshCount](Scene::EntityUVE, Scene::PrimitiveMeshComponentUVE&) { ++meshCount; });
        EXPECT_EQ(meshCount, 0U);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.TickUVE();
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        lightCount = 0U;
        entityManager.ForEachUVE<Scene::LightComponentUVE>(
            [&lightCount](Scene::EntityUVE, Scene::LightComponentUVE&) { ++lightCount; });
        EXPECT_EQ(lightCount, 0U);
        meshCount = 0U;
        entityManager.ForEachUVE<Scene::MeshComponentUVE>(
            [&meshCount](Scene::EntityUVE, Scene::MeshComponentUVE&) { ++meshCount; });
        entityManager.ForEachUVE<Scene::PrimitiveMeshComponentUVE>(
            [&meshCount](Scene::EntityUVE, Scene::PrimitiveMeshComponentUVE&) { ++meshCount; });
        EXPECT_EQ(meshCount, 0U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, TwoDCanvasStateUVE_IsEditorOnlyAndValidated) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_2d_canvas.uvescene");
        editor.InitUVE();
        const Editor2DCanvasStateUVE initial = editor.Get2DCanvasStateUVE();
        EXPECT_FLOAT_EQ(initial.zoom, 0.36F);
        EXPECT_FLOAT_EQ(initial.pan.x, 0.0F);
        EXPECT_FLOAT_EQ(initial.pan.y, 0.0F);
        EXPECT_TRUE(initial.gridVisible);
        EXPECT_TRUE(initial.safeAreaVisible);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        EXPECT_TRUE(editor.Set2DCanvasZoomUVE(1.25F));
        EXPECT_FLOAT_EQ(editor.Get2DCanvasStateUVE().zoom, 1.25F);
        EXPECT_FALSE(editor.Set2DCanvasZoomUVE(0.0F));
        EXPECT_FALSE(editor.Set2DCanvasZoomUVE(5.0F));
        EXPECT_FALSE(editor.Set2DCanvasZoomUVE(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FLOAT_EQ(editor.Get2DCanvasStateUVE().zoom, 1.25F);

        editor.Reset2DCanvasViewUVE();
        const Editor2DCanvasStateUVE reset = editor.Get2DCanvasStateUVE();
        EXPECT_FLOAT_EQ(reset.zoom, 0.36F);
        EXPECT_FLOAT_EQ(reset.pan.x, 0.0F);
        EXPECT_FLOAT_EQ(reset.pan.y, 0.0F);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, RenderOverlayUVE_HeadlessWorkspaceCompositionDoesNotMutateEditorState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_layout.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        const Scene::EntityUVE root = services.GetEntityManagerUVE().CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        editor.RenderOverlayUVE();

        EXPECT_EQ(editor.GetStateUVE(), EditorStateUVE::Running);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), root);
        ASSERT_EQ(editor.GetSelectedEntitiesUVE().size(), 1U);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE().front(), root);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, InspectorDrawerRegistrationUVE_IncludesStableHierarchyDrawer) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_drawer_registration.uvescene");
        // 27, not the original 22: every one of the twenty-two ids below still registers, and
        // five sections were added - Visibility, which never had one, plus the four the common
        // Node section gained (Process, Thread Group, Auto Translate, Metadata). None of those
        // five needed a line of Inspector code: the drawers are generated from what each
        // component declares (RegisterMetadataInspectorDrawersUVE), so this count follows the
        // declarations rather than a hand-written registration list.
        // 27 before the three abstract 3D bases, each of which brings one section; 30 before the
        // old Name and Hierarchy drawers were removed and SurfaceInstance3D, LightEmitter3D,
        // Decal3D and FogVolume3D each brought one.
        // 33 with Skeleton3D's own section; 34 with SolidBody3D's.
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorDrawerCountUVE(editor), 35U);
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "solid-body"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "animation-tree"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "skeleton-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "surface-instance"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "light-emitter"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "decal-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "fog-volume-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "bone-modifier"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "physics-object"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "render-instance"));
        EXPECT_FALSE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "name"));
        EXPECT_FALSE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "hierarchy"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "transform"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "primitive-mesh"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "camera"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "mesh"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "light"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "collider"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "rigid-body"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "audio-source"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "particle-emitter"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "script"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "animation-player"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "world-environment"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "character-controller"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "prefab-instance"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "canvas"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "ui-text"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "ui-image"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "ui-button"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "physics-interpolation"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "editor-description"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "visibility"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "process"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "thread-group"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "auto-translate"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "node-metadata"));
        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, WorldEnvironmentComponentUVE_AttachEditUndoRedoThroughEditorPath) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_world_environment.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        ASSERT_NE(entity, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity));

        Scene::WorldEnvironment3DNodeComponentUVE environment;
        environment.skyAssetPath = "environments/sunset.uvesky";
        environment.ambientColor = Math::Vector3UVE{0.15F, 0.25F, 0.40F};
        environment.ambientEnergy = 1.75F;
        environment.exposure = 1.25F;
        environment.fogColor = Math::Vector3UVE{0.30F, 0.35F, 0.45F};
        environment.fogDensity = 0.02F;
        environment.fogEnabled = true;
        environment.postProcessingEnabled = true;
        ASSERT_TRUE(Scene::IsWorldEnvironment3DNodeComponentValidUVE(environment));
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::WorldEnvironment, environment));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity).skyAssetPath,
                  environment.skyAssetPath);
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity).ambientEnergy,
                        environment.ambientEnergy);
        EXPECT_TRUE(editor.CanUndoUVE());

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity));
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity).fogEnabled,
                  environment.fogEnabled);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, OutlinerContextUVE_UsesFixedSpecializedTagPriority) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outliner_tags.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE plain = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, plain, Scene::TransformComponentUVE{});
        EXPECT_TRUE(EditorUVEAccessUVE::GetOutlinerTypeTagUVE(editor, plain).empty());

        const Scene::EntityUVE collider = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, collider, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(collider, Scene::ColliderComponentUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetOutlinerTypeTagUVE(editor, collider), "Collision Box");

        const Scene::EntityUVE light = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, light, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(light, Scene::LightComponentUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetOutlinerTypeTagUVE(editor, light), "Directional Light");

        const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, camera, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera, Scene::CameraComponentUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetOutlinerTypeTagUVE(editor, camera), "Camera");

        const Scene::EntityUVE primitive = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, primitive, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
            primitive, Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Plane, {0.4F, 0.5F, 0.6F}});
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(primitive, Scene::ColliderComponentUVE{});
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(primitive, Scene::CameraComponentUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetOutlinerTypeTagUVE(editor, primitive), "Plane");

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, OutlinerContextUVE_AncestryAndEligibleParentsExcludeSelectedSubtree) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outliner_hierarchy_context.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE rootA = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, rootA, Scene::TransformComponentUVE{});
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE selected = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, selected, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, selected, parent);
        const Scene::EntityUVE descendant = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, descendant, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, descendant, selected);
        const Scene::EntityUVE rootB = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, rootB, Scene::TransformComponentUVE{});

        EXPECT_EQ(EditorUVEAccessUVE::GetDocumentAncestryUVE(editor, selected),
                  (std::vector<Scene::EntityUVE>{parent, selected}));
        // The scene root leads, then the stray top-level entities in creation order.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleReparentParentsUVE(editor, selected),
                  (std::vector<Scene::EntityUVE>{editor.GetDocumentSceneRootUVE(), rootA, parent,
                                                 rootB}));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ContentBrowserWorkflowUVE_UsesPrimaryExtensionTagAndIndependentRegisteredFocus) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_tags.uvescene");
        Asset::ProjectFileEntryUVE registeredMesh;
        registeredMesh.relativePath = "Characters/Hero.UVEMODEL";
        registeredMesh.kind = Asset::ProjectFileEntryKindUVE::File;
        registeredMesh.registeredAssetGuid = Asset::AssetGuidUVE{42U};

        Asset::ProjectFileEntryUVE ordinaryFile;
        ordinaryFile.relativePath = "Notes/readme.txt";
        ordinaryFile.kind = Asset::ProjectFileEntryKindUVE::File;

        Asset::ProjectFileEntryUVE directory;
        directory.relativePath = "Characters";
        directory.kind = Asset::ProjectFileEntryKindUVE::Directory;

        // A registered file keeps its semantic extension tag; registration remains an independent badge/focus.
        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserItemTypeLabelUVE(registeredMesh), "Mesh");
        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserItemTypeLabelUVE(ordinaryFile), "File");
        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserItemTypeLabelUVE(directory), "Folder");

        EditorUVEAccessUVE::SelectContentBrowserMeshFocusUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::DoesContentBrowserEntryMatchFocusUVE(editor, registeredMesh));
        EXPECT_FALSE(EditorUVEAccessUVE::DoesContentBrowserEntryMatchFocusUVE(editor, ordinaryFile));

        EditorUVEAccessUVE::SelectContentBrowserRegisteredFocusUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::DoesContentBrowserEntryMatchFocusUVE(editor, registeredMesh));
        EXPECT_FALSE(EditorUVEAccessUVE::DoesContentBrowserEntryMatchFocusUVE(editor, ordinaryFile));
        EXPECT_FALSE(EditorUVEAccessUVE::DoesContentBrowserEntryMatchFocusUVE(editor, directory));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ContentBrowserWorkflowUVE_ScriptsAudioAndFontsAreTheirOwnTypesAndEveryTypeHasAnIcon) {
    const auto labelOf = [](const char* const path) {
        Asset::ProjectFileEntryUVE entry;
        entry.relativePath = path;
        entry.kind = Asset::ProjectFileEntryKindUVE::File;
        return EditorUVEAccessUVE::GetContentBrowserItemTypeLabelUVE(entry);
    };
    EXPECT_EQ(labelOf("Scripts/player.uvescript"), "Script");
    EXPECT_EQ(labelOf("Audio/step.uveaudio"), "Audio");
    EXPECT_EQ(labelOf("Audio/Step.WAV"), "Audio");
    EXPECT_EQ(labelOf("Fonts/title.ttf"), "Font");
    EXPECT_EQ(labelOf("Fonts/body.otf"), "Font");
    EXPECT_EQ(labelOf("Notes/readme.txt"), "File");
    // FBX is a model source like glTF and OBJ: shown as a mesh and imported behind the scenes.
    EXPECT_EQ(labelOf("Characters/hero.fbx"), "Mesh");
    EXPECT_EQ(labelOf("Characters/Hero.FBX"), "Mesh");

    // Each type shows its own picture in the browser, found by the label it is shown with.
    std::set<const EditorIconSourceUVE*> icons;
    for (const std::string& label : EditorUVEAccessUVE::GetEveryContentBrowserTypeLabelUVE()) {
        const EditorIconSourceUVE* const icon = FindEditorIconSourceUVE(EditorIconGroupUVE::ContentType, label);
        EXPECT_NE(icon, nullptr) << "no icon for content type " << label;
        icons.insert(icon);
    }
    EXPECT_EQ(icons.size(), EditorUVEAccessUVE::GetEveryContentBrowserTypeLabelUVE().size());
}

TEST(EditorUVETest, ContentBrowserWorkflowUVE_PersistsFiltersAndSafelyFallsBackWhenFolderDisappears) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_navigation.uvescene");
        Asset::ProjectFileSnapshotUVE snapshot;
        snapshot.entries.push_back(
            Asset::ProjectFileEntryUVE{std::filesystem::path{"Scenes"}, Asset::ProjectFileEntryKindUVE::Directory, std::nullopt});
        snapshot.entries.push_back(Asset::ProjectFileEntryUVE{std::filesystem::path{"Scenes/City.uvescene"},
                                                               Asset::ProjectFileEntryKindUVE::File, std::nullopt});

        EditorUVEAccessUVE::SetAssetFilterUVE(editor, "city");
        EditorUVEAccessUVE::SelectContentBrowserMeshFocusUVE(editor);
        EditorUVEAccessUVE::SetContentBrowserDirectoryUVE(editor, "Scenes");
        EditorUVEAccessUVE::ReconcileContentBrowserDirectoryUVE(editor, snapshot);

        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserDirectoryUVE(editor), std::filesystem::path{"Scenes"});
        EXPECT_EQ(editor.GetAssetFilterUVE(), "city");
        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserTypeFocusLabelUVE(editor), "Mesh");

        snapshot.entries.clear();
        EditorUVEAccessUVE::ReconcileContentBrowserDirectoryUVE(editor, snapshot);
        EXPECT_TRUE(EditorUVEAccessUVE::GetContentBrowserDirectoryUVE(editor).empty());
        EXPECT_EQ(editor.GetAssetFilterUVE(), "city");
        EXPECT_EQ(EditorUVEAccessUVE::GetContentBrowserTypeFocusLabelUVE(editor), "Mesh");

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ContentBrowserAutoRefreshUVE_RefreshesAfterEngineWatcherSequenceAndAcknowledgesIt) {
    const std::filesystem::path root = "uve_editor_tests_auto_refresh_content";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Scenes");
    {
        std::ofstream initialFile(root / "Scenes" / "Initial.txt", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(initialFile.is_open());
        initialFile << "initial";
        ASSERT_TRUE(initialFile.good());
    }

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    config.projectChangeWatchPollIntervalSecondsUVE = 0.0;
    config.projectChangeJournalCapacityUVE = 16U;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_auto_refresh.uvescene");
        editor.InitUVE();

        engine.TickFrameUVE();
        editor.TickUVE();
        Asset::IProjectFileIndexUVE& projectFileIndex = engine.GetServicesUVE().GetProjectFileIndexUVE();
        Asset::ProjectFileSnapshotUVE initialSnapshot = projectFileIndex.GetSnapshotUVE();
        ASSERT_EQ(initialSnapshot.refreshGeneration, 1U);
        ASSERT_TRUE(std::any_of(initialSnapshot.entries.begin(), initialSnapshot.entries.end(),
                                [](const Asset::ProjectFileEntryUVE& entry) {
                                    return entry.relativePath == std::filesystem::path{"Scenes/Initial.txt"};
                                }));
        EXPECT_TRUE(engine.GetServicesUVE().GetProjectChangeWatcherUVE().GetSnapshotUVE().changes.empty());

        {
            std::ofstream newFile(root / "Scenes" / "AutoRefresh.txt", std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(newFile.is_open());
            newFile << "created after baseline";
            ASSERT_TRUE(newFile.good());
        }

        engine.TickFrameUVE();
        editor.TickUVE();
        const Asset::ProjectFileSnapshotUVE refreshedSnapshot = projectFileIndex.GetSnapshotUVE();
        ASSERT_GT(refreshedSnapshot.refreshGeneration, initialSnapshot.refreshGeneration);
        EXPECT_TRUE(std::any_of(refreshedSnapshot.entries.begin(), refreshedSnapshot.entries.end(),
                                [](const Asset::ProjectFileEntryUVE& entry) {
                                    return entry.relativePath == std::filesystem::path{"Scenes/AutoRefresh.txt"};
                                }));
        const Asset::ProjectChangeSnapshotUVE changeSnapshot =
            engine.GetServicesUVE().GetProjectChangeWatcherUVE().GetSnapshotUVE();
        EXPECT_TRUE(changeSnapshot.changes.empty());
        EXPECT_FALSE(changeSnapshot.rescanRequired);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorUVETest, TextureThumbnailUVE_GracefullyReturnsZeroForMissingCorruptOrUnsupportedFormat) {
    const std::filesystem::path root = "uve_editor_tests_texture_thumbnail_content";
    std::filesystem::remove_all(root);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    {
        std::ofstream corrupt(root / "corrupt.uvetex", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt.is_open());
        corrupt << "not-a-uve-envelope";
    }
    {
        // A structurally valid envelope declaring an unsupported pixel format for thumbnails
        // (RGBA16Float): LoadTextureAssetUVE succeeds, but GetTextureThumbnailUVE must still
        // decline to upload it rather than misinterpreting the byte layout as RGBA8Unorm.
        Asset::TextureAssetUVE unsupported;
        unsupported.width = 1U;
        unsupported.height = 1U;
        unsupported.format = Asset::TextureFormatUVE::RGBA16Float;
        unsupported.pixels.resize(Asset::BytesPerPixelUVE(Asset::TextureFormatUVE::RGBA16Float));
        ASSERT_TRUE(Asset::SaveTextureAssetUVE(unsupported, root / "unsupported.uvetex"));
    }

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_texture_thumbnail.uvescene");
        editor.InitUVE();
        engine.TickFrameUVE();
        editor.TickUVE();

        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "missing.uvetex"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "corrupt.uvetex"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "unsupported.uvetex"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailCacheSizeUVE(editor), 3U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "missing.uvetex"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailCacheSizeUVE(editor), 3U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorUVETest, MeshThumbnailUVE_GracefullyReturnsZeroForMissingCorruptOrEmptyMesh) {
    const std::filesystem::path root = "uve_editor_tests_mesh_thumbnail_content";
    std::filesystem::remove_all(root);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    {
        std::ofstream corrupt(root / "corrupt.uvemodel", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt.is_open());
        corrupt << "not-a-uve-envelope";
    }
    {
        // A structurally valid mesh envelope with no geometry: LoadMeshAssetUVE succeeds, but
        // RenderThumbnailUVE has nothing to draw and must decline rather than issuing an empty
        // draw call.
        const Asset::MeshAssetUVE empty;
        ASSERT_TRUE(Asset::SaveMeshAssetUVE(empty, root / "empty.uvemodel"));
    }

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_mesh_thumbnail.uvescene");
        editor.InitUVE();
        engine.TickFrameUVE();
        editor.TickUVE();

        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "missing.uvemodel"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "corrupt.uvemodel"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "empty.uvemodel"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailCacheSizeUVE(editor), 3U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "missing.uvemodel"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailCacheSizeUVE(editor), 3U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorUVETest, SessionSettingsUVE_MigratesWithoutHiddenWriteAndPreservesDocumentState) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Config::IConfigManagerUVE& settings = services.GetConfigManagerUVE();
        settings.SetIntUVE("editor.sessionSettingsVersion", 0);
        settings.SetBoolUVE("editor.panels.sceneVisible", false);
        settings.SetBoolUVE("editor.panels.inspectorVisible", false);
        settings.SetBoolUVE("editor.panels.bottomDockVisible", false);
        settings.SetBoolUVE("editor.viewport.snap.enabled", true);
        settings.SetDoubleUVE("editor.viewport.snap.translateStep", -1.0);
        settings.SetDoubleUVE("editor.viewport.snap.rotateStepDegrees", 45.0);
        settings.SetDoubleUVE("editor.viewport.snap.scaleStep", 0.25);

        EditorUVE editor(services, "uve_editor_tests_session_settings.uvescene");
        editor.InitUVE();
        EXPECT_FALSE(EditorUVEAccessUVE::IsScenePanelVisibleUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::IsInspectorPanelVisibleUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::IsBottomDockVisibleUVE(editor));
        EXPECT_FALSE(settings.HasKeyUVE("editor.workspace.active"));
        const EditorTransformSnappingSettingsUVE& snapping = editor.GetTransformSnappingSettingsUVE();
        EXPECT_TRUE(snapping.enabled);
        EXPECT_FLOAT_EQ(snapping.translateStep, 1.0F);
        EXPECT_FLOAT_EQ(snapping.rotateStepDegrees, 45.0F);
        EXPECT_FLOAT_EQ(snapping.scaleStep, 0.25F);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());

        EditorUVEAccessUVE::ApplyDefaultLayoutPresetUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsScenePanelVisibleUVE(editor));
        EXPECT_TRUE(EditorUVEAccessUVE::IsInspectorPanelVisibleUVE(editor));
        EXPECT_TRUE(EditorUVEAccessUVE::IsBottomDockVisibleUVE(editor));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());

        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        EXPECT_EQ(settings.GetIntUVE("editor.sessionSettingsVersion", -1), 1);
        EXPECT_TRUE(settings.HasKeyUVE("editor.workspace.active"));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, EditorSettingsUVE_DescriptorDefaultsMatchTheEditorsOwnDefaults) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        // Not initialised, so nothing has been loaded: every value is the editor's in-class default.
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_setting_defaults.uvescene");
        const Config::SettingsRegistryUVE& registry = editor.GetSettingsRegistryUVE();
        // The declared preferences, and a primary and alternate shortcut for every command.
        ASSERT_EQ(registry.GetCountUVE(), 34U + (2U * editor.GetEditorCommandsUVE().size()));
        for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
            const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(descriptor->id);
            ASSERT_TRUE(value.has_value()) << descriptor->id;
            EXPECT_EQ(*value, descriptor->defaultValue) << descriptor->id;
        }
    }
    engine.Shutdown();
}

TEST(EditorUVETest, EditorSettingsUVE_SetAppliesAtOnceAndRefusesWhatItsDescriptorForbids) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_setting_set.uvescene");
        editor.InitUVE();
        namespace Id = EditorSettingIdUVE;
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kGridOpacityUVE, 0.5));
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacityUVE(), 0.5F);
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kSelectionOutlineColorUVE, Config::SettingColorUVE{0.0F, 1.0F, 0.0F}));
        EXPECT_FLOAT_EQ(editor.GetViewportSelectionOutlineColorUVE().g, 1.0F);
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kSnapRotateStepDegreesUVE, 45.0));
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().rotateStepDegrees, 45.0F);

        // Out of range, the wrong type, and an unknown id change nothing.
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kGridOpacityUVE, 0.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kGridOpacityUVE, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kGridOpacityUVE, true));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kSnapRotateStepDegreesUVE, 720.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE("editor.unknown", true));
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacityUVE(), 0.5F);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().rotateStepDegrees, 45.0F);

        // A change made elsewhere (the menu) is what the setting reads back.
        ASSERT_TRUE(editor.SetViewportGridCellSizeUVE(5.0F));
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kGridCellSizeUVE), Config::SettingValueUVE{5.0});
        EXPECT_FALSE(editor.GetEditorSettingUVE("editor.unknown").has_value());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ProjectSettingsUVE_ChangesAreSavedAndTheNextSessionRunsWithThem) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.projectSettingsFilePath);
    {
        Core::EngineCoreUVE engine(config);
        engine.Init();
        ASSERT_TRUE(engine.Load());
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_project_settings.uvescene");
        editor.InitUVE();
        Config::SettingsDocumentUVE& project = engine.GetServicesUVE().GetProjectSettingsUVE();
        ASSERT_TRUE(project.SetValueUVE(Core::EngineProjectSettingIdUVE::kPhysicsTicksPerSecondUVE, 120.0));
        EXPECT_TRUE(project.IsDirtyUVE());
        // A setting left at its default never reaches the file.
        ASSERT_TRUE(project.SetValueUVE(Core::EngineProjectSettingIdUVE::kShadowFilterUVE, std::int64_t{1}));
        editor.ShutdownUVE();
        EXPECT_FALSE(project.IsDirtyUVE());
        engine.Shutdown();
    }
    ASSERT_TRUE(std::filesystem::exists(config.projectSettingsFilePath));
    {
        Core::EngineCoreUVE engine(config);
        engine.Init();
        EXPECT_DOUBLE_EQ(engine.GetConfigUVE().fixedUpdateFps, 120.0);
        EXPECT_FALSE(engine.GetServicesUVE().GetProjectSettingsUVE().GetStoredValueUVE(
                         Core::EngineProjectSettingIdUVE::kShadowFilterUVE)
                         .has_value());
        engine.Shutdown();
    }
    std::filesystem::remove(config.projectSettingsFilePath);
}

TEST(EditorUVETest, NewNodeDefaultsUVE_ParentAndPlacementFollowThePreferences) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_new_node_defaults.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entities = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& graph = engine.GetServicesUVE().GetSceneGraphUVE();
        const auto parentOf = [&entities](const Scene::EntityUVE entity) {
            return entities.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
        };
        const auto localPositionOf = [&entities](const Scene::EntityUVE entity) {
            return entities.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
        };
        namespace Id = EditorSettingIdUVE;

        // By default: under the selection, at its origin, whatever the camera looks at.
        editor.SetViewportCameraFocusUVE(Math::Vector3UVE{3.0F, 0.0F, -2.0F});
        const Scene::EntityUVE parent = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        const Scene::EntityUVE root = parentOf(parent);
        const Scene::EntityUVE child = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::BoxMesh3D);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        EXPECT_EQ(parentOf(child), parent);
        EXPECT_FLOAT_EQ(localPositionOf(child).x, 0.0F);

        // Not under the selection: straight under the scene root.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewNodesUnderSelectionUVE, false));
        editor.SelectEntityUVE(parent);
        const Scene::EntityUVE sibling = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        EXPECT_EQ(parentOf(sibling), root);

        // At the view focus: under the root, that point itself...
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewNodePlacementUVE,
                                               static_cast<std::int64_t>(EditorNewNodePlacementUVE::ViewFocus)));
        const Scene::EntityUVE focused = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        EXPECT_FLOAT_EQ(localPositionOf(focused).x, 3.0F);
        EXPECT_FLOAT_EQ(localPositionOf(focused).z, -2.0F);

        // ...and under a moved parent, the same point taken into the parent's space.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewNodesUnderSelectionUVE, true));
        Scene::TransformComponentUVE moved = entities.GetComponentUVE<Scene::TransformComponentUVE>(parent);
        moved.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        graph.SetLocalTransformUVE(entities, parent, moved);
        graph.UpdateUVE(entities);
        editor.SelectEntityUVE(parent);
        const Scene::EntityUVE nested = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        EXPECT_EQ(parentOf(nested), parent);
        EXPECT_FLOAT_EQ(localPositionOf(nested).x, 2.0F);
        EXPECT_FLOAT_EQ(localPositionOf(nested).z, -2.0F);

        // Undo and redo keep the place.
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE redone = editor.GetSelectedEntityUVE();
        ASSERT_NE(redone, Scene::kInvalidEntityUVE);
        EXPECT_FLOAT_EQ(localPositionOf(redone).x, 2.0F);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, PlayModePreferencesUVE_PauseSaveAndStayInTheTab) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_play_prefs.uvescene";
    std::filesystem::remove(scenePath);
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath, 100U, &engine);
        editor.InitUVE();
        namespace Id = EditorSettingIdUVE;

        // By default: running, in the Game tab, and the tab comes back at Stop.
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        EXPECT_FALSE(EditorUVEAccessUVE::IsLibraryWorkspaceActiveUVE(editor));
        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_TRUE(EditorUVEAccessUVE::IsLibraryWorkspaceActiveUVE(editor));

        // Paused on start, in the tab it was started from, with the dirty scene saved first.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kPlayPauseOnStartUVE, true));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kPlaySwitchToGameUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kPlaySaveSceneFirstUVE, true));
        ASSERT_NE(editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D), Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Paused);
        EXPECT_TRUE(EditorUVEAccessUVE::IsLibraryWorkspaceActiveUVE(editor));
        EXPECT_TRUE(std::filesystem::exists(scenePath));
        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorUVETest, NodeTypesUVE_EveryNodeIsTypedAndKeepsItThroughDuplicateUndoAndSave) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_node_types.uvescene";
    std::filesystem::remove(scenePath);
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        EXPECT_EQ(editor.GetNodeTypeNameUVE(root), "SceneRoot");

        // Every node the Add Node list makes reads back as the type it was made as, including the
        // ones whose components alone could not say (StaticBody3D is built like a Collider3D).
        for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : Scene::Nodes::GetSceneNodeDescriptorsUVE()) {
            if (!descriptor.libraryCreatable) {
                continue;
            }
            editor.SelectEntityUVE(root);
            const Scene::EntityUVE node = editor.CreateDocumentSceneNodeUVE(descriptor.kind);
            ASSERT_NE(node, Scene::kInvalidEntityUVE) << descriptor.displayName;
            EXPECT_EQ(editor.GetNodeTypeNameUVE(node), descriptor.displayName);
        }

        // The legacy creation commands type their nodes as the same kinds.
        editor.SelectEntityUVE(root);
        EXPECT_EQ(editor.GetNodeTypeNameUVE(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube)), "BoxMesh3D");

        // Duplicate, and undo then redo of a creation, carry the type with the node.
        editor.SelectEntityUVE(root);
        const Scene::EntityUVE body = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::StaticBody3D);
        ASSERT_NE(body, Scene::kInvalidEntityUVE);
        editor.SelectEntityUVE(body);
        const Scene::EntityUVE copy = editor.DuplicateSelectedEntityUVE();
        EXPECT_EQ(editor.GetNodeTypeNameUVE(copy), "StaticBody3D");
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(editor.GetNodeTypeNameUVE(editor.GetSelectedEntityUVE()), "StaticBody3D");

        ASSERT_TRUE(editor.SaveSceneUVE());
        ASSERT_TRUE(editor.LoadSceneUVE());
        std::size_t staticBodies = 0U;
        entityManager.ForEachUVE<Scene::NameComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NameComponentUVE& name) {
                if (name.name.starts_with("StaticBody3D")) {
                    ++staticBodies;
                    EXPECT_EQ(editor.GetNodeTypeNameUVE(entity), "StaticBody3D");
                }
            });
        EXPECT_EQ(staticBodies, 2U); // the one from the list, and the one brought back by redo
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorUVETest, SiblingOrderUVE_MoveDuplicateAndDeleteKeepPlacesThroughUndo) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_sibling_order.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        std::vector<Scene::EntityUVE> nodes;
        for (int i = 0; i < 3; ++i) {
            editor.SelectEntityUVE(root);
            nodes.push_back(editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D));
            ASSERT_NE(nodes.back(), Scene::kInvalidEntityUVE);
        }
        const auto order = [&] { return sceneGraph.GetChildrenUVE(entityManager, root); };
        ASSERT_EQ(order(), nodes);
        using Move = EditorSiblingMoveUVE;

        // At the ends, the moves toward that end are unavailable; the root never moves.
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(nodes[0], Move::Up));
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(nodes[2], Move::ToBottom));
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(root, Move::Down));

        ASSERT_TRUE(editor.MoveDocumentEntityUVE(nodes[2], Move::Up));
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{nodes[0], nodes[2], nodes[1]}));
        ASSERT_TRUE(editor.MoveDocumentEntityUVE(nodes[0], Move::ToBottom));
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{nodes[2], nodes[1], nodes[0]}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{nodes[0], nodes[2], nodes[1]}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(order(), nodes);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{nodes[0], nodes[2], nodes[1]}));
        ASSERT_TRUE(editor.UndoUVE());

        // A copy lands right below its source, and comes back there on redo.
        editor.SelectEntityUVE(nodes[0]);
        const Scene::EntityUVE copy = editor.DuplicateSelectedEntityUVE();
        ASSERT_NE(copy, Scene::kInvalidEntityUVE);
        EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, copy), std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, editor.GetSelectedEntityUVE()),
                  std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_EQ(order(), nodes);

        // A deleted node comes back in its place, not at the end.
        editor.SelectEntityUVE(nodes[1]);
        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        ASSERT_TRUE(editor.UndoUVE());
        const std::vector<Scene::EntityUVE> after = order();
        ASSERT_EQ(after.size(), 3U);
        EXPECT_EQ(after[0], nodes[0]);
        EXPECT_EQ(after[2], nodes[2]);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyPreferencesUVE_ApplyRefuseWhatIsOutOfRangeAndPersist) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    namespace Id = EditorSettingIdUVE;
    const std::string_view scenePath = "uve_editor_tests_hierarchy_prefs.uvescene";
    const auto expectChosen = [](const HierarchyViewSettingsUVE& view) {
        EXPECT_FALSE(view.revealSelection);
        EXPECT_FALSE(view.showTypeName);
        EXPECT_EQ(view.visibilityColumn, HierarchyVisibilityColumnUVE::OnHover);
        EXPECT_EQ(view.doubleClick, HierarchyDoubleClickUVE::FocusInViewport);
        EXPECT_EQ(view.treeLines, HierarchyTreeLinesUVE::ToEachChild);
        EXPECT_FLOAT_EQ(view.indentWidth, 30.0F);
        EXPECT_TRUE(view.showIcons);
        EXPECT_TRUE(view.dragToReparent);
    };
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyRevealSelectionUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyShowTypeNameUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyVisibilityColumnUVE,
                                               static_cast<std::int64_t>(HierarchyVisibilityColumnUVE::OnHover)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyDoubleClickUVE,
                                               static_cast<std::int64_t>(HierarchyDoubleClickUVE::FocusInViewport)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyTreeLinesUVE,
                                               static_cast<std::int64_t>(HierarchyTreeLinesUVE::ToEachChild)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 30.0));
        expectChosen(editor.GetHierarchyViewSettingsUVE());

        // An indent the tree could not draw, an action that does not exist, or a value of the wrong
        // type changes nothing.
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 2.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 400.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyDoubleClickUVE, std::int64_t{7}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyShowIconsUVE, std::int64_t{1}));
        expectChosen(editor.GetHierarchyViewSettingsUVE());
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        // The next session starts where this one left off.
        EditorUVE reloaded(engine.GetServicesUVE(), scenePath);
        reloaded.InitUVE();
        expectChosen(reloaded.GetHierarchyViewSettingsUVE());
        reloaded.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, EditorCommandsUVE_RunOnlyWhenAvailableAndKeepRebindsAcrossSessions) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_commands.uvescene");
        editor.InitUVE();
        const std::vector<EditorCommandUVE>& commands = editor.GetEditorCommandsUVE();
        // Ids are unique, and every default shortcut is one only its command has.
        for (std::size_t a = 0U; a < commands.size(); ++a) {
            for (std::size_t b = a + 1U; b < commands.size(); ++b) {
                EXPECT_NE(commands[a].id, commands[b].id);
                for (const EditorShortcutUVE& shortcut : commands[a].defaultShortcuts) {
                    if (!shortcut.IsEmptyUVE()) {
                        EXPECT_EQ(std::count(commands[b].defaultShortcuts.begin(), commands[b].defaultShortcuts.end(),
                                             shortcut),
                                  0)
                            << commands[a].id << " and " << commands[b].id;
                    }
                }
            }
        }

        // Undo is unavailable with nothing done; after creating a node it runs.
        EXPECT_FALSE(editor.RunEditorCommandUVE("edit.undo"));
        ASSERT_TRUE(editor.RunEditorCommandUVE("create.empty"));
        EXPECT_TRUE(editor.CanUndoUVE());
        EXPECT_TRUE(editor.RunEditorCommandUVE("edit.undo"));
        EXPECT_FALSE(editor.RunEditorCommandUVE("no.such.command"));

        // A rebind is kept with the preferences; a key a shortcut may not use is refused.
        const std::optional<EditorShortcutUVE> ctrlU = ParseEditorShortcutUVE("Ctrl+U");
        ASSERT_TRUE(ctrlU.has_value());
        ASSERT_TRUE(editor.SetEditorCommandShortcutUVE("edit.undo", 1U, *ctrlU));
        ASSERT_TRUE(editor.SetEditorCommandShortcutUVE("play.step", 0U, EditorShortcutUVE{}));
        EXPECT_FALSE(editor.SetEditorCommandShortcutUVE("edit.undo", 2U, *ctrlU));
        EXPECT_FALSE(editor.SetEditorCommandShortcutUVE("edit.undo", 0U, EditorShortcutUVE{1, false, false, false}));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_commands_reload.uvescene");
        reloaded.InitUVE();
        const auto find = [&reloaded](const std::string_view id) {
            const auto& commands = reloaded.GetEditorCommandsUVE();
            return *std::find_if(commands.begin(), commands.end(), [id](const auto& c) { return c.id == id; });
        };
        EXPECT_EQ(FormatEditorShortcutUVE(find("edit.undo").shortcuts[0]), "Ctrl+Z");
        EXPECT_EQ(FormatEditorShortcutUVE(find("edit.undo").shortcuts[1]), "Ctrl+U");
        EXPECT_TRUE(find("play.step").shortcuts[0].IsEmptyUVE());
        reloaded.ShutdownUVE();
    }
    // A shortcut the file garbled falls back to its default.
    engine.GetServicesUVE().GetConfigManagerUVE().SetStringUVE("editor.shortcuts.edit.undo.primary", "Ctrl+Nope");
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_commands_corrupt.uvescene");
        corrupt.InitUVE();
        const auto& commands = corrupt.GetEditorCommandsUVE();
        const auto undo = std::find_if(commands.begin(), commands.end(), [](const auto& c) { return c.id == "edit.undo"; });
        EXPECT_EQ(FormatEditorShortcutUVE(undo->shortcuts[0]), "Ctrl+Z");
        corrupt.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, SessionSettingsUVE_NeverRestoresTheGameWorkspace) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();

    // Leaving the editor in Game keeps the last workspace a session can reopen into.
    settings.SetIntUVE("editor.workspace.active", 2);
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_game.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        EditorUVEAccessUVE::ActivateGameWorkspaceUVE(editor);
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        EXPECT_EQ(settings.GetIntUVE("editor.workspace.active", -1), 2);
        editor.ShutdownUVE();
    }
    // A file that names Game anyway opens in Library.
    settings.SetIntUVE("editor.workspace.active", 5);
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_game_file.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(EditorUVEAccessUVE::IsLibraryWorkspaceActiveUVE(editor));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportViewUVE_NamedViewsGoOrthographicAutomaticallyUntilOrbited) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_viewport_views.uvescene");
        editor.InitUVE();
        using View = EditorUVE::ViewportViewUVE;
        EXPECT_EQ(editor.GetViewportViewUVE(), View::User);
        EXPECT_FALSE(editor.IsViewportOrthographicUVE());
        const std::uint32_t serialBefore = editor.GetViewportViewRequestSerialUVE();

        // A named view switches to orthographic by itself, and each request bumps the serial -
        // picking the same view again must re-snap a camera that has drifted.
        editor.RequestViewportViewUVE(View::Top);
        EXPECT_EQ(editor.GetViewportViewUVE(), View::Top);
        EXPECT_TRUE(editor.IsViewportOrthographicUVE());
        editor.RequestViewportViewUVE(View::Top);
        EXPECT_EQ(editor.GetViewportViewRequestSerialUVE(), serialBefore + 2U);

        // Orbiting out of it returns to a free perspective view.
        editor.NotifyViewportOrbitedUVE();
        EXPECT_EQ(editor.GetViewportViewUVE(), View::User);
        EXPECT_FALSE(editor.IsViewportOrthographicUVE());

        // An orthographic the author chose sticks through views and orbits.
        editor.SetViewportOrthographicUVE(true);
        editor.RequestViewportViewUVE(View::Front);
        editor.NotifyViewportOrbitedUVE();
        EXPECT_EQ(editor.GetViewportViewUVE(), View::User);
        EXPECT_TRUE(editor.IsViewportOrthographicUVE());

        // Choosing perspective while in a named view keeps the view.
        editor.RequestViewportViewUVE(View::Right);
        editor.SetViewportOrthographicUVE(false);
        EXPECT_EQ(editor.GetViewportViewUVE(), View::Right);
        EXPECT_FALSE(editor.IsViewportOrthographicUVE());

        EXPECT_STREQ(EditorUVE::GetViewportViewNameUVE(View::Back), "Back");

        // Keypad layout: 7/1/3 are Top/Front/Right, Ctrl gives the opposite side; others are not views.
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(7, false), View::Top);
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(7, true), View::Bottom);
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(1, false), View::Front);
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(1, true), View::Back);
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(3, false), View::Right);
        EXPECT_EQ(EditorUVE::GetViewportViewForKeypadDigitUVE(3, true), View::Left);
        EXPECT_FALSE(EditorUVE::GetViewportViewForKeypadDigitUVE(5, false).has_value());
        EXPECT_FALSE(EditorUVE::GetViewportViewForKeypadDigitUVE(0, true).has_value());
        EXPECT_STRNE(EditorUVE::GetViewportViewShortcutUVE(View::Left), "");
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorFoldsUVE_RememberedAcrossSessionReloadAndBounded) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_folds.uvescene");
        editor.InitUVE();
        // An unknown key answers the caller's default, open or closed.
        EXPECT_TRUE(editor.IsInspectorFoldOpenUVE("section:transform", true));
        EXPECT_FALSE(editor.IsInspectorFoldOpenUVE("group:light/Shadow", false));

        editor.SetInspectorFoldOpenUVE("section:transform", false);
        editor.SetInspectorFoldOpenUVE("group:light/Shadow", true);
        editor.SetInspectorFoldOpenUVE("", false); // an empty key is not a fold
        EXPECT_FALSE(editor.IsInspectorFoldOpenUVE("section:transform", true));
        EXPECT_TRUE(editor.IsInspectorFoldOpenUVE("group:light/Shadow", false));
        EXPECT_TRUE(editor.IsInspectorFoldOpenUVE("", true));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_inspector_folds_reload.uvescene");
        reloaded.InitUVE();
        EXPECT_FALSE(reloaded.IsInspectorFoldOpenUVE("section:transform", true));
        EXPECT_TRUE(reloaded.IsInspectorFoldOpenUVE("group:light/Shadow", false));

        // Bounded: past the cap, new keys are not remembered, but existing ones still update.
        for (std::size_t index = 0U; index < EditorUVE::kMaxRememberedInspectorFoldsUVE + 10U; ++index) {
            reloaded.SetInspectorFoldOpenUVE("section:filler-" + std::to_string(index), false);
        }
        EXPECT_TRUE(reloaded.IsInspectorFoldOpenUVE(
            "section:filler-" + std::to_string(EditorUVE::kMaxRememberedInspectorFoldsUVE + 5U), true));
        reloaded.SetInspectorFoldOpenUVE("section:transform", true);
        EXPECT_TRUE(reloaded.IsInspectorFoldOpenUVE("section:transform", false));
        reloaded.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportGridUVE_RefusesBadOpacityAndPersistsAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(editor.IsViewportGridVisibleUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacityUVE(), 1.0F);

        // Out of range, zero (a hidden grid under another name) and NaN are refused whole.
        EXPECT_FALSE(editor.SetViewportGridUVE(false, 1.5F));
        EXPECT_FALSE(editor.SetViewportGridUVE(false, 0.0F));
        EXPECT_FALSE(editor.SetViewportGridUVE(false, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(editor.IsViewportGridVisibleUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacityUVE(), 1.0F);

        ASSERT_TRUE(editor.SetViewportGridUVE(false, 0.35F));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs_reload.uvescene");
        reloaded.InitUVE();
        EXPECT_FALSE(reloaded.IsViewportGridVisibleUVE());
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridOpacityUVE(), 0.35F);
        reloaded.ShutdownUVE();
    }

    // A corrupt stored opacity falls back to its default alone; the stored visibility survives it.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.grid.opacity", 7.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs_corrupt.uvescene");
        corrupt.InitUVE();
        EXPECT_FALSE(corrupt.IsViewportGridVisibleUVE());
        EXPECT_FLOAT_EQ(corrupt.GetViewportGridOpacityUVE(), 1.0F);
        corrupt.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportGridCellSizeUVE_RefusesBadSizesAndPersistsAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_grid_cell.uvescene");
        editor.InitUVE();
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSizeUVE(), 1.0F);

        // Zero and negative sizes would blank the grid; NaN, infinity and sizes past the range
        // are refused too, and each refusal leaves the size as it was.
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(0.0F));
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(-1.0F));
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(std::numeric_limits<float>::infinity()));
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(EditorUVE::kMinimumViewportGridCellSizeUVE * 0.5F));
        EXPECT_FALSE(editor.SetViewportGridCellSizeUVE(EditorUVE::kMaximumViewportGridCellSizeUVE * 2.0F));
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSizeUVE(), 1.0F);

        ASSERT_TRUE(editor.SetViewportGridCellSizeUVE(0.25F));
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSizeUVE(), 0.25F);
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_grid_cell_reload.uvescene");
        reloaded.InitUVE();
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridCellSizeUVE(), 0.25F);
        reloaded.ShutdownUVE();
    }

    // A corrupt stored size falls back to the 1 m default.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.grid.cellSize", -3.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_grid_cell_corrupt.uvescene");
        corrupt.InitUVE();
        EXPECT_FLOAT_EQ(corrupt.GetViewportGridCellSizeUVE(), 1.0F);
        corrupt.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportSelectionOutlineUVE_RefusesBadValuesAndPersistsAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    using Color = EditorUVE::ViewportAxisColorUVE;
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisibleUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportSelectionOutlineThicknessUVE(), 2.0F);

        // Each bad value is refused whole: nothing changes.
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(false, Color{1.5F, 0.0F, 0.0F}, 2.0F));
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(false, Color{1.0F, 0.0F, 0.0F}, 0.5F));
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(false, Color{1.0F, 0.0F, 0.0F}, 7.0F));
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(
            false, Color{1.0F, 0.0F, 0.0F}, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisibleUVE());

        ASSERT_TRUE(editor.SetViewportSelectionOutlineUVE(false, Color{0.2F, 0.6F, 1.0F}, 4.0F));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_reload.uvescene");
        reloaded.InitUVE();
        EXPECT_FALSE(reloaded.IsViewportSelectionOutlineVisibleUVE());
        EXPECT_FLOAT_EQ(reloaded.GetViewportSelectionOutlineColorUVE().g, 0.6F);
        EXPECT_FLOAT_EQ(reloaded.GetViewportSelectionOutlineThicknessUVE(), 4.0F);
        reloaded.ShutdownUVE();
    }
    // A corrupt stored thickness falls back to its default alone; visibility and colour survive it.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.selectionOutline.thickness", 40.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_corrupt.uvescene");
        corrupt.InitUVE();
        EXPECT_FALSE(corrupt.IsViewportSelectionOutlineVisibleUVE());
        EXPECT_FLOAT_EQ(corrupt.GetViewportSelectionOutlineColorUVE().g, 0.6F);
        EXPECT_FLOAT_EQ(corrupt.GetViewportSelectionOutlineThicknessUVE(), 2.0F);
        corrupt.ShutdownUVE();
    }
    // One bad colour channel sends the whole colour back to its default, never a mixed colour.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.selectionOutline.b", 3.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_corrupt_color.uvescene");
        corrupt.InitUVE();
        const Color color = corrupt.GetViewportSelectionOutlineColorUVE();
        EXPECT_FLOAT_EQ(color.r, 1.0F);
        EXPECT_FLOAT_EQ(color.g, 0.62F);
        EXPECT_FLOAT_EQ(color.b, 0.16F);
        EXPECT_FALSE(corrupt.IsViewportSelectionOutlineVisibleUVE());
        corrupt.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportAxisColorsUVE_RefuseInvalidChannelsAndPersistAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    using AxisColorUVE = EditorUVE::ViewportAxisColorUVE;
    const AxisColorUVE chosenX{0.90F, 0.10F, 0.40F};
    const AxisColorUVE chosenY{0.20F, 0.80F, 0.30F};
    const AxisColorUVE chosenZ{0.15F, 0.45F, 0.95F};

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_axis_colors.uvescene");
        editor.InitUVE();

        // Nothing is seeded until the host - which owns the viewport's default hues - supplies
        // them, so the editor must say so rather than report three zeroes as a chosen palette.
        EXPECT_FALSE(editor.AreViewportAxisColorsSetUVE());

        // All three or none: one bad channel must not leave a partially applied palette.
        EXPECT_FALSE(editor.SetViewportAxisColorsUVE(AxisColorUVE{1.5F, 0.0F, 0.0F}, chosenY, chosenZ))
            << "a channel above 1 must be refused";
        EXPECT_FALSE(editor.SetViewportAxisColorsUVE(chosenX, AxisColorUVE{0.0F, -0.3F, 0.0F}, chosenZ))
            << "a negative channel must be refused";
        EXPECT_FALSE(editor.SetViewportAxisColorsUVE(
            chosenX, chosenY, AxisColorUVE{0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN()}))
            << "NaN must be refused, not compared its way through";
        EXPECT_FALSE(editor.AreViewportAxisColorsSetUVE())
            << "a refused palette must leave the state untouched, not half-written";

        ASSERT_TRUE(editor.SetViewportAxisColorsUVE(chosenX, chosenY, chosenZ));
        EXPECT_TRUE(editor.AreViewportAxisColorsSetUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(0).r, chosenX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(1).g, chosenY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(2).b, chosenZ.b);

        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_axis_colors_reload.uvescene");
        reloaded.InitUVE();
        ASSERT_TRUE(reloaded.AreViewportAxisColorsSetUVE())
            << "a saved palette must survive LoadSessionSettingsUVE on the next InitUVE()";
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColorUVE(0).r, chosenX.r);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColorUVE(0).g, chosenX.g);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColorUVE(1).g, chosenY.g);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColorUVE(2).b, chosenZ.b);

        // Reset drops the choice so the host re-seeds its own defaults; it must not write
        // default-looking values here, which would make this module a second home for them.
        reloaded.ResetViewportAxisColorsUVE();
        EXPECT_FALSE(reloaded.AreViewportAxisColorsSetUVE());
        reloaded.ShutdownUVE();
    }
    {
        // A corrupt settings file must cost the colour choice, not produce a viewport drawing
        // axes in a colour nobody picked.
        Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
        settings.SetBoolUVE("editor.viewport.axisColors.set", true);
        settings.SetDoubleUVE("editor.viewport.axisColors.x.r", 7.5);
        ASSERT_TRUE(settings.SaveUVE());

        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_axis_colors_corrupt.uvescene");
        corrupt.InitUVE();
        EXPECT_FALSE(corrupt.AreViewportAxisColorsSetUVE())
            << "an out-of-range persisted channel must fall back to unset, not be clamped in";
        corrupt.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, FavoritesUVE_ToggleReflectsImmediatelyAndPersistsAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const std::filesystem::path firstFavorite{"editor"};
    const std::filesystem::path secondFavorite{"retarget/UNIVEX_bone_retarget_map.json"};

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_favorites.uvescene");
        editor.InitUVE();
        EXPECT_FALSE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, firstFavorite));

        EditorUVEAccessUVE::ToggleProjectPathFavoriteUVE(editor, firstFavorite);
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, firstFavorite));
        EditorUVEAccessUVE::ToggleProjectPathFavoriteUVE(editor, secondFavorite);
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, secondFavorite));

        EditorUVEAccessUVE::ToggleProjectPathFavoriteUVE(editor, firstFavorite);
        EXPECT_FALSE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, firstFavorite))
            << "toggling twice must remove the favorite again";

        EditorUVEAccessUVE::ToggleProjectPathFavoriteUVE(editor, firstFavorite);
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_favorites_reload.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, firstFavorite))
            << "a favorite saved before shutdown must survive LoadSessionSettingsUVE on the next InitUVE()";
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, secondFavorite));
        EXPECT_FALSE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(editor, std::filesystem::path{"never-favorited"}));
        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, SelectionAndInspectorTransformEdit_ValidateLifetimeAndFiniteValues) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection.uvescene");
        editor.InitUVE();

        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        const Scene::EntityUVE root = services.GetEntityManagerUVE().CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});

        editor.SelectEntityUVE(root);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), root);

        Scene::TransformComponentUVE edited{};
        edited.localPosition = Math::Vector3UVE{3.0F, -2.0F, 7.0F};
        edited.localScale = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(edited));
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_EQ(services.GetEntityManagerUVE().GetComponentUVE<Scene::TransformComponentUVE>(root).localPosition,
                  edited.localPosition);

        edited.localScale.x = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(editor.SetSelectedLocalTransformUVE(edited));

        services.GetEntityManagerUVE().DestroyEntityUVE(root);
        editor.TickUVE();
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, MultiSelectionUVE_ToggleMaintainsOrderActiveFallbackAndSingleCommandSafety) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_selection.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, first, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, second, Scene::TransformComponentUVE{});

        editor.SelectEntityUVE(first);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{first});
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelectionUVE());

        editor.ToggleEntitySelectionUVE(second);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{first, second}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), second);
        EXPECT_FALSE(editor.HasSingleDocumentSelectionUVE());

        const Scene::TransformComponentUVE before =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(second);
        EXPECT_FALSE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 1.0F));
        EXPECT_FALSE(editor.DuplicateSelectedEntityUVE() != Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(second).localPosition,
                  before.localPosition);

        editor.ToggleEntitySelectionUVE(second);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{first});
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelectionUVE());

        editor.ToggleEntitySelectionUVE(second);
        editor.ToggleEntitySelectionUVE(first);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{second});
        EXPECT_EQ(editor.GetSelectedEntityUVE(), second);

        editor.ToggleEntitySelectionUVE(second);
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.HasSingleDocumentSelectionUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, MultiSelectionUVE_TickPrunesStaleEntitiesAndPromotesLastLiveSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_selection_stale.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, first, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, second, Scene::TransformComponentUVE{});

        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        entityManager.DestroyEntityUVE(second);
        editor.TickUVE();
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{first});
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelectionUVE());

        entityManager.DestroyEntityUVE(first);
        editor.TickUVE();
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, CreateDocumentEntityUVE_CreatesSelectedDirtyRootArchetypes) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_entities.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE empty = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(empty));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(empty));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::CameraComponentUVE>(empty));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::LightComponentUVE>(empty));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(empty));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(empty));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(empty).name, "Node3D");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), empty);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        const Scene::EntityUVE camera = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Camera);
        ASSERT_TRUE(entityManager.IsAliveUVE(camera));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(camera));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::CameraComponentUVE>(camera));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(camera));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(camera).name, "Camera");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), camera);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        const Scene::EntityUVE directionalLight =
            editor.CreateDocumentEntityUVE(EditorEntityKindUVE::DirectionalLight);
        ASSERT_TRUE(entityManager.IsAliveUVE(directionalLight));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(directionalLight));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::LightComponentUVE>(directionalLight));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::LightComponentUVE>(directionalLight).type,
                  Scene::LightTypeUVE::Directional);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(directionalLight));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(directionalLight).name, "Directional Light");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), directionalLight);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        const Scene::EntityUVE collisionBox = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::CollisionBox);
        ASSERT_TRUE(entityManager.IsAliveUVE(collisionBox));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(collisionBox));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(collisionBox));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(collisionBox));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(collisionBox).name, "Collision Box");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), collisionBox);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        const Scene::EntityUVE cube = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube);
        ASSERT_TRUE(entityManager.IsAliveUVE(cube));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(cube));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).kind,
                  Scene::PrimitiveMeshKindUVE::Cube);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(cube).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(cube).name, "Cube");

        const Scene::EntityUVE sphere = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::UVSphere);
        ASSERT_TRUE(entityManager.IsAliveUVE(sphere));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(sphere));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(sphere));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(sphere).kind,
                  Scene::PrimitiveMeshKindUVE::UVSphere);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(sphere).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(sphere).name, "UV Sphere");

        const Scene::EntityUVE plane = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Plane);
        ASSERT_TRUE(entityManager.IsAliveUVE(plane));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(plane));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(plane));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(plane).kind,
                  Scene::PrimitiveMeshKindUVE::Plane);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(plane).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.025F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(plane).name, "Plane");

        // One-root document: every created archetype lives under the scene root (chained by
        // creation-under-selection), and the document's single root is the SceneRoot itself.
        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 1U);
        EXPECT_EQ(roots.front(), editor.GetDocumentSceneRootUVE());
        EXPECT_TRUE(entityManager.IsAliveUVE(empty));
        EXPECT_TRUE(entityManager.IsAliveUVE(camera));
        EXPECT_TRUE(entityManager.IsAliveUVE(directionalLight));
        EXPECT_TRUE(entityManager.IsAliveUVE(collisionBox));
        EXPECT_TRUE(entityManager.IsAliveUVE(cube));
        EXPECT_TRUE(entityManager.IsAliveUVE(sphere));
        EXPECT_TRUE(entityManager.IsAliveUVE(plane));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PrimitiveAppearanceUVE_UpdatesColliderAndSupportsAtomicUndoRedo) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_primitive_appearance.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE cube = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube);
        ASSERT_TRUE(entityManager.IsAliveUVE(cube));
        const Scene::PrimitiveMeshComponentUVE before =
            entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube);

        const Scene::PrimitiveMeshComponentUVE after{Scene::PrimitiveMeshKindUVE::Plane,
                                                     Math::Vector3UVE{0.1F, 0.4F, 0.9F}};
        ASSERT_TRUE(editor.SetSelectedPrimitiveMeshUVE(after));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).baseColor, after.baseColor);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(cube).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.025F, 0.5F}));

        const Scene::PrimitiveMeshComponentUVE invalid{static_cast<Scene::PrimitiveMeshKindUVE>(99),
                                                       Math::Vector3UVE{0.1F, 0.4F, 0.9F}};
        EXPECT_FALSE(editor.SetSelectedPrimitiveMeshUVE(invalid));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).baseColor, after.baseColor);

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).kind, before.kind);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).baseColor, before.baseColor);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(cube).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).baseColor, after.baseColor);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(cube).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.025F, 0.5F}));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, CreateDocumentEntityUVE_RejectsInvalidKindsAndNonRunningStates) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_invalid.uvescene");
        EXPECT_EQ(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty), Scene::kInvalidEntityUVE);

        editor.InitUVE();
        EXPECT_EQ(editor.CreateDocumentEntityUVE(static_cast<EditorEntityKindUVE>(999)),
                  Scene::kInvalidEntityUVE);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
        EXPECT_EQ(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty), Scene::kInvalidEntityUVE);
    }

    engine.Shutdown();
}

TEST(EditorUVETest, CreateDocumentEntityUVE_AllocatesUniqueNames) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_names.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE firstCamera = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Camera);
        const Scene::EntityUVE secondCamera = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Camera);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(firstCamera));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(secondCamera));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(firstCamera).name, "Camera");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(secondCamera).name, "Camera 2");

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, SetSelectedEntityNameUVE_ValidatesInputAndMarksDocumentDirty) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rename.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});

        EXPECT_FALSE(editor.SetSelectedEntityNameUVE("Unselected"));
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Gameplay Root"));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(root));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name, "Gameplay Root");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), root);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE("Gameplay Root"));
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE(""));
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE("   \t"));
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE(std::string(97U, 'n')));

        entityManager.DestroyEntityUVE(root);
        editor.TickUVE();
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE("Destroyed"));
        editor.ShutdownUVE();
        EXPECT_FALSE(editor.SetSelectedEntityNameUVE("Shutdown"));
    }

    engine.Shutdown();
}

TEST(EditorUVETest, NodeWarningsUVE_ReportSetupProblemsAndScriptPath) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_node_warnings.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        // A plain, well-formed node has nothing to report and no script.
        const Scene::EntityUVE clean = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, clean, Scene::TransformComponentUVE{});
        EXPECT_TRUE(editor.GetNodeWarningsUVE(clean).empty());
        EXPECT_FALSE(editor.GetNodeScriptPathUVE(clean).has_value());

        // A mesh node with no mesh, and one pointing at a mesh the project does not have.
        const Scene::EntityUVE noMesh = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, noMesh, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(noMesh, Scene::MeshComponentUVE{});
        ASSERT_EQ(editor.GetNodeWarningsUVE(noMesh).size(), 1U);
        EXPECT_NE(editor.GetNodeWarningsUVE(noMesh).front().find("No mesh"), std::string::npos);

        const Scene::EntityUVE lostMesh = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lostMesh, Scene::TransformComponentUVE{});
        Scene::MeshComponentUVE lost{};
        lost.meshGuid = Asset::AssetGuidUVE{0x12345678ULL};
        lost.materialGuid = Asset::AssetGuidUVE{0x9abcdef0ULL};
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(lostMesh, lost);
        EXPECT_EQ(editor.GetNodeWarningsUVE(lostMesh).size(), 2U) << "missing mesh and missing material";

        // A script: a valid path is reported as the node's script; an invalid one is a warning too.
        const Scene::EntityUVE scripted = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, scripted, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(scripted, Scene::ScriptComponentUVE{"scripts/player.uvescript"});
        ASSERT_TRUE(editor.GetNodeScriptPathUVE(scripted).has_value());
        EXPECT_EQ(*editor.GetNodeScriptPathUVE(scripted), "scripts/player.uvescript");
        EXPECT_TRUE(editor.GetNodeWarningsUVE(scripted).empty());
        entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(scripted).scriptAssetPath = "../outside.uvescript";
        EXPECT_EQ(editor.GetNodeWarningsUVE(scripted).size(), 1U);

        // A Skeleton3D with no source model has no bones to show.
        const Scene::EntityUVE skeleton = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, skeleton, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton, Scene::Skeleton3DNodeComponentUVE{});
        EXPECT_EQ(editor.GetNodeWarningsUVE(skeleton).size(), 1U);

        // Not a document entity: nothing to say.
        EXPECT_TRUE(editor.GetNodeWarningsUVE(Scene::kInvalidEntityUVE).empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorClipboardUVE_CopyPasteResetComponentsAndTransformUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_clipboard.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
        const Core::TypeMetadataEntryUVE* const primitive =
            registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::PrimitiveMeshComponentUVE)));
        const Core::TypeMetadataEntryUVE* const visibility =
            registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::VisibilityComponentUVE)));
        ASSERT_NE(primitive, nullptr);
        ASSERT_NE(visibility, nullptr);

        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        const Scene::EntityUVE target = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE placed{};
        placed.localPosition = Math::Vector3UVE{3.0F, 1.0F, -2.0F};
        placed.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        AttachRootUVE(engine, source, placed);
        AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
            source, Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{0.9F, 0.1F, 0.2F}});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(target, Scene::PrimitiveMeshComponentUVE{});

        // Nothing copied yet: nothing to paste.
        editor.SelectEntityUVE(target);
        EXPECT_FALSE(editor.CanPasteSelectedComponentUVE(*primitive));
        EXPECT_FALSE(editor.PasteSelectedComponentUVE(*primitive));

        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.CopySelectedComponentUVE(*primitive));
        ASSERT_TRUE(editor.CopySelectedTransformUVE());
        // A clipboard of one type never pastes into another.
        EXPECT_FALSE(editor.CanPasteSelectedComponentUVE(*visibility));

        editor.SelectEntityUVE(target);
        ASSERT_TRUE(editor.PasteSelectedComponentUVE(*primitive));
        const auto& pasted = entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target);
        EXPECT_EQ(pasted.kind, Scene::PrimitiveMeshKindUVE::UVSphere);
        EXPECT_FLOAT_EQ(pasted.baseColor.x, 0.9F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).kind,
                  Scene::PrimitiveMeshKindUVE::Cube);

        // Transform: only the local pose travels.
        ASSERT_TRUE(editor.PasteSelectedTransformUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localPosition,
                  placed.localPosition);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localScale, placed.localScale);

        // Reset writes the type's defaults, and is one undoable step.
        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.ResetSelectedComponentUVE(*primitive));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(source).kind,
                  Scene::PrimitiveMeshComponentUVE{}.kind);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(source).kind,
                  Scene::PrimitiveMeshKindUVE::UVSphere);
        ASSERT_TRUE(editor.ResetSelectedTransformUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(source).localPosition, Math::Vector3UVE{});
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(source).localScale,
                  (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));

        // A node without the component cannot be copied from.
        editor.ClearSelectionUVE();
        EXPECT_FALSE(editor.CopySelectedComponentUVE(*primitive));
        EXPECT_FALSE(editor.CopySelectedTransformUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SetEntityVisibleUVE_TogglesAnyRowUndoablyWithoutTouchingSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_visibility_toggle.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE shown = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, shown, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(shown, Scene::VisibilityComponentUVE{});
        const Scene::EntityUVE other = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, other, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(other);

        // The eye works on a row that is not selected, and leaves the selection alone.
        ASSERT_TRUE(editor.SetEntityVisibleUVE(shown, false));
        EXPECT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(shown).visible);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), other);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        // Setting the value it already has is not an edit.
        EXPECT_FALSE(editor.SetEntityVisibleUVE(shown, false));

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(shown).visible);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), other);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(shown).visible);

        // A node without a Visibility component has no eye to toggle.
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(other));
        EXPECT_FALSE(editor.SetEntityVisibleUVE(other, false));
        EXPECT_FALSE(editor.SetEntityVisibleUVE(Scene::kInvalidEntityUVE, false));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ViewportFocusRequest_OnlyForNodesWithAPlaceInTheScene) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_viewport_focus.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE placed = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, placed, Scene::TransformComponentUVE{});
        // An entity with no transform is like the scene root or a plain Node: nothing to look at.
        const Scene::EntityUVE unplaced = entityManager.CreateEntityUVE();

        const std::uint32_t serialBefore = editor.GetViewportFocusRequestSerialUVE();
        EXPECT_TRUE(editor.CanFocusEntityInViewportUVE(placed));
        ASSERT_TRUE(editor.RequestViewportFocusUVE(placed));
        EXPECT_EQ(editor.GetViewportFocusEntityUVE(), placed);
        EXPECT_EQ(editor.GetViewportFocusRequestSerialUVE(), serialBefore + 1U);

        // Asking again is a new request, so the host focuses again even on the same node.
        ASSERT_TRUE(editor.RequestViewportFocusUVE(placed));
        EXPECT_EQ(editor.GetViewportFocusRequestSerialUVE(), serialBefore + 2U);

        // Refused requests leave the last one as it was.
        EXPECT_FALSE(editor.CanFocusEntityInViewportUVE(unplaced));
        EXPECT_FALSE(editor.RequestViewportFocusUVE(unplaced));
        EXPECT_FALSE(editor.RequestViewportFocusUVE(Scene::kInvalidEntityUVE));
        EXPECT_EQ(editor.GetViewportFocusEntityUVE(), placed);
        EXPECT_EQ(editor.GetViewportFocusRequestSerialUVE(), serialBefore + 2U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyBranchOpen_QueuesEveryRowWithChildrenInTheBranch) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_branch.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const auto makeNode = [&](const Scene::EntityUVE parent) {
            const Scene::EntityUVE node = entityManager.CreateEntityUVE();
            AttachRootUVE(engine, node, Scene::TransformComponentUVE{});
            if (parent != Scene::kInvalidEntityUVE) {
                services.GetSceneGraphUVE().SetParentUVE(entityManager, node, parent);
            }
            return node;
        };
        // branch -> middle -> leaf, branch -> sibling leaf; and a separate tree beside it.
        const Scene::EntityUVE branch = makeNode(Scene::kInvalidEntityUVE);
        const Scene::EntityUVE middle = makeNode(branch);
        const Scene::EntityUVE leaf = makeNode(middle);
        const Scene::EntityUVE siblingLeaf = makeNode(branch);
        const Scene::EntityUVE otherRoot = makeNode(Scene::kInvalidEntityUVE);
        const Scene::EntityUVE otherChild = makeNode(otherRoot);

        ASSERT_TRUE(editor.SetHierarchyBranchOpenUVE(branch, true));
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(branch), std::optional<bool>{true});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(middle), std::optional<bool>{true});
        // Leaves have nothing to open, and the tree beside it is not part of the branch.
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpenUVE(leaf).has_value());
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpenUVE(siblingLeaf).has_value());
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpenUVE(otherRoot).has_value());

        // Collapsing replaces the pending state for the whole branch.
        ASSERT_TRUE(editor.SetHierarchyBranchOpenUVE(branch, false));
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(branch), std::optional<bool>{false});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(middle), std::optional<bool>{false});

        // A pending row that is deleted before it is drawn is dropped by the next request.
        entityManager.DestroyEntityUVE(middle);
        ASSERT_TRUE(editor.SetHierarchyBranchOpenUVE(otherRoot, true));
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpenUVE(middle).has_value());
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(branch), std::optional<bool>{false});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpenUVE(otherRoot), std::optional<bool>{true});
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpenUVE(otherChild).has_value());

        EXPECT_FALSE(editor.SetHierarchyBranchOpenUVE(Scene::kInvalidEntityUVE, true));
        EXPECT_FALSE(editor.SetHierarchyBranchOpenUVE(middle, true));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_TransformUndoRedoRestoresSelectionAndDirtyState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_transform.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        Scene::TransformComponentUVE moved{};
        moved.localPosition = Math::Vector3UVE{4.0F, -3.0F, 2.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(moved));
        EXPECT_TRUE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), root);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(root).localPosition,
                  Math::Vector3UVE{});
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_TRUE(editor.CanRedoUVE());

        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), root);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(root).localPosition,
                  moved.localPosition);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_NameUndoRedoRestoresOptionalComponentState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_name.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Level Root"));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(root));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name, "Level Root");
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(root));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(root));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name, "Level Root");
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_CreationUndoRedoRecreatesArchetypeAndName) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_create.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE created = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::CollisionBox);
        ASSERT_TRUE(entityManager.IsAliveUVE(created));
        ASSERT_TRUE(editor.CanUndoUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(created));
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE recreated = editor.GetSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(recreated));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(recreated));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(recreated));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(recreated));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(recreated).name, "Collision Box");
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_NewMutationClearsRedoAndCapacityDiscardsOldestCommand) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_capacity.uvescene", 1U);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        Scene::TransformComponentUVE first{};
        first.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(first));
        Scene::TransformComponentUVE second = first;
        second.localPosition = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(second));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(root).localPosition,
                  first.localPosition);
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_TRUE(editor.CanRedoUVE());

        Scene::TransformComponentUVE third = first;
        third.localPosition = Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(third));
        EXPECT_FALSE(editor.CanRedoUVE());
        EXPECT_FALSE(editor.SetSelectedLocalTransformUVE(third));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_StaleTargetsAndNonRunningStateFailWithoutMutation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_stale.uvescene");
        EXPECT_FALSE(editor.UndoUVE());
        EXPECT_FALSE(editor.RedoUVE());
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        Scene::TransformComponentUVE moved{};
        moved.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(moved));
        entityManager.DestroyEntityUVE(root);
        editor.TickUVE();
        EXPECT_FALSE(editor.UndoUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());

        editor.ShutdownUVE();
        EXPECT_FALSE(editor.UndoUVE());
        EXPECT_FALSE(editor.RedoUVE());
    }

    engine.Shutdown();
}

TEST(EditorUVETest, DuplicateSelectedEntityUVE_RootCreatesNamedSiblingWithCopiedComponents) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_root.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE sourceTransform{};
        sourceTransform.localPosition = Math::Vector3UVE{2.0F, 4.0F, 6.0F};
        AttachRootUVE(engine, source, sourceTransform);
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(source);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(source, Scene::NameComponentUVE{"Lamp"});
        editor.SelectEntityUVE(source);

        const Scene::EntityUVE duplicate = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(duplicate));
        EXPECT_NE(duplicate, source);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), duplicate);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(duplicate).name, "Lamp 2");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(duplicate).localPosition,
                  sourceTransform.localPosition);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(duplicate));

        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 3U); // the scene root + the raw source + its duplicate
        EXPECT_NE(std::find(roots.begin(), roots.end(), source), roots.end());
        EXPECT_NE(std::find(roots.begin(), roots.end(), duplicate), roots.end());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, DuplicateSelectedEntityUVE_ChildRestoresAsSiblingUnderSameParent) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_child.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(child, Scene::NameComponentUVE{"Child"});
        editor.SelectEntityUVE(child);

        const Scene::EntityUVE duplicate = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(duplicate));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(duplicate).name, "Child 2");
        const std::vector<Scene::EntityUVE> children =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, parent);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_NE(std::find(children.begin(), children.end(), child), children.end());
        EXPECT_NE(std::find(children.begin(), children.end(), duplicate), children.end());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), duplicate);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, DeleteSelectedEntityUVE_DeletesSubtreeAndSelectsLiveParent) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_subtree.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        const Scene::EntityUVE grandchild = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, grandchild, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, grandchild, child);
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(child));
        EXPECT_FALSE(entityManager.IsAliveUVE(grandchild));
        EXPECT_TRUE(entityManager.IsAliveUVE(parent));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, DeleteSelectedEntityUVE_RootClearsSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_root.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);

        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(root));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_DuplicateUndoRedoUsesFreshHandlesAndRestoresDirtySelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_history.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, source, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(source, Scene::NameComponentUVE{"Actor"});
        editor.SelectEntityUVE(source);

        const Scene::EntityUVE duplicate = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(duplicate));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(duplicate));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), source);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE recreated = editor.GetSelectedEntityUVE();
        EXPECT_TRUE(entityManager.IsAliveUVE(recreated));
        EXPECT_NE(recreated, duplicate);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(recreated).name, "Actor 2");
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_DeleteUndoRedoRestoresSubtreeUnderOriginalParentWithFreshHandles) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_history.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(child, Scene::NameComponentUVE{"Deleted Child"});
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(child));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);
        ASSERT_TRUE(editor.UndoUVE());
        const Scene::EntityUVE restored = editor.GetSelectedEntityUVE();
        EXPECT_TRUE(entityManager.IsAliveUVE(restored));
        EXPECT_NE(restored, child);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(restored).name, "Deleted Child");
        const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(entityManager, parent);
        EXPECT_NE(std::find(children.begin(), children.end(), restored), children.end());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(restored));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_DeleteUndoRejectsStaleParentAndClearsTimeline) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_stale_parent.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        entityManager.DestroyEntityUVE(parent);
        EXPECT_FALSE(editor.UndoUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());
        // The ever-present scene root is the only thing left in the document.
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_NewMutationAfterDuplicateUndoInvalidatesRedo) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_redo_invalidation.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, source, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(source, Scene::NameComponentUVE{"Source"});
        editor.SelectEntityUVE(source);

        ASSERT_NE(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.CanRedoUVE());
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Source Revised"));
        EXPECT_FALSE(editor.CanRedoUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EntityLifecycleUVE_RejectsUnselectedStaleNonRunningAndUnsupportedCapture) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lifecycle_safety.uvescene");
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        editor.InitUVE();
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());

        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE unsupported = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, unsupported, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<UnregisteredEditorLifecycleComponentUVE>(
            unsupported, UnregisteredEditorLifecycleComponentUVE{7});
        editor.SelectEntityUVE(unsupported);
        const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.IsAliveUVE(unsupported));
        EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        EXPECT_TRUE(entityManager.IsAliveUVE(unsupported));

        entityManager.DestroyEntityUVE(unsupported);
        editor.TickUVE();
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        editor.ShutdownUVE();
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_RootMovesBelowTargetAndPreservesLocalTransform) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_root.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE target = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE localTransform{};
        localTransform.localPosition = Math::Vector3UVE{2.0F, -3.0F, 7.0F};
        AttachRootUVE(engine, moved, localTransform);
        editor.SelectEntityUVE(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(target));
        const std::vector<Scene::EntityUVE> targetChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, target);
        EXPECT_NE(std::find(targetChildren.begin(), targetChildren.end(), moved), targetChildren.end());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moved).localPosition,
                  localTransform.localPosition);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), moved);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.CanUndoUVE());
        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the still-top-level target
        EXPECT_NE(std::find(roots.begin(), roots.end(), target), roots.end());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_ChildCanReturnToRootWithoutDetachingDescendants) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_root_detach.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        const Scene::EntityUVE grandchild = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, grandchild, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, grandchild, child);
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the still-top-level parent
        EXPECT_NE(std::find(roots.begin(), roots.end(), parent), roots.end());
        EXPECT_TRUE(services.GetSceneGraphUVE().GetChildrenUVE(entityManager, parent).empty());
        // "Return to root" now means a direct child of the scene root.
        const std::vector<Scene::EntityUVE> sceneRootChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, editor.GetDocumentSceneRootUVE());
        EXPECT_NE(std::find(sceneRootChildren.begin(), sceneRootChildren.end(), child), sceneRootChildren.end());
        const std::vector<Scene::EntityUVE> childChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, child);
        EXPECT_NE(std::find(childChildren.begin(), childChildren.end(), grandchild), childChildren.end());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_ReparentUndoRedoRestoresParentsSelectionAndDirtyState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_history.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE oldParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, oldParent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE newParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, newParent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, moved, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, moved, oldParent);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(moved, Scene::NameComponentUVE{"Moved"});
        editor.SelectEntityUVE(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(newParent));
        const std::vector<Scene::EntityUVE> childrenAfterReparent =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, newParent);
        EXPECT_NE(std::find(childrenAfterReparent.begin(), childrenAfterReparent.end(), moved),
                  childrenAfterReparent.end());
        ASSERT_TRUE(editor.UndoUVE());
        const std::vector<Scene::EntityUVE> childrenAfterUndo =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, oldParent);
        EXPECT_NE(std::find(childrenAfterUndo.begin(), childrenAfterUndo.end(), moved), childrenAfterUndo.end());
        EXPECT_EQ(editor.GetSelectedEntityUVE(), moved);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.RedoUVE());
        const std::vector<Scene::EntityUVE> childrenAfterRedo =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, newParent);
        EXPECT_NE(std::find(childrenAfterRedo.begin(), childrenAfterRedo.end(), moved), childrenAfterRedo.end());
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Moved Again"));
        EXPECT_FALSE(editor.CanRedoUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, SceneRootUVE_ChildrenOfTheTransformlessRootStayFullyEditable) {
    // The scene root is a pure Node: in the hierarchy, with no transform. Everything that used to
    // read the parent's world transform must treat it as identity, the way the scene graph does -
    // otherwise every direct child of the root would refuse to move, rotate or be reparented.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scene_root_children.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE sceneRoot = editor.GetDocumentSceneRootUVE();
        ASSERT_NE(sceneRoot, Scene::kInvalidEntityUVE);
        ASSERT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(sceneRoot));

        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localPosition = Math::Vector3UVE{0.0F, 10.0F, 0.0F};
        AttachRootUVE(engine, parent, parentTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, parent, sceneRoot);
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, sceneRoot);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        // A gizmo drag on a direct child of the root: world delta == local delta.
        editor.SelectEntityUVE(child);
        ASSERT_TRUE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 2.0F));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localPosition,
                  (Math::Vector3UVE{3.0F, 2.0F, 3.0F}));
        EXPECT_TRUE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::Y, 0.5F));
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        // Keep-world reparent out from under the root and back onto it by name, then undo and
        // redo both: every step names the root as a parent, which a spatial-only check refused.
        ASSERT_TRUE(editor.SetReparentTransformModeUVE(EditorReparentTransformModeUVE::KeepWorld));
        const Math::Vector3UVE worldBefore =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldPosition;
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(parent));
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldPosition.y,
                    worldBefore.y, 1.0e-5F);
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(sceneRoot));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, sceneRoot);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldPosition.y,
                    worldBefore.y, 1.0e-5F);

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, sceneRoot);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_RedoOfMoveToDocumentRootLandsUnderTheSceneRoot) {
    // "Move to document root" is spelled as no parent, and means the scene root. History has to
    // record the parent the entity actually got, or redo would set no parent at all and leave a
    // stray beside the root - a second top-level node in a one-root document.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_to_root_redo.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE sceneRoot = editor.GetDocumentSceneRootUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, parent, sceneRoot);
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, sceneRoot);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, sceneRoot);
        EXPECT_EQ(editor.GetDocumentRootsUVE(), std::vector<Scene::EntityUVE>{sceneRoot});

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_RejectsCyclesNoOpNonDocumentStaleAndNonRunningStates) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_safety.uvescene");
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, root);
        editor.SelectEntityUVE(root);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(root));
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(child));
        // kInvalidEntityUVE as the new parent now means "move under the scene root" (see
        // ReparentDocumentEntityUVE), so rejection is exercised with a dead handle instead.
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(Scene::EntityUVE{9999U, 1U}));
        editor.SelectEntityUVE(child);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(root));
        const Scene::EntityUVE nonDocumentEntity = entityManager.CreateEntityUVE();
        editor.SelectEntityUVE(nonDocumentEntity);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(root));

        const Scene::EntityUVE staleTarget = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, staleTarget, Scene::TransformComponentUVE{});
        entityManager.DestroyEntityUVE(staleTarget);
        editor.SelectEntityUVE(root);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(staleTarget));

        const Scene::EntityUVE malformed = entityManager.CreateEntityUVE();
        editor.SelectEntityUVE(malformed);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        editor.ShutdownUVE();
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_ReparentUndoRejectsStalePriorParentAndClearsTimeline) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_stale_parent.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE oldParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, oldParent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE newParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, newParent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, moved, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, moved, oldParent);
        editor.SelectEntityUVE(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(newParent));
        entityManager.DestroyEntityUVE(oldParent);
        EXPECT_FALSE(editor.UndoUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, KeepWorldReparentUVE_PreservesCompatibleWorldTrsAndHistory) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_keep_world_reparent.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localPosition = Math::Vector3UVE{10.0F, -2.0F, 5.0F};
        parentTransform.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.5F,
                                              parentTransform.localRotation));
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, parentTransform);
        Scene::TransformComponentUVE movedTransform{};
        movedTransform.localPosition = Math::Vector3UVE{4.0F, 3.0F, -2.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              movedTransform.localRotation));
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, moved, movedTransform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const Scene::WorldTransformComponentUVE worldBefore =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(moved);
        editor.SelectEntityUVE(moved);
        ASSERT_TRUE(editor.SetReparentTransformModeUVE(EditorReparentTransformModeUVE::KeepWorld));
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(parent));
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const Scene::WorldTransformComponentUVE& worldAfter =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(moved);
        EXPECT_NEAR(worldAfter.worldPosition.x, worldBefore.worldPosition.x, 0.0001F);
        EXPECT_NEAR(worldAfter.worldPosition.y, worldBefore.worldPosition.y, 0.0001F);
        EXPECT_NEAR(worldAfter.worldPosition.z, worldBefore.worldPosition.z, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.x, worldBefore.worldScale.x, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.y, worldBefore.worldScale.y, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.z, worldBefore.worldScale.z, 0.0001F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moved).localPosition,
                  movedTransform.localPosition);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_NE(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moved).localPosition,
                  movedTransform.localPosition);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, KeepWorldReparentUVE_RejectsShearProneAndNearZeroScaleParents) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_keep_world_reject.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::TransformComponentUVE shearParentTransform{};
        shearParentTransform.localScale = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              shearParentTransform.localRotation));
        const Scene::EntityUVE shearParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, shearParent, shearParentTransform);
        Scene::TransformComponentUVE movedTransform{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              movedTransform.localRotation));
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, moved, movedTransform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        editor.SelectEntityUVE(moved);
        ASSERT_TRUE(editor.SetReparentTransformModeUVE(EditorReparentTransformModeUVE::KeepWorld));
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(shearParent));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(moved).localPosition,
                  movedTransform.localPosition);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        Scene::TransformComponentUVE tinyParentTransform{};
        tinyParentTransform.localScale = Math::Vector3UVE{0.0001F, 1.0F, 1.0F};
        const Scene::EntityUVE tinyParent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, tinyParent, tinyParentTransform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(tinyParent));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SaveThenLoadScene_RoundTripsDocumentRootsWithoutSerializingEditorCamera) {
    const std::filesystem::path scenePath = "uve_editor_tests_round_trip.uvescene";
    std::filesystem::remove(scenePath);
    std::filesystem::remove(scenePath.string() + ".editor-recovery");

    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        const Scene::EntityUVE root = services.GetEntityManagerUVE().CreateEntityUVE();
        Scene::TransformComponentUVE rootTransform{};
        rootTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});

        const Scene::EntityUVE child = services.GetEntityManagerUVE().CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{4.0F, 5.0F, 6.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(services.GetEntityManagerUVE(), child, root);

        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(rootTransform));
        ASSERT_TRUE(editor.SaveSceneUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(std::filesystem::exists(scenePath));

        Scene::TransformComponentUVE modified = rootTransform;
        modified.localPosition = Math::Vector3UVE{9.0F, 9.0F, 9.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(modified));
        ASSERT_TRUE(editor.LoadSceneUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());

        const std::vector<Scene::EntityUVE> loadedRoots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(loadedRoots.size(), 1U);
        // The single root is the scene root; the authored root (with its saved transform and
        // its own child) sits one level under it - the load wrapped the pre-root save's
        // top-level entities beneath the one-root invariant.
        const Scene::EntityUVE loadedSceneRoot = loadedRoots.front();
        const std::vector<Scene::EntityUVE> loadedSceneRootChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(services.GetEntityManagerUVE(), loadedSceneRoot);
        ASSERT_EQ(loadedSceneRootChildren.size(), 1U);
        const Scene::TransformComponentUVE& loadedTransform =
            services.GetEntityManagerUVE().GetComponentUVE<Scene::TransformComponentUVE>(
                loadedSceneRootChildren.front());
        EXPECT_EQ(loadedTransform.localPosition, rootTransform.localPosition);
        EXPECT_EQ(services.GetSceneGraphUVE()
                      .GetChildrenUVE(services.GetEntityManagerUVE(), loadedSceneRootChildren.front())
                      .size(),
                  1U);
        EXPECT_TRUE(editor.IsSceneDirtyUVE()); // the load wrapped the file's top level

        editor.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(scenePath);
    std::filesystem::remove(scenePath.string() + ".editor-recovery");
}

TEST(EditorUVETest, TranslateSelectedAlongAxis_UpdatesLocalTransformAndConvertsParentScale) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gizmo.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localScale = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        AttachRootUVE(engine, parent, parentTransform);

        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        editor.SelectEntityUVE(child);
        EXPECT_TRUE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 2.0F));
        const Scene::TransformComponentUVE& translated =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child);
        EXPECT_NEAR(translated.localPosition.x, 2.0F, 0.0001F);
        EXPECT_NEAR(translated.localPosition.y, 2.0F, 0.0001F);
        EXPECT_NEAR(translated.localPosition.z, 3.0F, 0.0001F);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localPosition.x,
                    1.0F, 0.0001F);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localPosition.x,
                    2.0F, 0.0001F);
        EXPECT_FALSE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::None, 1.0F));
        EXPECT_FALSE(editor.TranslateSelectedAlongAxisUVE(
            EditorTransformAxisUVE::Y, std::numeric_limits<float>::infinity()));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, RotateSelectedAroundWorldAxis_RotatesRootPreservesOtherLocalFieldsAndReplaysHistory) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_root.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE initial{};
        initial.localPosition = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        initial.localScale = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        AttachRootUVE(engine, entity, initial);

        editor.SelectEntityUVE(entity);
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::Z,
                                                            std::numbers::pi_v<float> * 0.5F));
        const Scene::TransformComponentUVE& rotated =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        const Math::Vector3UVE localXAxis =
            Math::RotateVectorUVE(rotated.localRotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(localXAxis.x, 0.0F, 0.0001F);
        EXPECT_NEAR(localXAxis.y, 1.0F, 0.0001F);
        EXPECT_NEAR(localXAxis.z, 0.0F, 0.0001F);
        EXPECT_EQ(rotated.localPosition, initial.localPosition);
        EXPECT_EQ(rotated.localScale, initial.localScale);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
                  initial.localRotation);
        ASSERT_TRUE(editor.RedoUVE());
        const Math::Vector3UVE replayedXAxis = Math::RotateVectorUVE(
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
            Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(replayedXAxis.x, 0.0F, 0.0001F);
        EXPECT_NEAR(replayedXAxis.y, 1.0F, 0.0001F);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, RotateSelectedAroundWorldAxis_ConvertsParentWorldRotationToLocalRotation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_parented.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        Math::QuaternionUVE parentRotation{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.5F, parentRotation));
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localRotation = parentRotation;
        AttachRootUVE(engine, parent, parentTransform);

        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        childTransform.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        editor.SelectEntityUVE(child);
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::X,
                                                            std::numbers::pi_v<float> * 0.5F));
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        Math::QuaternionUVE worldDelta{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.5F, worldDelta));
        const Math::QuaternionUVE expectedWorld = Math::MultiplyUVE(worldDelta, parentRotation);
        const Scene::WorldTransformComponentUVE& childWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child);
        const Math::Vector3UVE expectedProbe =
            Math::RotateVectorUVE(expectedWorld, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        const Math::Vector3UVE actualProbe =
            Math::RotateVectorUVE(childWorld.worldRotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        EXPECT_NEAR(actualProbe.x, expectedProbe.x, 0.0001F);
        EXPECT_NEAR(actualProbe.y, expectedProbe.y, 0.0001F);
        EXPECT_NEAR(actualProbe.z, expectedProbe.z, 0.0001F);
        const Scene::TransformComponentUVE& rotated =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child);
        EXPECT_EQ(rotated.localPosition, childTransform.localPosition);
        EXPECT_EQ(rotated.localScale, childTransform.localScale);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, RotateSelectedAroundWorldAxis_RejectsInvalidOrUnsafeStateWithoutMutation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_safety.uvescene");
        editor.InitUVE();
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::Z, 1.0F));

        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(entity);
        const Scene::TransformComponentUVE before =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::None, 1.0F));
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::Y,
                                                             std::numeric_limits<float>::infinity()));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
                  before.localRotation);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ScaleSelectedAlongAxis_UpdatesOnlyPositiveLocalScaleAndReplaysHistory) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scale.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE initial{};
        initial.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        initial.localScale = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, child, initial);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        editor.SelectEntityUVE(child);
        ASSERT_TRUE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::Y, 1.5F));
        const Scene::TransformComponentUVE& scaled =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child);
        EXPECT_EQ(scaled.localPosition, initial.localPosition);
        EXPECT_EQ(scaled.localRotation, initial.localRotation);
        EXPECT_NEAR(scaled.localScale.x, 1.0F, 0.0001F);
        EXPECT_NEAR(scaled.localScale.y, 3.5F, 0.0001F);
        EXPECT_NEAR(scaled.localScale.z, 3.0F, 0.0001F);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localScale,
                  initial.localScale);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localScale.y,
                    3.5F, 0.0001F);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScaleSelectedAlongAxis_RejectsUnsafeInputWithoutMutation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scale_safety.uvescene");
        editor.InitUVE();
        EXPECT_FALSE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 1.0F));
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(entity);
        const Scene::TransformComponentUVE before = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_FALSE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::None, 1.0F));
        EXPECT_FALSE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::X, -1.0F));
        EXPECT_FALSE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::Z,
                                                       std::numeric_limits<float>::infinity()));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localScale,
                  before.localScale);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScaleSelectedUniformlyUVE_AppliesAdditiveOffsetAndSnapping) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_uniform_scale_offset.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE initial{};
        initial.localScale = Math::Vector3UVE{2.0F, 1.0F, 1.0F};
        AttachRootUVE(engine, entity, initial);
        editor.SelectEntityUVE(entity);
        ASSERT_TRUE(editor.ScaleSelectedUniformlyUVE(1.0F));
        const Scene::TransformComponentUVE& additive =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_NEAR(additive.localScale.x, 3.0F, 0.0001F);
        EXPECT_NEAR(additive.localScale.y, 2.0F, 0.0001F);
        EXPECT_NEAR(additive.localScale.z, 2.0F, 0.0001F);
        EditorTransformSnappingSettingsUVE settings{};
        settings.enabled = true;
        settings.scaleStep = 0.25F;
        ASSERT_TRUE(editor.SetTransformSnappingSettingsUVE(settings));
        ASSERT_TRUE(editor.ScaleSelectedUniformlyUVE(0.37F));
        const Scene::TransformComponentUVE& snapped =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_NEAR(snapped.localScale.x, 3.25F, 0.0001F);
        EXPECT_NEAR(snapped.localScale.y, 2.25F, 0.0001F);
        EXPECT_NEAR(snapped.localScale.z, 2.25F, 0.0001F);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScaleSelectedUniformlyUVE_RejectsAsymmetricFloorWithoutPartialMutation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_uniform_scale_floor.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE initial{};
        initial.localScale = Math::Vector3UVE{0.01F, 5.0F, 5.0F};
        AttachRootUVE(engine, entity, initial);
        editor.SelectEntityUVE(entity);
        EXPECT_FALSE(editor.ScaleSelectedUniformlyUVE(-0.02F));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localScale,
                  initial.localScale);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SelectedBoundsQuery_BuildsIdentityWorldBoxWithoutMutatingEditorState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection_bounds_identity.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = Math::Vector3UVE{4.0F, -5.0F, 6.0F};
        AttachRootUVE(engine, entity, transform);
        editor.SelectEntityUVE(entity);
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        Scene::ColliderComponentUVE collider{};
        collider.halfExtents = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        editor.SelectEntityUVE(entity);

        const std::optional<EditorSelectionBoundsUVE> bounds = editor.TryGetSelectedBoundsUVE();
        ASSERT_TRUE(bounds.has_value());
        EXPECT_EQ(bounds->worldCenter, transform.localPosition);
        EXPECT_EQ(bounds->worldCorners[0], (Math::Vector3UVE{3.0F, -7.0F, 3.0F}));
        EXPECT_EQ(bounds->worldCorners[6], (Math::Vector3UVE{5.0F, -3.0F, 9.0F}));
        const Scene::TransformComponentUVE& afterQuery =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        EXPECT_EQ(afterQuery.localPosition, transform.localPosition);
        EXPECT_EQ(afterQuery.localRotation, transform.localRotation);
        EXPECT_EQ(afterQuery.localScale, transform.localScale);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), entity);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.CanRedoUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, SelectedBoundsQuery_UsesDerivedParentTransformAndRejectsUnsafeState) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection_bounds_parented.uvescene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        Math::QuaternionUVE parentRotation{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.5F, parentRotation));
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localPosition = Math::Vector3UVE{10.0F, 20.0F, 30.0F};
        parentTransform.localRotation = parentRotation;
        parentTransform.localScale = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        AttachRootUVE(engine, parent, parentTransform);

        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        AttachRootUVE(engine, child, childTransform);
        Scene::ColliderComponentUVE collider{};
        collider.halfExtents = Math::Vector3UVE{0.5F, 1.0F, 1.5F};
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(child, collider);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        editor.SelectEntityUVE(child);

        const std::optional<EditorSelectionBoundsUVE> bounds = editor.TryGetSelectedBoundsUVE();
        ASSERT_TRUE(bounds.has_value());
        EXPECT_NEAR(bounds->worldCenter.x, 10.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCenter.y, 22.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCenter.z, 30.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[0].x, 13.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[0].y, 21.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[0].z, 24.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[6].x, 7.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[6].y, 23.0F, 0.0001F);
        EXPECT_NEAR(bounds->worldCorners[6].z, 36.0F, 0.0001F);

        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(child).halfExtents.x = 0.0F;
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(child).halfExtents = collider.halfExtents;
        Scene::WorldTransformComponentUVE& worldTransform =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child);
        worldTransform.dirty = true;
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        worldTransform.dirty = false;
        const Math::Vector3UVE savedScale = worldTransform.worldScale;
        worldTransform.worldScale.x = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        worldTransform.worldScale = savedScale;
        const Math::QuaternionUVE savedRotation = worldTransform.worldRotation;
        worldTransform.worldRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
        worldTransform.worldRotation = savedRotation;
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        const Scene::EntityUVE nonDocumentEntity = entityManager.CreateEntityUVE();
        editor.SelectEntityUVE(nonDocumentEntity);
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());

        editor.ShutdownUVE();
        EXPECT_FALSE(editor.TryGetSelectedBoundsUVE().has_value());
    }

    engine.Shutdown();
}

TEST(EditorUVETest, TransformSnappingSettings_ExposeSafeDefaultsAndRejectInvalidValues) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_snapping_settings.uvescene");
        editor.InitUVE();

        const EditorTransformSnappingSettingsUVE defaults = editor.GetTransformSnappingSettingsUVE();
        EXPECT_FALSE(defaults.enabled);
        EXPECT_FLOAT_EQ(defaults.translateStep, 1.0F);
        EXPECT_FLOAT_EQ(defaults.rotateStepDegrees, 15.0F);
        EXPECT_FLOAT_EQ(defaults.scaleStep, 0.1F);

        EditorTransformSnappingSettingsUVE configured{true, 0.5F, 45.0F, 0.25F};
        ASSERT_TRUE(editor.SetTransformSnappingSettingsUVE(configured));
        EXPECT_EQ(editor.GetTransformSnappingSettingsUVE().enabled, configured.enabled);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().translateStep, configured.translateStep);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().rotateStepDegrees, configured.rotateStepDegrees);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().scaleStep, configured.scaleStep);

        EditorTransformSnappingSettingsUVE invalid = configured;
        invalid.translateStep = 0.0F;
        EXPECT_FALSE(editor.SetTransformSnappingSettingsUVE(invalid));
        invalid = configured;
        invalid.rotateStepDegrees = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(editor.SetTransformSnappingSettingsUVE(invalid));
        invalid = configured;
        invalid.scaleStep = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.SetTransformSnappingSettingsUVE(invalid));
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().translateStep, configured.translateStep);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().rotateStepDegrees, configured.rotateStepDegrees);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettingsUVE().scaleStep, configured.scaleStep);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, TransformSnapping_QuantizesCommandsWithoutHistoryDriftAndReplaysRotation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_snapping_commands.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE initial{};
        initial.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, entity, initial);
        editor.SelectEntityUVE(entity);

        ASSERT_TRUE(editor.SetTransformSnappingSettingsUVE(
            EditorTransformSnappingSettingsUVE{true, 0.5F, 15.0F, 0.25F}));
        EXPECT_FALSE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 0.1F));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        ASSERT_TRUE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 0.74F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    1.5F, 0.0001F);
        ASSERT_TRUE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, -0.74F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    initial.localPosition.x, 0.0001F);

        ASSERT_TRUE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 0.37F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localScale.x,
                    1.25F, 0.0001F);
        EXPECT_FALSE(editor.ScaleSelectedAlongAxisUVE(EditorTransformAxisUVE::X, -1.24F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localScale.x,
                    1.25F, 0.0001F);

        const float twentyDegreesRadians = (20.0F * std::numbers::pi_v<float>) / 180.0F;
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxisUVE(EditorTransformAxisUVE::Z, twentyDegreesRadians));
        const Math::Vector3UVE rotatedXAxis = Math::RotateVectorUVE(
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
            Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(rotatedXAxis.x, std::cos(std::numbers::pi_v<float> / 12.0F), 0.0001F);
        EXPECT_NEAR(rotatedXAxis.y, std::sin(std::numbers::pi_v<float> / 12.0F), 0.0001F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
                  initial.localRotation);
        ASSERT_TRUE(editor.RedoUVE());
        const Math::Vector3UVE replayedXAxis = Math::RotateVectorUVE(
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localRotation,
            Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(replayedXAxis.x, rotatedXAxis.x, 0.0001F);
        EXPECT_NEAR(replayedXAxis.y, rotatedXAxis.y, 0.0001F);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, LoadMissingScene_FailsWithoutDestroyingCurrentDocument) {
    const std::filesystem::path missingScenePath = "uve_editor_tests_missing.uvescene";
    std::filesystem::remove(missingScenePath);

    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), missingScenePath);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        const Scene::EntityUVE root = services.GetEntityManagerUVE().CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});

        EXPECT_FALSE(editor.LoadSceneUVE());
        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the surviving authored root
        EXPECT_NE(std::find(roots.begin(), roots.end(), root), roots.end());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_RestoresSnapshotRejectsAuthoringAndPreservesSelectionIntent) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_restore.uvescene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE authored{};
        authored.localPosition = Math::Vector3UVE{2.0F, 3.0F, -4.0F};
        AttachRootUVE(engine, root, authored);
        editor.SelectEntityUVE(root);

        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        EXPECT_TRUE(engine.IsTransientSimulationSessionActiveUVE());
        EXPECT_FALSE(editor.SetSelectedLocalTransformUVE(Scene::TransformComponentUVE{}));
        EXPECT_EQ(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.UndoUVE());

        ASSERT_TRUE(editor.PausePlayModeUVE());
        EXPECT_EQ(engine.GetSimulationExecutionModeUVE(), Core::SimulationExecutionModeUVE::Paused);
        ASSERT_TRUE(editor.StepPlayModeUVE());
        EXPECT_FALSE(editor.StepPlayModeUVE());
        engine.TickFrameUVE();
        ASSERT_TRUE(editor.ResumePlayModeUVE());
        EXPECT_EQ(engine.GetSimulationExecutionModeUVE(), Core::SimulationExecutionModeUVE::Running);

        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Edit);
        EXPECT_FALSE(engine.IsTransientSimulationSessionActiveUVE());
        const std::vector<Scene::EntityUVE> restoredRoots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(restoredRoots.size(), 2U); // the scene root + the restored authored root
        const Scene::EntityUVE restoredAuthored =
            restoredRoots.front() == editor.GetDocumentSceneRootUVE() ? restoredRoots.back()
                                                                       : restoredRoots.front();
        EXPECT_NE(restoredAuthored, root);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), restoredAuthored);
        const Scene::TransformComponentUVE& restored =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(restoredAuthored);
        EXPECT_EQ(restored.localPosition, authored.localPosition);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, GetDocumentRootsUVE_ExcludesEditorInternalEntitiesAndTheySurvivePlayStop) {
    // Regression test for a real, reproducible crash: entities tagged EditorInternalEntityComponentUVE
    // (e.g. the editor Viewport's own hidden free-look-camera proxy) are scene roots just like real
    // document content (AttachTransformUVE always creates a root), but must never be swept up by
    // Play-mode's destroy/recreate snapshot cycle in StopPlayModeUVE() - before this fix, they were,
    // which left a cached EntityUVE elsewhere pointing at a destroyed entity and crashed the next
    // time it was dereferenced. See GetDocumentRootsUVE()'s own comment for the fix.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_internal_entity.uvescene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE documentRoot = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, documentRoot, Scene::TransformComponentUVE{});

        const Scene::EntityUVE internalEntity = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, internalEntity, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::EditorInternalEntityComponentUVE>(internalEntity);

        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the authored document root
        EXPECT_NE(std::find(roots.begin(), roots.end(), documentRoot), roots.end());
        EXPECT_EQ(std::find(roots.begin(), roots.end(), internalEntity), roots.end());

        editor.SelectEntityUVE(documentRoot);
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        ASSERT_TRUE(editor.StopPlayModeUVE());

        // The actual bug: confirm the internal entity is untouched (still alive, same handle) by
        // the destroy/recreate cycle that just ran on every *document* root.
        EXPECT_TRUE(entityManager.IsAliveUVE(internalEntity));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(internalEntity));

        const std::vector<Scene::EntityUVE> rootsAfterStop = editor.GetDocumentRootsUVE();
        ASSERT_EQ(rootsAfterStop.size(), 2U); // the scene root + the restored authored root
        EXPECT_NE(rootsAfterStop.back(), documentRoot); // restored as a fresh handle, like every real root

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_SpawnPointTeleportsThePlayerAndStopGivesEverythingBack) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn.uvescene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // The player: a root-level entity with the controller component, authored at origin.
        const Scene::EntityUVE player = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, player, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
            player, Scene::CharacterControllerComponentUVE{});

        // The spawn point: another root-level entity at (3, 1, -2), no offset, one-shot so both
        // sandbox mutations (teleport, spend) can be measured in one session.
        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE spawnTransform{};
        spawnTransform.localPosition = Math::Vector3UVE{3.0F, 1.0F, -2.0F};
        AttachRootUVE(engine, spawn, spawnTransform);
        Scene::SpawnPoint3DNodeComponentUVE spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn, spawnPoint);

        // Compose reads the spawn node's WORLD pose, so the sweep must have run since attaching.
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        ASSERT_TRUE(editor.EnterPlayModeUVE());
        const Scene::TransformComponentUVE& playedTransform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(player);
        EXPECT_NEAR(playedTransform.localPosition.x, 3.0F, 1.0e-5F);
        EXPECT_NEAR(playedTransform.localPosition.y, 1.0F, 1.0e-5F);
        EXPECT_NEAR(playedTransform.localPosition.z, -2.0F, 1.0e-5F);
        // The documented simulation-write rule: the quaternion is now the truth, so the authored
        // Euler cache stops replaying over this teleport.
        EXPECT_EQ(playedTransform.rotationEditMode, Scene::RotationEditModeUVE::Quaternion);
        // One-shot is spent inside the sandbox.
        EXPECT_FALSE(entityManager.GetComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn).enabled);

        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(player)
                        .worldPosition.x,
                    3.0F, 1.0e-5F);

        // Stop hands BOTH back - the snapshot remakes document entities, so the two roles are
        // found again by component, never by the old handles (see the restore test above).
        ASSERT_TRUE(editor.StopPlayModeUVE());
        Scene::EntityUVE restoredPlayer = Scene::kInvalidEntityUVE;
        entityManager.ForEachUVE<Scene::CharacterControllerComponentUVE>(
            [&restoredPlayer](const Scene::EntityUVE entity, Scene::CharacterControllerComponentUVE&) {
                restoredPlayer = entity;
            });
        ASSERT_NE(restoredPlayer, Scene::kInvalidEntityUVE);
        const Scene::TransformComponentUVE& restoredTransform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(restoredPlayer);
        EXPECT_EQ(restoredTransform.localPosition.x, 0.0F);
        EXPECT_EQ(restoredTransform.localPosition.z, 0.0F);
        EXPECT_EQ(restoredTransform.rotationEditMode, Scene::RotationEditModeUVE::Euler);
        Scene::EntityUVE restoredSpawn = Scene::kInvalidEntityUVE;
        entityManager.ForEachUVE<Scene::SpawnPoint3DNodeComponentUVE>(
            [&restoredSpawn](const Scene::EntityUVE entity, Scene::SpawnPoint3DNodeComponentUVE&) {
                restoredSpawn = entity;
            });
        ASSERT_NE(restoredSpawn, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(
            entityManager.GetComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(restoredSpawn).enabled);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_SpawnPointWithOffsetAndParentPlacesRespectingBothKinds) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_offset.uvescene", 100U,
                         &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

        // Spawn point node at (4, 0, 0) whose authored offset raises the player by (0, 0.5, 0);
        // one-shot false, so the point stays live through the session.
        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE spawnTransform{};
        spawnTransform.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
        AttachRootUVE(engine, spawn, spawnTransform);
        Scene::SpawnPoint3DNodeComponentUVE spawnPoint{};
        spawnPoint.localPosition = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
        entityManager.AddComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn, spawnPoint);

        // The player this time is a child of a scaled, translated parent: the world pose must
        // arrive through the sweep's exact inverse, not by pretending the parent is identity.
        const Scene::EntityUVE group = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE groupTransform{};
        groupTransform.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
        groupTransform.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        AttachRootUVE(engine, group, groupTransform);
        const Scene::EntityUVE player = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, player, Scene::TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, player, group);
        entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
            player, Scene::CharacterControllerComponentUVE{});

        sceneGraph.UpdateUVE(entityManager);
        ASSERT_TRUE(editor.EnterPlayModeUVE());

        // Expected world pose = (4, 0.5, 0); expected local = ((4-10)/2, (0.5-0)/2, 0).
        const Scene::TransformComponentUVE& local =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(player);
        EXPECT_NEAR(local.localPosition.x, -3.0F, 1.0e-5F);
        EXPECT_NEAR(local.localPosition.y, 0.25F, 1.0e-5F);
        EXPECT_NEAR(local.localPosition.z, 0.0F, 1.0e-5F);
        sceneGraph.UpdateUVE(entityManager);
        const Scene::WorldTransformComponentUVE& world =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(player);
        EXPECT_NEAR(world.worldPosition.x, 4.0F, 1.0e-4F);
        EXPECT_NEAR(world.worldPosition.y, 0.5F, 1.0e-4F);
        // A reusable point is not spent.
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn).enabled);

        ASSERT_TRUE(editor.StopPlayModeUVE());
        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_SpawnPointWithNoPlayerJustPlays) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_noop_a.uvescene", 100U,
                         &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // A spawn point with no player to absorb it: play still enters, and the point keeps its
        // one-shot loaded - resolution is defined as "nothing to do", never a failure.
        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, spawn, Scene::TransformComponentUVE{});
        Scene::SpawnPoint3DNodeComponentUVE spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn, spawnPoint);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::SpawnPoint3DNodeComponentUVE>(spawn).enabled);
        ASSERT_TRUE(editor.StopPlayModeUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_PlayerWithNoSpawnPointKeepsItsAuthoredPose) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_noop_b.uvescene", 100U,
                         &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // The inverse of the previous test, in a document of its own: a player with nothing to
        // spawn at keeps its authored pose through the whole sandbox cycle.
        const Scene::EntityUVE player = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE authored{};
        authored.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, player, authored);
        entityManager.AddComponentUVE<Scene::CharacterControllerComponentUVE>(
            player, Scene::CharacterControllerComponentUVE{});
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        const Scene::TransformComponentUVE& during =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(player);
        EXPECT_EQ(during.localPosition.x, 1.0F);
        EXPECT_EQ(during.localPosition.y, 2.0F);
        ASSERT_TRUE(editor.StopPlayModeUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ViewportBookmarks_StoreRestoreClearAndRejectBadInput) {
    // The session bookmark store itself: Unreal's Ctrl+digit/digit slots as editor-owned
    // transient state - isolated per slot, validated on the way in, honest about what is not
    // inside it (no document coupling, no scene dirty flag touched).
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_bookmarks.uvescene", 100U, &engine);
        editor.InitUVE();

        // An untouched slot answers no value, and every slot index out of range fails closed.
        EXPECT_FALSE(editor.GetViewportBookmarkUVE(0U).has_value());
        EXPECT_FALSE(editor.GetViewportBookmarkUVE(Editor::kEditorViewportBookmarkSlotCountUVE).has_value());
        EXPECT_FALSE(editor.ClearViewportBookmarkUVE(Editor::kEditorViewportBookmarkSlotCountUVE));

        const Editor::EditorViewportBookmarkUVE first{
            Math::Vector3UVE{1.0F, 2.0F, 3.0F}, 0.4F, -0.2F, 7.5F};
        ASSERT_TRUE(editor.SetViewportBookmarkUVE(0U, first));
        const Editor::EditorViewportBookmarkUVE other{
            Math::Vector3UVE{-8.0F, 0.0F, 2.0F}, 2.1F, 0.9F, 12.0F};
        ASSERT_TRUE(editor.SetViewportBookmarkUVE(Editor::kEditorViewportBookmarkSlotCountUVE - 1U, other));

        // Round trip is exact - the store must not smear floats while they are only passing through.
        const std::optional<Editor::EditorViewportBookmarkUVE> restored =
            editor.GetViewportBookmarkUVE(0U);
        ASSERT_TRUE(restored.has_value());
        EXPECT_EQ(restored->target.x, 1.0F);
        EXPECT_EQ(restored->yawRadians, 0.4F);
        EXPECT_EQ(restored->pitchRadians, -0.2F);
        EXPECT_EQ(restored->distance, 7.5F);
        // Slots stay independent; clearing an occupied slot reports it, an empty one does not.
        ASSERT_TRUE(editor.GetViewportBookmarkUVE(9U).has_value());
        EXPECT_TRUE(editor.ClearViewportBookmarkUVE(0U));
        EXPECT_FALSE(editor.GetViewportBookmarkUVE(0U).has_value());
        EXPECT_TRUE(editor.GetViewportBookmarkUVE(9U).has_value());
        EXPECT_FALSE(editor.ClearViewportBookmarkUVE(1U));

        // Storing over an occupied slot replaces it.
        ASSERT_TRUE(editor.SetViewportBookmarkUVE(9U, first));
        EXPECT_EQ(editor.GetViewportBookmarkUVE(9U)->target.x, 1.0F);

        // Garbage in, nothing stored: out-of-range slot, NaN, and a non-positive distance.
        EXPECT_FALSE(editor.SetViewportBookmarkUVE(10U, first));
        EXPECT_FALSE(editor.SetViewportBookmarkUVE(
            1U, Editor::EditorViewportBookmarkUVE{
                    Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                    0.0F, 0.0F, 5.0F}));
        EXPECT_FALSE(editor.SetViewportBookmarkUVE(
            1U, Editor::EditorViewportBookmarkUVE{{}, 0.0F, 0.0F, 0.0F}));
        EXPECT_FALSE(editor.GetViewportBookmarkUVE(1U).has_value());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, Marker3DFocusBookmark_FliesTheCameraIntoTheMarkerViewpoint) {
    // ComposeMarker3DFocusBookmarkUVE is the live consumer that separates UVE's Marker3D from
    // Godot's inert annotation: the marker's authored offset+rotation compose under the node's
    // world pose, the eye lands exactly ON the marker looking along its composed -Z, and the
    // orbit inverse then hands back a target/yaw/pitch the viewport camera can hold verbatim -
    // measured here by running the camera's own forward formula back to the eye (round trip).
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_marker_focus.uvescene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // Marker node at (3,1,-2), yawed 90 degrees about Y (-Z faces -X), local offset (0,2,4).
        const Scene::EntityUVE markerNode = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE markerTransform{};
        markerTransform.localPosition = Math::Vector3UVE{3.0F, 1.0F, -2.0F};
        markerTransform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};
        AttachRootUVE(engine, markerNode, markerTransform);
        Scene::Marker3DNodeComponentUVE marker{};
        marker.markerName = "Boss view";
        marker.localPosition = Math::Vector3UVE{0.0F, 2.0F, 4.0F};
        entityManager.AddComponentUVE<Scene::Marker3DNodeComponentUVE>(markerNode, marker);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        const std::optional<Editor::EditorViewportBookmarkUVE> bookmark =
            editor.ComposeMarker3DFocusBookmarkUVE(markerNode);
        ASSERT_TRUE(bookmark.has_value());
        // Composed eye: rotation maps (0,2,4) to (4,2,0) about Y with sin90, added to the node.
        const float expectedEyeX = 3.0F + 4.0F;
        const float expectedEyeY = 1.0F + 2.0F;
        const float expectedEyeZ = -2.0F;
        // Composed forward: -Z rotated 90 degrees about Y points along -X.
        // The camera convention: eye = target + offset(yaw,pitch) * distance with
        // offset=(cosy*cosp, sinp, siny*cosp) - run it forward and require the eye back.
        const float cosPitch = std::cos(bookmark->pitchRadians);
        const Math::Vector3UVE offset{
            std::cos(bookmark->yawRadians) * cosPitch,
            std::sin(bookmark->pitchRadians),
            std::sin(bookmark->yawRadians) * cosPitch,
        };
        const Math::Vector3UVE roundTripEye =
            bookmark->target + offset * bookmark->distance;
        EXPECT_NEAR(roundTripEye.x, expectedEyeX, 1.0e-4F);
        EXPECT_NEAR(roundTripEye.y, expectedEyeY, 1.0e-4F);
        EXPECT_NEAR(roundTripEye.z, expectedEyeZ, 1.0e-4F);
        EXPECT_NEAR(bookmark->distance, Editor::kEditorMarkerFocusDistanceUVE, 1.0e-6F);
        // And the view direction itself is -X: target - eye points along composed -Z.
        const Math::Vector3UVE viewDirection =
            (bookmark->target - roundTripEye) * (1.0F / Editor::kEditorMarkerFocusDistanceUVE);
        EXPECT_NEAR(viewDirection.x, -1.0F, 1.0e-4F);
        EXPECT_NEAR(viewDirection.y, 0.0F, 1.0e-4F);
        EXPECT_NEAR(viewDirection.z, 0.0F, 1.0e-4F);

        // A disabled or invalid marker, or a plain entity, composes nothing - fail-closed.
        entityManager.GetComponentUVE<Scene::Marker3DNodeComponentUVE>(markerNode).enabled = false;
        EXPECT_FALSE(editor.ComposeMarker3DFocusBookmarkUVE(markerNode).has_value());
        const Scene::EntityUVE plain = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, plain, Scene::TransformComponentUVE{});
        EXPECT_FALSE(editor.ComposeMarker3DFocusBookmarkUVE(plain).has_value());

        // Plain-entity focus still answers the authored world position when a transform exists.
        Scene::TransformComponentUVE plainTransform{};
        plainTransform.localPosition = Math::Vector3UVE{-4.0F, 0.5F, 8.0F};
        services.GetSceneGraphUVE().SetLocalTransformUVE(entityManager, plain, plainTransform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const std::optional<Math::Vector3UVE> focusTarget =
            editor.ResolveEntityFocusTargetUVE(plain);
        ASSERT_TRUE(focusTarget.has_value());
        EXPECT_NEAR(focusTarget->x, -4.0F, 1.0e-5F);
        EXPECT_NEAR(focusTarget->y, 0.5F, 1.0e-5F);
        EXPECT_NEAR(focusTarget->z, 8.0F, 1.0e-5F);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, OrbitBookmarkInverse_RoundTripsTheCameraEyeAndGuardsThePoles) {
    // The pure inverse: craft any in-range yaw/pitch, build the camera's own offset formula
    // forward from it, give that eye+forward to ResolveOrbitBookmarkFromLookUVE, and require the
    // recovered pose reproduces the same eye through the same forward formula - the exact
    // measured round trip (identical claim style to SpringArm3D's sweep inverse, below 1e-4).
    const float distance = 6.0F;
    const float cases[][2] = {{0.0F, 0.0F}, {0.7553F, -0.4561F}, {-2.2F, 1.2F}, {3.0F, -1.55F}};
    for (const auto& yawPitch : cases) {
        const float cosPitch = std::cos(yawPitch[1]);
        const Math::Vector3UVE offset{
            std::cos(yawPitch[0]) * cosPitch, std::sin(yawPitch[1]),
            std::sin(yawPitch[0]) * cosPitch};
        const Math::Vector3UVE target{2.0F, -1.0F, 5.0F};
        const Math::Vector3UVE eye = target + offset * distance;
        const Math::Vector3UVE forward = offset * (-1.0F);
        const std::optional<Editor::EditorViewportBookmarkUVE> recovered =
            Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(eye, forward, distance);
        ASSERT_TRUE(recovered.has_value());
        const float recoveredCosPitch = std::cos(recovered->pitchRadians);
        const Math::Vector3UVE recoveredOffset{
            std::cos(recovered->yawRadians) * recoveredCosPitch,
            std::sin(recovered->pitchRadians),
            std::sin(recovered->yawRadians) * recoveredCosPitch};
        const Math::Vector3UVE recoveredEye =
            recovered->target + recoveredOffset * recovered->distance;
        EXPECT_NEAR(recoveredEye.x, eye.x, 1.0e-4F) << "yaw in case: " << yawPitch[0];
        EXPECT_NEAR(recoveredEye.y, eye.y, 1.0e-4F) << "yaw in case: " << yawPitch[0];
        EXPECT_NEAR(recoveredEye.z, eye.z, 1.0e-4F) << "yaw in case: " << yawPitch[0];
    }

    // The poles: a straight-down look can only snap to the clamped pitch with yaw 0 (the
    // convention), and garbage never yields a pose at all.
    const std::optional<Editor::EditorViewportBookmarkUVE> straightDown =
        Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(
            {}, Math::Vector3UVE{0.0F, -1.0F, 0.0F}, distance);
    ASSERT_TRUE(straightDown.has_value());
    // The camera formula has offset.y = sin(pitch): a down-looking forward (-Y) means the eye
    // sits ABOVE the target, so the inverse of a -Y forward is pitch = +pi/2, not -pi/2. The
    // camera's own ~89-degree clamp is applied only when the pose is handed to it
    // (OrbitCamera::SetYawPitch), and yaw is clamped to 0 at the pole where it is unobservable.
    EXPECT_NEAR(straightDown->pitchRadians, std::numbers::pi_v<float> * 0.5F, 1.0e-6F);
    EXPECT_EQ(straightDown->yawRadians, 0.0F);
    // The opposite pole: a straight-up look inverts to pitch = -pi/2 (eye below the target).
    const std::optional<Editor::EditorViewportBookmarkUVE> straightUp =
        Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(
            {}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, distance);
    ASSERT_TRUE(straightUp.has_value());
    EXPECT_NEAR(straightUp->pitchRadians, -std::numbers::pi_v<float> * 0.5F, 1.0e-6F);
    EXPECT_EQ(straightUp->yawRadians, 0.0F);

    EXPECT_FALSE(Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(
                     {}, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, distance)
                     .has_value());
    EXPECT_FALSE(Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(
                     {}, Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                     distance)
                     .has_value());
    EXPECT_FALSE(Editor::EditorUVE::ResolveOrbitBookmarkFromLookUVE(
                     {}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -1.0F)
                     .has_value());
}

TEST(EditorUVETest, PlayModeSandbox_RestoresOrderedMultiSelectionAndActiveEntity) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_multi_selection.uvescene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, first, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, second, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);

        ASSERT_TRUE(editor.EnterPlayModeUVE());
        ASSERT_TRUE(editor.StopPlayModeUVE());

        const std::vector<Scene::EntityUVE> restoredRoots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(restoredRoots.size(), 3U); // the scene root + the two restored authored roots
        EXPECT_EQ(restoredRoots[0], editor.GetDocumentSceneRootUVE());
        EXPECT_NE(restoredRoots[1], first);
        EXPECT_NE(restoredRoots[2], second);
        // The restored selection is the authored pair (the two non-scene-root restored roots,
        // in their restored order); the scene root itself is never part of it.
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{restoredRoots[1], restoredRoots[2]}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), restoredRoots[2]);
        EXPECT_FALSE(editor.HasSingleDocumentSelectionUVE());

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_HandlesEmptyDocumentAndMissingControlSafely) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE withoutControl(engine.GetServicesUVE(), "uve_editor_tests_play_no_control.uvescene");
        withoutControl.InitUVE();
        EXPECT_FALSE(withoutControl.EnterPlayModeUVE());
        withoutControl.ShutdownUVE();

        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_empty.uvescene", 100U, &engine);
        editor.InitUVE();
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Edit);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentSceneRootUVE());
        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, VisualScriptSearchInsertionPreservesPositionAndCompilerUsesNativeGraph) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scripting_workspace.uvescene");
        editor.InitUVE();
        Scripting::ScriptGraphCanvasUVE& canvas = editor.GetVisualScriptCanvasUVE();
        const Scripting::ScriptGraphCanvasSnapshotUVE before = canvas.GetSnapshotUVE();
        ASSERT_TRUE(std::any_of(
            before.paletteDescriptors.cbegin(), before.paletteDescriptors.cend(),
            [](const Scripting::ScriptGraphCanvasPaletteEntryUVE& entry) { return entry.typeId == "engine.log"; }));

        const Scripting::ScriptGraphCanvasPointUVE insertionPosition{-37.5F, 82.25F};
        const auto addResult = canvas.AddNodeTypeUVE("engine.log", insertionPosition, before.revision);
        ASSERT_TRUE(addResult.IsAppliedUVE());
        const Scripting::ScriptGraphCanvasSnapshotUVE after = canvas.GetSnapshotUVE();
        ASSERT_EQ(after.nodes.size(), 1U);
        EXPECT_EQ(after.nodes.front().typeId, "engine.log");
        EXPECT_EQ(after.nodes.front().position, insertionPosition);
        EXPECT_EQ(after.revision, addResult.revision);

        EditorUVEAccessUVE::CompileVisualScriptUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsVisualScriptCompileSuccessfulUVE(editor));
        EXPECT_EQ(EditorUVEAccessUVE::GetLastCompiledVisualScriptGraphRevisionUVE(editor), after.graphRevision);
        EXPECT_GT(EditorUVEAccessUVE::GetVisualScriptCompileInstructionCountUVE(editor), 0U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}



// ---------------------------------------------------------------------------------------------
// Transform gestures.
//
// A pointer drag is one transaction, not a stream of commands. These pin the properties that
// distinguish the two - a single undo step per drag, previews measured from where the drag began,
// and a cancel that refuses to write a stale baseline over someone else's change.
// ---------------------------------------------------------------------------------------------

/// One selected root entity ready to be dragged, with `editor` already pointing at it.
[[nodiscard]] Scene::EntityUVE SelectFreshRootUVE(Core::EngineCoreUVE& engine, EditorUVE& editor,
                                                  const Math::Vector3UVE position) {
    Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform{};
    transform.localPosition = position;
    AttachRootUVE(engine, entity, transform);
    engine.GetServicesUVE().GetSceneGraphUVE().UpdateUVE(entityManager);
    editor.SelectEntityUVE(entity);
    return entity;
}

TEST(EditorUVETest, TransformGesture_ManyPreviewsCollapseIntoExactlyOneUndoStep) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_commit.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = SelectFreshRootUVE(engine, editor, Math::Vector3UVE{});

        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        EXPECT_EQ(editor.GetToolSessionPhaseUVE(), EditorToolSessionPhaseUVE::Previewing);

        // A drag reports its TOTAL offset each frame. Feeding 1, 2, ... 10 must land on 10, not on
        // their sum - that difference is the whole reason previews run off the baseline.
        for (int step = 1; step <= 10; ++step) {
            ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X,
                                                          static_cast<float>(step)));
        }
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    10.0F, 0.0001F);

        ASSERT_TRUE(editor.CommitTransformGestureUVE());
        EXPECT_EQ(editor.GetToolSessionPhaseUVE(), EditorToolSessionPhaseUVE::Idle);
        EXPECT_EQ(editor.GetLastToolSessionOutcomeUVE(), EditorToolSessionOutcomeUVE::Committed);

        // Ten previews, one undo step: straight back to the start, and nothing left to undo after.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    0.0F, 0.0001F);
        EXPECT_FALSE(editor.UndoUVE());

        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    10.0F, 0.0001F);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, TransformGesture_CancelRestoresTheBaselineAndLeavesNoHistory) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_cancel.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity =
            SelectFreshRootUVE(engine, editor, Math::Vector3UVE{3.0F, 0.0F, 0.0F});
        const bool dirtyBeforeGesture = editor.IsSceneDirtyUVE();

        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X, 25.0F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    28.0F, 0.0001F);

        ASSERT_TRUE(editor.CancelTransformGestureUVE());
        EXPECT_EQ(editor.GetLastToolSessionOutcomeUVE(), EditorToolSessionOutcomeUVE::Cancelled);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    3.0F, 0.0001F);
        // An abandoned drag must not leave the document looking modified, or the user is prompted
        // to save a change they explicitly threw away.
        EXPECT_EQ(editor.IsSceneDirtyUVE(), dirtyBeforeGesture);
        EXPECT_FALSE(editor.UndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, TransformGesture_CancelRefusesToOverwriteAnExternalChange) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_conflict.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = SelectFreshRootUVE(engine, editor, Math::Vector3UVE{});

        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X, 5.0F));

        // Something other than this gesture moves the entity - another tool, a script, the
        // runtime. The baseline is now stale.
        Scene::TransformComponentUVE external{};
        external.localPosition = Math::Vector3UVE{-99.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(external));

        // Cancel must decline rather than silently restore over that change.
        EXPECT_FALSE(editor.CancelTransformGestureUVE());
        EXPECT_EQ(editor.GetLastToolSessionOutcomeUVE(),
                  EditorToolSessionOutcomeUVE::ExternalTransformConflict);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.x,
                    -99.0F, 0.0001F);
        EXPECT_EQ(editor.GetToolSessionPhaseUVE(), EditorToolSessionPhaseUVE::Idle);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, TransformGesture_RejectsMultiSelectionAndOutOfOrderCalls) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_guards.uvescene");
        editor.InitUVE();

        // Nothing is in flight, so preview/commit/cancel have nothing to act on.
        EXPECT_FALSE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X, 1.0F));
        EXPECT_FALSE(editor.CommitTransformGestureUVE());
        EXPECT_FALSE(editor.CancelTransformGestureUVE());

        const Scene::EntityUVE first = SelectFreshRootUVE(engine, editor, Math::Vector3UVE{});
        const Scene::EntityUVE second =
            SelectFreshRootUVE(engine, editor, Math::Vector3UVE{5.0F, 0.0F, 0.0F});
        ASSERT_NE(first, second);

        // Two entities selected: a transform gesture has no single pivot to act on, matching the
        // existing single-selection rule the four axis commands already enforce.
        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        EXPECT_FALSE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));

        // Back to one, and a re-entrant Begin is refused without disturbing the live session.
        editor.SelectEntityUVE(first);
        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Scale));
        EXPECT_FALSE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Rotate));
        EXPECT_EQ(editor.GetToolSessionPhaseUVE(), EditorToolSessionPhaseUVE::Previewing);

        // The mode captured at Begin is the one that applies, so this previews a SCALE even though
        // a rotate Begin was attempted in between.
        ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::Y, 1.5F));
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(first).localScale.y,
                    2.5F, 0.0001F);

        // The scale floor still rejects, and a rejected preview leaves the last good one standing.
        EXPECT_FALSE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::Y, -50.0F));
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(first).localScale.y,
                    2.5F, 0.0001F);
        ASSERT_TRUE(editor.CancelTransformGestureUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, TransformGesture_NoOpDragCommitsWithoutHistoryOrDirtyingTheScene) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_noop.uvescene");
        editor.InitUVE();
        static_cast<void>(SelectFreshRootUVE(engine, editor, Math::Vector3UVE{}));
        const bool dirtyBeforeGesture = editor.IsSceneDirtyUVE();

        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X, 0.0F));
        ASSERT_TRUE(editor.CommitTransformGestureUVE());

        EXPECT_EQ(editor.GetLastToolSessionOutcomeUVE(),
                  EditorToolSessionOutcomeUVE::CompletedWithoutChange);
        EXPECT_EQ(editor.IsSceneDirtyUVE(), dirtyBeforeGesture);
        EXPECT_FALSE(editor.UndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

// Snapping must mean the same thing on both paths, or a drag with Snap on would quantise
// differently from the keyboard command that nominally does the same operation.
TEST(EditorUVETest, TransformGesture_SnappingQuantisesIdenticallyToTheEquivalentCommand) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_snap.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        EditorTransformSnappingSettingsUVE snapping{};
        snapping.enabled = true;
        snapping.translateStep = 0.5F;
        ASSERT_TRUE(editor.SetTransformSnappingSettingsUVE(snapping));

        const Scene::EntityUVE viaCommand = SelectFreshRootUVE(engine, editor, Math::Vector3UVE{});
        ASSERT_TRUE(editor.TranslateSelectedAlongAxisUVE(EditorTransformAxisUVE::X, 1.31F));
        const float commandResult =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(viaCommand).localPosition.x;

        static_cast<void>(SelectFreshRootUVE(engine, editor, Math::Vector3UVE{}));
        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        ASSERT_TRUE(editor.PreviewTransformGestureUVE(EditorTransformAxisUVE::X, 1.31F));
        ASSERT_TRUE(editor.CommitTransformGestureUVE());
        const float gestureResult = entityManager
                                        .GetComponentUVE<Scene::TransformComponentUVE>(
                                            editor.GetSelectedEntityUVE())
                                        .localPosition.x;

        EXPECT_NEAR(gestureResult, commandResult, 1e-6F);
        EXPECT_NEAR(gestureResult, 1.5F, 1e-6F);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests

namespace UVE::Editor::Tests {
namespace {

TEST(EditorUVETest, SceneRootInspectorUVE_ShowsExactlyTheNodeSectionInOrder) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scene_root_inspector.uvescene");
        editor.InitUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        // No Name, Hierarchy or Transform; Thread Group lives inside Process.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, root),
                  (std::vector<std::string>{"process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "node-metadata"}));

        // On a node without Process, Thread Group still has a section of its own.
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE node = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, node, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ThreadGroupComponentUVE>(node, Scene::ThreadGroupComponentUVE{});
        const std::vector<std::string> ids = EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, node);
        EXPECT_NE(std::find(ids.begin(), ids.end(), "thread-group"), ids.end());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScriptSlotUVE_AddNewCppCreatesALinkedScriptTitledWithTheNode) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_script_slot.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("main"));

        ASSERT_TRUE(editor.CreateScriptForSelectedEntityUVE());
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath;
        EXPECT_EQ(path.rfind("scripts/main", 0), 0U);
        EXPECT_TRUE(editor.DescribeScriptAssetProblemUVE(path).empty()); // A real, decodable script file.
        EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        const auto nodes = editor.GetVisualScriptCanvasUVE().GetSnapshotUVE().nodes;
        ASSERT_EQ(nodes.size(), 1U);
        EXPECT_EQ(nodes.front().typeId, "scene.self");
        EXPECT_TRUE(nodes.front().pins.empty());
        EXPECT_EQ(EditorUVEAccessUVE::GetScriptNodeTitleUVE(editor), "main");

        // Connected: renaming the node renames it on the canvas.
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("game"));
        EXPECT_EQ(EditorUVEAccessUVE::GetScriptNodeTitleUVE(editor), "game");

        // A second Add is refused while the slot is filled; Clear then Load puts it back.
        EXPECT_FALSE(editor.CreateScriptForSelectedEntityUVE());
        ASSERT_TRUE(editor.AssignScriptToSelectedEntityUVE({}));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath.empty());
        EXPECT_FALSE(editor.AssignScriptToSelectedEntityUVE("scripts/missing.uvescript"));
        EXPECT_FALSE(editor.AssignScriptToSelectedEntityUVE("../escape.uvescript"));
        ASSERT_TRUE(editor.AssignScriptToSelectedEntityUVE(path));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath.empty());

        std::error_code error;
        std::filesystem::remove(path, error);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, NodeMetadataUVE_EveryEditIsOneUndoStep) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_node_metadata.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        editor.SelectEntityUVE(root);
        const auto entries = [&] { return entityManager.GetComponentUVE<Scene::NodeMetadataComponentUVE>(root).entries; };

        EXPECT_FALSE(editor.AddSelectedNodeMetadataUVE("2bad", Core::VariantUVE::MakeIntUVE(1)));
        ASSERT_TRUE(editor.AddSelectedNodeMetadataUVE("charges", Core::VariantUVE::MakeFloatUVE(3.5)));
        EXPECT_FALSE(editor.AddSelectedNodeMetadataUVE("charges", Core::VariantUVE::MakeIntUVE(1)));
        ASSERT_TRUE(editor.RenameSelectedNodeMetadataUVE("charges", "ammo"));
        // 3.5 -> int loses the fraction: refused unless the author confirms.
        EXPECT_FALSE(editor.ChangeSelectedNodeMetadataTypeUVE("ammo", Core::VariantTypeUVE::Int, false));
        ASSERT_TRUE(editor.ChangeSelectedNodeMetadataTypeUVE("ammo", Core::VariantTypeUVE::Int, true));
        EXPECT_EQ(entries().front().value.GetTypeUVE(), Core::VariantTypeUVE::Int);
        ASSERT_TRUE(editor.RemoveSelectedNodeMetadataUVE("ammo"));
        EXPECT_TRUE(entries().empty());

        ASSERT_TRUE(editor.UndoUVE()); // remove
        EXPECT_EQ(entries().front().value.GetTypeUVE(), Core::VariantTypeUVE::Int);
        ASSERT_TRUE(editor.UndoUVE()); // retype
        EXPECT_EQ(entries().front().value, Core::VariantUVE::MakeFloatUVE(3.5));
        ASSERT_TRUE(editor.UndoUVE()); // rename
        EXPECT_EQ(entries().front().key, "charges");
        ASSERT_TRUE(editor.UndoUVE()); // add
        EXPECT_TRUE(entries().empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, RenderInstanceChildInspectorUVE_IsOwnSectionThenBasesThenNode3DThenNode) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_render_instance_inspector.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE decal = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, decal, Scene::TransformComponentUVE{});
        Scene::ApplyDecal3DNodeDefinitionUVE(entityManager, decal, Scene::Decal3DNodeDefinitionUVE{});
        const Scene::EntityUVE fog = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, fog, Scene::TransformComponentUVE{});
        Scene::ApplyFogVolume3DNodeDefinitionUVE(entityManager, fog, Scene::FogVolume3DNodeDefinitionUVE{});
        const auto expected = [](const std::string& own) {
            return std::vector<std::string>{own,       "render-instance",       "transform",      "visibility",
                                            "process", "physics-interpolation", "auto-translate", "editor-description",
                                            "script",  "node-metadata"};
        };
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, decal), expected("decal-3d"));
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, fog), expected("fog-volume-3d"));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SurfaceInstanceChildInspectorUVE_IsOwnSectionThenSurfaceRenderNode3DNode) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_surface_instance_inspector.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const auto create = [&](const auto& apply) {
            const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
            AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
            apply(entity);
            return entity;
        };
        const auto expected = [](const std::string& own) {
            return std::vector<std::string>{own,
                                            "surface-instance",
                                            "render-instance",
                                            "transform",
                                            "visibility",
                                            "process",
                                            "physics-interpolation",
                                            "auto-translate",
                                            "editor-description",
                                            "script",
                                            "node-metadata"};
        };
        const Scene::EntityUVE mesh = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyMeshInstance3DNodeDefinitionUVE(entityManager, entity, Scene::MeshInstance3DNodeDefinitionUVE{});
        });
        const Scene::EntityUVE box = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyBoxMesh3DNodeDefinitionUVE(entityManager, entity, Scene::BoxMesh3DNodeDefinitionUVE{});
        });
        const Scene::EntityUVE sphere = create([&](const Scene::EntityUVE entity) {
            Scene::ApplySphereMesh3DNodeDefinitionUVE(entityManager, entity, Scene::SphereMesh3DNodeDefinitionUVE{});
        });
        const Scene::EntityUVE plane = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyPlaneMesh3DNodeDefinitionUVE(entityManager, entity, Scene::PlaneMesh3DNodeDefinitionUVE{});
        });
        const Scene::EntityUVE particles = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyParticleEmitter3DNodeDefinitionUVE(entityManager, entity,
                                                           Scene::ParticleEmitter3DNodeDefinitionUVE{});
        });
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, mesh), expected("mesh"));
        // The primitive's collision is drawn inside its own section, not as a section of its own.
        for (const Scene::EntityUVE primitive : {box, sphere, plane}) {
            EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(primitive));
            EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, primitive),
                      expected("primitive-mesh"));
        }
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, particles),
                  expected("particle-emitter"));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, CharacterBodyInspectorUVE_IsItsChainToTheRootAndNothingElse) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_character_body_inspector.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE body = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, body, Scene::TransformComponentUVE{});
        Scene::ApplyCharacterBody3DNodeDefinitionUVE(entityManager, body, Scene::CharacterBody3DNodeDefinitionUVE{});
        // CharacterBody3D > SolidBody3D > PhysicsObject3D > Node3D > Node. The capsule, layer and mask
        // are drawn inside PhysicsObject3D, so the collider has no section of its own.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, body),
                  (std::vector<std::string>{"character-controller", "solid-body", "physics-object", "transform",
                                            "visibility", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "node-metadata"}));
        // A primitive still draws its collider in its own section, not in PhysicsObject3D's.
        const Scene::EntityUVE box = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, box, Scene::TransformComponentUVE{});
        Scene::ApplyBoxMesh3DNodeDefinitionUVE(entityManager, box, Scene::BoxMesh3DNodeDefinitionUVE{});
        const std::vector<std::string> boxIds = EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, box);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "collider"), 0);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "physics-object"), 0);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AnimationNodesUVE_InspectorIsTheirOwnSectionThenTheNodeSection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_animation_inspector.uvescene");
        editor.InitUVE();
        const Scene::EntityUVE player =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AnimationPlayer);
        const Scene::EntityUVE tree = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AnimationTree);
        ASSERT_NE(player, Scene::kInvalidEntityUVE);
        ASSERT_NE(tree, Scene::kInvalidEntityUVE);
        // AnimationPlayer > Node, exactly like the scene root: no Transform, no Visibility.
        const std::vector<std::string> nodeSection{"process", "physics-interpolation", "auto-translate",
                                                   "editor-description", "script", "node-metadata"};
        std::vector<std::string> expected{"animation-player"};
        expected.insert(expected.end(), nodeSection.begin(), nodeSection.end());
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, player), expected);
        expected.front() = "animation-tree";
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, tree), expected);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AnimationNodesUVE_APureNodeReparentsAndUndoes) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_animation_reparent.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE player =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AnimationPlayer);
        editor.SelectEntityUVE(editor.GetDocumentSceneRootUVE());
        const Scene::EntityUVE door = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Node3D);
        ASSERT_NE(door, Scene::kInvalidEntityUVE);
        ASSERT_NE(player, Scene::kInvalidEntityUVE);
        const Scene::EntityUVE rootParent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(player).parent;

        editor.SelectEntityUVE(player);
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(door));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(player).parent, door);
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(player));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(player).parent, rootParent);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(player).parent, door);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ColorPickerSessionUVE_ManyLiveChangesAreOneUndoStep) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_color_session.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE light = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, light, Scene::TransformComponentUVE{});
        Scene::LightComponentUVE authored{};
        authored.color = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(light, authored);
        const Scene::EntityUVE other = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, other, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(other, authored);
        editor.SelectEntityUVE(light);

        const Core::TypeMetadataEntryUVE* const entry =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::LightComponentUVE)));
        ASSERT_NE(entry, nullptr);
        const auto colorProperty = std::find_if(entry->properties.begin(), entry->properties.end(),
                                                [](const auto& candidate) { return candidate.name == "color"; });
        ASSERT_NE(colorProperty, entry->properties.end());
        const auto liveColor = [&](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::LightComponentUVE>(entity).color;
        };

        // A drag across the picker: every step is visible at once, none of them is history.
        EXPECT_FALSE(editor.CanUndoUVE());
        for (int step = 1; step <= 30; ++step) {
            const Math::Vector3UVE value{1.0F, 1.0F - (static_cast<float>(step) / 60.0F), 0.2F};
            ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &value));
            EXPECT_FLOAT_EQ(liveColor(light).y, value.y);
        }
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        // Closing the picker records the whole session as one step.
        ASSERT_TRUE(EditorUVEAccessUVE::CommitComponentPropertyPreviewUVE(editor));
        EXPECT_FLOAT_EQ(liveColor(light).y, 0.5F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(liveColor(light).x, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).y, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).z, 1.0F);
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FLOAT_EQ(liveColor(light).y, 0.5F);
        ASSERT_TRUE(editor.UndoUVE());

        // Cancel puts the colour back, with no history and the scene as clean as it was.
        const Math::Vector3UVE red{1.0F, 0.0F, 0.0F};
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(EditorUVEAccessUVE::CancelComponentPropertyPreviewUVE(editor));
        EXPECT_FLOAT_EQ(liveColor(light).y, 1.0F);
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(EditorUVEAccessUVE::CommitComponentPropertyPreviewUVE(editor));

        // A session that ends where it began records nothing.
        const Math::Vector3UVE white{1.0F, 1.0F, 1.0F};
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &white));
        ASSERT_TRUE(EditorUVEAccessUVE::CommitComponentPropertyPreviewUVE(editor));
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        // Undo in the middle of a session finishes it first, then takes it back.
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(liveColor(light).y, 1.0F);
        EXPECT_FALSE(editor.CanUndoUVE());

        // Moving to another node while a session is open finishes it on the node it began on.
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &red));
        editor.SelectEntityUVE(other);
        const Math::Vector3UVE blue{0.0F, 0.0F, 1.0F};
        ASSERT_TRUE(EditorUVEAccessUVE::PreviewSelectedComponentPropertyUVE(editor, *entry, *colorProperty, &blue));
        ASSERT_TRUE(EditorUVEAccessUVE::CommitComponentPropertyPreviewUVE(editor));
        EXPECT_FLOAT_EQ(liveColor(light).y, 0.0F);
        EXPECT_FLOAT_EQ(liveColor(other).z, 1.0F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(liveColor(other).x, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).y, 0.0F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(liveColor(light).y, 1.0F);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ColorPickerPreferencesUVE_SanitiseAndPersistAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_color_prefs.uvescene");
        editor.InitUVE();
        EXPECT_TRUE(editor.GetColorPickerPreferencesUVE().advancedOpen);
        EXPECT_TRUE(editor.GetColorPickerPreferencesUVE().saved.empty());

        Editor::ColorPickerPreferencesUVE preferences;
        preferences.advancedOpen = false;
        preferences.saved = {Editor::EditorColorUVE{1.0F, 0.5F, 0.0F, 1.0F},
                             Editor::EditorColorUVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F},
                             Editor::EditorColorUVE{2.0F, -1.0F, 0.25F, 0.5F}};
        for (int index = 0; index < 15; ++index) {
            preferences.recents.push_back(Editor::EditorColorUVE{static_cast<float>(index) / 15.0F, 0.0F, 0.0F, 1.0F});
        }
        editor.SetColorPickerPreferencesUVE(preferences);
        // The NaN colour is dropped, the out-of-range one clamped, and the recents trimmed.
        ASSERT_EQ(editor.GetColorPickerPreferencesUVE().saved.size(), 2U);
        EXPECT_EQ(Editor::FormatColorHexUVE(editor.GetColorPickerPreferencesUVE().saved[1], true), "#FF004080");
        EXPECT_EQ(editor.GetColorPickerPreferencesUVE().recents.size(), Editor::kMaxRecentColorsUVE);
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_color_prefs_reload.uvescene");
        reloaded.InitUVE();
        const Editor::ColorPickerPreferencesUVE& preferences = reloaded.GetColorPickerPreferencesUVE();
        EXPECT_FALSE(preferences.advancedOpen);
        ASSERT_EQ(preferences.saved.size(), 2U);
        EXPECT_EQ(Editor::FormatColorHexUVE(preferences.saved[0], true), "#FF8000FF");
        EXPECT_EQ(Editor::FormatColorHexUVE(preferences.saved[1], true), "#FF004080");
        EXPECT_EQ(preferences.recents.size(), Editor::kMaxRecentColorsUVE);
        reloaded.ShutdownUVE();
    }
    // A stored entry that is not a colour is skipped; the rest of the list still loads.
    engine.GetServicesUVE().GetConfigManagerUVE().SetStringUVE("editor.colorPicker.saved.0", "not a colour");
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_color_prefs_corrupt.uvescene");
        corrupt.InitUVE();
        ASSERT_EQ(corrupt.GetColorPickerPreferencesUVE().saved.size(), 1U);
        EXPECT_EQ(Editor::FormatColorHexUVE(corrupt.GetColorPickerPreferencesUVE().saved[0], true), "#FF004080");
        corrupt.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, InspectorTransformDragUVE_IsOneUndoStepAndCanBeCancelled) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_transform_drag.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE node = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, node, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(node);
        const auto live = [&] { return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(node); };

        // No gesture in flight: nothing to preview into.
        Scene::TransformComponentUVE moved = live();
        moved.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        EXPECT_FALSE(editor.PreviewTransformGestureValueUVE(moved));

        // Dragging Position.x from 0 to 3: every frame shows, one undo step results.
        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        for (int frame = 1; frame <= 30; ++frame) {
            moved.localPosition.x = static_cast<float>(frame) * 0.1F;
            ASSERT_TRUE(editor.PreviewTransformGestureValueUVE(moved));
            EXPECT_FLOAT_EQ(live().localPosition.x, moved.localPosition.x);
        }
        // A non-finite value is refused and the last good one stays.
        Scene::TransformComponentUVE broken = moved;
        broken.localScale.y = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.PreviewTransformGestureValueUVE(broken));
        EXPECT_FLOAT_EQ(live().localScale.y, 1.0F);
        EXPECT_FALSE(editor.CanUndoUVE());
        ASSERT_TRUE(editor.CommitTransformGestureUVE());
        ASSERT_TRUE(editor.CanUndoUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(live().localPosition.x, 0.0F);
        EXPECT_FALSE(editor.CanUndoUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        // Cancelled: back where it started, no history.
        ASSERT_TRUE(editor.BeginTransformGestureUVE(EditorToolSessionModeUVE::Scale));
        Scene::TransformComponentUVE scaled = live();
        scaled.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        ASSERT_TRUE(editor.PreviewTransformGestureValueUVE(scaled));
        ASSERT_TRUE(editor.CancelTransformGestureUVE());
        EXPECT_FLOAT_EQ(live().localScale.x, 1.0F);
        EXPECT_FALSE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorMetadataDragUVE_IsOneUndoStepAndOnlyItsOwnEditIsCommitted) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_metadata_drag.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.AddSelectedNodeMetadataUVE("speed", Core::VariantUVE::MakeFloatUVE(1.0)));
        const auto speed = [&] {
            return entityManager.GetComponentUVE<Scene::NodeMetadataComponentUVE>(root).entries.front().value;
        };

        for (int frame = 1; frame <= 20; ++frame) {
            ASSERT_TRUE(editor.PreviewSelectedNodeMetadataValueUVE(
                "speed", Core::VariantUVE::MakeFloatUVE(1.0 + static_cast<double>(frame))));
        }
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(21.0));

        // Letting go of some other field does not end this drag.
        const Core::TypeMetadataEntryUVE* const light =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::LightComponentUVE)));
        ASSERT_NE(light, nullptr);
        EXPECT_FALSE(EditorUVEAccessUVE::CommitComponentPropertyPreviewForUVE(editor, *light, light->properties.front()));

        ASSERT_TRUE(EditorUVEAccessUVE::CommitComponentPropertyPreviewUVE(editor));
        ASSERT_TRUE(editor.UndoUVE()); // the whole drag
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(1.0));

        // A one-shot edit arriving while a drag is still in flight records the drag first, so
        // undo walks back through both in the order they happened.
        ASSERT_TRUE(editor.PreviewSelectedNodeMetadataValueUVE("speed", Core::VariantUVE::MakeFloatUVE(5.0)));
        ASSERT_TRUE(editor.SetSelectedNodeMetadataValueUVE("speed", Core::VariantUVE::MakeFloatUVE(9.0)));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(5.0));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(1.0));
        ASSERT_TRUE(editor.UndoUVE()); // the add
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::NodeMetadataComponentUVE>(root).entries.empty());
        EXPECT_FALSE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorPropertyEditUVE_RefusesAValueTheComponentRuleRejects) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_mesh_rule.uvescene");
        editor.InitUVE();
        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::MeshInstance3D);
        ASSERT_NE(entity, Scene::kInvalidEntityUVE);
        const Core::TypeMetadataEntryUVE* const entry =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::MeshComponentUVE)));
        ASSERT_NE(entry, nullptr);
        const auto property = [&](const std::string& name) {
            return *std::find_if(entry->properties.begin(), entry->properties.end(),
                                 [&](const auto& candidate) { return candidate.name == name; });
        };
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        // A material with no mesh has nothing to draw on: refused, and the component unchanged.
        const Asset::AssetGuidUVE material{0x77U};
        EXPECT_FALSE(EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("materialGuid"),
                                                                          &material));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity).materialGuid,
                  Asset::kInvalidAssetGuidUVE);
        // A mesh alone is fine: it is drawn with the built-in lit shader.
        const Asset::AssetGuidUVE mesh{0x55U};
        EXPECT_TRUE(EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("meshGuid"), &mesh));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity).meshGuid, mesh);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ModelSourcesUVE_AreImportedAutomaticallyOutsideTheContentFolder) {
    const std::filesystem::path root = "uve_editor_tests_model_auto_import_content";
    const std::filesystem::path derived = "uve_editor_tests_model_auto_import_derived";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(derived);
    std::filesystem::create_directories(root / "Models");
    {
        std::ofstream obj(root / "Models" / "tri.obj", std::ios::binary | std::ios::trunc);
        obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
        std::ofstream rig(root / "Models" / "rig.gltf", std::ios::binary | std::ios::trunc);
        rig << R"({"asset":{"version":"2.0"},"meshes":[{}],"skins":[{"joints":[0]}],"nodes":[{}]})";
        // Exported motion: one bone and a one-second take, nothing to draw.
        std::ofstream motion(root / "Models" / "strafe.fbx", std::ios::binary | std::ios::trunc);
        motion << R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
Objects:  {
	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	AnimationStack: 5000, "AnimStack::Strafe", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 5100, "AnimLayer::Base", "" {
	}
}
Connections:  {
	C: "OO",5100,5000
	C: "OO",3100,3000
	C: "OO",3000,0
}
)";
    }
    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    config.derivedArtifactCacheRootUVE = derived / "Import";
    config.projectChangeWatchPollIntervalSecondsUVE = 0.0;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_model_auto_import.uvescene");
        editor.InitUVE();
        const std::filesystem::path imported = EditorUVEAccessUVE::GetImportedModelPathUVE(editor, "Models/tri.obj");
        for (int frame = 0; frame < 8 && !std::filesystem::exists(imported); ++frame) {
            editor.TickUVE();
            engine.TickFrameUVE();
        }
        // Converted without any action, into derived data - the content folder holds only sources.
        EXPECT_TRUE(std::filesystem::exists(imported));
        EXPECT_TRUE(imported.generic_string().ends_with(
            "uve_editor_tests_model_auto_import_derived/Imported/Models/tri.obj.uvemodel"))
            << imported;
        for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
            EXPECT_NE(file.path().extension(), ".uvemodel") << file.path();
        }
        // Mesh plus bones reads as a model; a plain mesh does not.
        EXPECT_TRUE(EditorUVEAccessUVE::IsRiggedModelSourceUVE(editor, "Models/rig.gltf"));
        EXPECT_FALSE(EditorUVEAccessUVE::IsRiggedModelSourceUVE(editor, "Models/tri.obj"));
        // Motion with no mesh is an animation: described, and never sent to the mesh importer.
        const EditorModelSourceInfoUVE* const motion = EditorUVEAccessUVE::FindModelSourceInfoUVE(editor, "Models/strafe.fbx");
        ASSERT_NE(motion, nullptr);
        EXPECT_TRUE(motion->animationOnly);
        EXPECT_FALSE(motion->rigged);
        // Its bones are still a skeleton a Skeleton3D can take.
        EXPECT_TRUE(motion->hasSkeleton);
        EXPECT_EQ(motion->summary, "1 bone, 1 animation, 1.00 s");
        EXPECT_FALSE(EditorUVEAccessUVE::IsModelImportQueuedUVE(editor, "Models/strafe.fbx"));
        EXPECT_FALSE(std::filesystem::exists(EditorUVEAccessUVE::GetImportedModelPathUVE(editor, "Models/strafe.fbx")));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(derived);
}

TEST(EditorUVETest, Skeleton3DUVE_StartsEmptyAndTakesItsBonesFromARiggedModel) {
    const std::filesystem::path root = "uve_editor_tests_skeleton_content";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Characters");
    {
        std::ofstream rig(root / "Characters" / "hero.gltf", std::ios::binary | std::ios::trunc);
        rig << R"({"asset":{"version":"2.0"},"nodes":[{"name":"Armature","children":[1]},)"
               R"({"name":"Hips","translation":[0,1,0],"children":[2]},{"name":"Spine"}],"skins":[{"joints":[1,2]}]})";
        std::ofstream plain(root / "Characters" / "rock.gltf", std::ios::binary | std::ios::trunc);
        plain << R"({"asset":{"version":"2.0"},"nodes":[{}]})";
    }
    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    config.derivedArtifactCacheRootUVE = "uve_editor_tests_skeleton_derived/Import";
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_skeleton.uvescene");
        editor.InitUVE();
        editor.TickUVE(); // first project refresh
        const Scene::EntityUVE skeleton = editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Skeleton3D);
        ASSERT_NE(skeleton, Scene::kInvalidEntityUVE);
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        // A Node3D child, empty until pointed at a rig, and named for what it is.
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(skeleton).name, "Skeleton3D");
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton).bones.empty());
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, skeleton),
                  (std::vector<std::string>{"skeleton-3d", "transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "node-metadata"}));

        // A model with no armature is refused and the node stays as it was.
        EXPECT_FALSE(EditorUVEAccessUVE::BindSelectedSkeletonSourceUVE(editor, "Characters/rock.gltf"));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton).bones.empty());

        ASSERT_TRUE(EditorUVEAccessUVE::BindSelectedSkeletonSourceUVE(editor, "Characters/hero.gltf"));
        const Scene::Skeleton3DNodeComponentUVE& bound =
            entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton);
        EXPECT_EQ(bound.skeletonAssetPath, "Characters/hero.gltf");
        ASSERT_EQ(bound.bones.size(), 2U);
        EXPECT_EQ(bound.bones[0].name, "Hips");
        EXPECT_EQ(bound.bones[1].parentIndex, 0);
        // One undo step takes the node back to empty.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton).bones.empty());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeleton).skeletonAssetPath.empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all("uve_editor_tests_skeleton_derived");
}

TEST(EditorUVETest, Node3DInspectorUVE_IsTransformVisibilityAndTheNodeSection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_node3d_inspector.uvescene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE node = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, node, Scene::TransformComponentUVE{});
        Scene::ApplyNode3DNodeDefinitionUVE(entityManager, node, Scene::Node3DNodeDefinitionUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, node),
                  (std::vector<std::string>{"transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "node-metadata"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
