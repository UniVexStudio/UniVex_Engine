// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

#include "scene_folder.h"
#include "scene_object_type.h"

#include "Support/test_scratch.h"

#include "editor_icon_set.h"

#include <GL/gl.h>

#include "animation_clip_asset.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "model_animations.h"
#include "skeleton_asset.h"
#include "texture_asset.h"
#include "engine_core.h"
#include "engine_project_settings.h"
#include "editor_settings.h"
#include "editor.h"
#include "camera_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "editor_internal_entity_component.h"
#include "hierarchy_component.h"
#include "thread_group_component.h"
#include "object_metadata_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "prefab_instance_component.h"
#include "primitive_mesh_component.h"
#include "script_component.h"
#include "skin_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "all_objects_3d.h"
#include "camera_3d.h"
#include "decal_3d.h"
#include "fog_volume_3d.h"
#include "marker_3d.h"
#include "skeleton_3d.h"
#include "world_transform_component.h"
#include "spawn_point_3d.h"
#include "scene_object_registry.h"
#include "scene_component_metadata.h"

namespace UVE::Editor::Tests {

struct EditorAccess final {
    // The folder new level objects go to (the Outliner allows objects only inside folders).
    [[nodiscard]] static Scene::Entity GetObjectFolder(Editor& editor) { return editor.ResolveObjectFolder(); }
    [[nodiscard]] static Scene::Entity GetViewport(Editor& editor) { return editor.GetDocumentViewport(); }
    /// A double-click's first stop: the row opens what it stands for, and only then may the
    /// Double-Click preference act.
    [[nodiscard]] static bool OpenHierarchyRowTarget(Editor& editor, const Scene::Entity entity) {
        return editor.OpenHierarchyRowTarget(entity);
    }

    [[nodiscard]] static std::string GetOutlinerTypeTag(const Editor& editor,
                                                            const Scene::Entity entity) {
        return editor.GetOutlinerTypeTag(entity);
    }

    [[nodiscard]] static std::vector<Scene::Entity> GetDocumentAncestry(const Editor& editor,
                                                                                const Scene::Entity entity) {
        return editor.GetDocumentAncestry(entity);
    }

    [[nodiscard]] static std::vector<Scene::Entity> GetEligibleReparentParents(Editor& editor,
                                                                                        const Scene::Entity entity) {
        return editor.GetEligibleReparentParents(entity);
    }

    [[nodiscard]] static bool HasInspectorDrawer(const Editor& editor, const std::string_view id) {
        return editor.m_inspectorDrawerRegistry.HasDrawer(id);
    }

    [[nodiscard]] static std::filesystem::path GetImportedModelPath(const Editor& editor,
                                                                        const std::filesystem::path& source) {
        return editor.GetImportedModelPath(source);
    }
    [[nodiscard]] static const EditorModelSourceInfo* FindModelSourceInfo(const Editor& editor,
                                                                                 const std::filesystem::path& source) {
        return editor.FindModelSourceInfo(source);
    }

    [[nodiscard]] static bool IsModelImportQueued(const Editor& editor, const std::string& source) {
        return editor.m_modelImportJobs.contains(source);
    }

    [[nodiscard]] static bool EntityClassFocusPending(const Editor& editor) {
        return editor.m_entityClassPicker.has_value() && editor.m_entityClassPicker->focusPending;
    }
    static void ClearEntityClassFocusPending(Editor& editor) {
        if (editor.m_entityClassPicker.has_value()) {
            editor.m_entityClassPicker->focusPending = false;
        }
    }
    [[nodiscard]] static bool& PreferencesFocusPending(Editor& editor) {
        return editor.m_preferencesWindow.focusPending;
    }
    [[nodiscard]] static bool& ProjectSettingsFocusPending(Editor& editor) {
        return editor.m_projectSettingsWindow.focusPending;
    }
    [[nodiscard]] static bool& InputMapFocusPending(Editor& editor) {
        return editor.m_inputMapWindow.focusPending;
    }
    [[nodiscard]] static bool& ShortcutsFocusPending(Editor& editor) {
        return editor.m_shortcutsWindow.focusPending;
    }

    [[nodiscard]] static bool IsRetargetPreviewActive(const Editor& editor) {
        return editor.m_retargetPreview.has_value();
    }
    static void TickRetargetWindow(Editor& editor) {
        editor.TickRetargetWindow();
    }
    [[nodiscard]] static std::vector<std::filesystem::path> RetargetWindowAnimations(const Editor& editor) {
        return editor.m_retargetWindow.has_value() ? editor.m_retargetWindow->animations : std::vector<std::filesystem::path>{};
    }
    [[nodiscard]] static std::filesystem::path RetargetWindowTarget(const Editor& editor) {
        return editor.m_retargetWindow.has_value() ? editor.m_retargetWindow->target : std::filesystem::path{};
    }
    static void RebuildRetargetPreview(Editor& editor, const RetargetPlan& plan, const std::filesystem::path& model) {
        editor.RebuildRetargetPreview(plan, model);
    }
    [[nodiscard]] static Scene::Entity GetRetargetSourceSkeleton(const Editor& editor) {
        return editor.m_retargetPreview.has_value() ? editor.m_retargetPreview->sourceSkeleton : Scene::kInvalidEntity;
    }
    [[nodiscard]] static std::size_t GetRetargetSourceColourCount(const Editor& editor) {
        return editor.m_retargetPreview.has_value() ? editor.m_retargetPreview->sourceColours.size() : 0U;
    }

    [[nodiscard]] static bool IsRiggedModelSource(const Editor& editor, const std::filesystem::path& source) {
        return editor.IsRiggedModelSource(source);
    }
    [[nodiscard]] static bool BindSelectedSkeletonSource(Editor& editor, const std::filesystem::path& source) {
        return editor.BindSelectedSkeletonSource(source);
    }
    [[nodiscard]] static bool SetSelectedComponentProperty(Editor& editor, const Core::TypeMetadataEntry& entry,
                                                              const Core::TypeMetadataProperty& property,
                                                              const void* value) {
        return editor.SetSelectedComponentProperty(entry, property, value);
    }
    [[nodiscard]] static bool PreviewSelectedComponentProperty(Editor& editor,
                                                                  const Core::TypeMetadataEntry& entry,
                                                                  const Core::TypeMetadataProperty& property,
                                                                  const void* value) {
        return editor.PreviewSelectedComponentProperty(entry, property, value);
    }
    [[nodiscard]] static bool CommitComponentPropertyPreview(Editor& editor) {
        return editor.CommitComponentPropertyPreview();
    }
    static bool CancelComponentPropertyPreview(Editor& editor) {
        return editor.CancelComponentPropertyPreview();
    }
    [[nodiscard]] static bool CommitComponentPropertyPreviewFor(Editor& editor,
                                                                   const Core::TypeMetadataEntry& entry,
                                                                   const Core::TypeMetadataProperty& property) {
        return editor.CommitComponentPropertyPreviewFor(entry, property);
    }
    [[nodiscard]] static std::vector<std::string> GetInspectorGroupHeaders(const Editor& editor,
                                                                             const Scene::Entity entity) {
        return editor.m_inspectorDrawerRegistry.GetEligibleGroupHeaders(entity);
    }
    [[nodiscard]] static std::vector<std::string> GetEligibleInspectorDrawerIds(const Editor& editor,
                                                                                  const Scene::Entity entity) {
        return editor.m_inspectorDrawerRegistry.GetEligibleDrawerIds(entity);
    }
    [[nodiscard]] static std::size_t GetInspectorDrawerCount(const Editor& editor) {
        return editor.m_inspectorDrawerRegistry.GetDrawerCount();
    }

    [[nodiscard]] static std::vector<std::string> GetEveryAssetTableTypeLabel() {
        using Type = Editor::AssetTableItemType;
        std::vector<std::string> labels;
        for (const Type type : {Type::Folder, Type::Scene, Type::Prefab, Type::Entity, Type::Bundle, Type::Mesh, Type::Model,
                                Type::Texture, Type::Shader, Type::Material, Type::Save, Type::Animation, Type::Script, Type::Audio,
                                Type::Font, Type::File}) {
            labels.emplace_back(Editor::GetAssetTableItemTypeLabel(type));
        }
        return labels;
    }

    [[nodiscard]] static std::string GetAssetTableItemTypeLabel(const Asset::ProjectFileEntry& entry) {
        return Editor::GetAssetTableItemTypeLabel(Editor::ClassifyAssetTableEntry(entry));
    }

    [[nodiscard]] static std::string GetAssetTableEntryTypeLabel(
        const Editor& editor, const Asset::ProjectFileEntry& entry) {
        return Editor::GetAssetTableItemTypeLabel(editor.GetAssetTableEntryType(entry));
    }

    static void SelectAssetTableMeshFocus(Editor& editor) {
        editor.m_assetTableTypeFocus = Editor::AssetTableTypeFocus::Mesh;
    }

    static void SelectAssetTableRegisteredFocus(Editor& editor) {
        editor.m_assetTableTypeFocus = Editor::AssetTableTypeFocus::Registered;
    }

    [[nodiscard]] static bool DoesAssetTableEntryMatchFocus(const Editor& editor,
                                                                     const Asset::ProjectFileEntry& entry) {
        return editor.DoesAssetTableEntryMatchFocus(entry);
    }

    static void SetAssetTableDirectory(Editor& editor, std::filesystem::path directory) {
        editor.m_assetTableDirectory = std::move(directory);
    }

    [[nodiscard]] static const std::filesystem::path& GetAssetTableDirectory(const Editor& editor) {
        return editor.m_assetTableDirectory;
    }

    static void SetAssetFilter(Editor& editor, std::string filter) { editor.m_assetFilter = std::move(filter); }

    [[nodiscard]] static std::string GetAssetTableTypeFocusLabel(const Editor& editor) {
        return Editor::GetAssetTableFocusLabel(editor.m_assetTableTypeFocus);
    }

    static void ReconcileAssetTableDirectory(Editor& editor, const Asset::ProjectFileSnapshot& snapshot) {
        editor.ReconcileAssetTableDirectory(snapshot);
    }

    static void LoadSessionSettings(Editor& editor) { editor.LoadSessionSettings(); }
    [[nodiscard]] static bool SaveSessionSettings(Editor& editor) { return editor.SaveSessionSettings(); }
    static void ApplyDefaultLayoutPreset(Editor& editor) {
        editor.ApplyLayoutPreset(Editor::EditorLayoutPreset::Default);
    }
    [[nodiscard]] static bool IsScenePanelVisible(const Editor& editor) noexcept { return editor.m_scenePanelVisible; }
    [[nodiscard]] static bool IsInspectorPanelVisible(const Editor& editor) noexcept { return editor.m_inspectorPanelVisible; }
    [[nodiscard]] static bool IsBottomDockVisible(const Editor& editor) noexcept { return editor.m_bottomDockVisible; }
    [[nodiscard]] static bool IsProjectPathFavorited(const Editor& editor,
                                                         const std::filesystem::path& relativePath) {
        return editor.IsProjectPathFavorited(relativePath);
    }
    [[nodiscard]] static ContentShelves& GetContentShelves(Editor& editor) { return editor.m_contentShelves; }
    [[nodiscard]] static std::filesystem::path GetSharedShelvesPath(const Editor& editor) {
        return editor.GetSharedShelvesPath();
    }
    static bool SaveSharedShelves(Editor& editor) { return editor.SaveSharedShelves(); }
    static void ReloadSharedShelvesIfChanged(Editor& editor) { editor.ReloadSharedShelvesIfChanged(); }
    [[nodiscard]] static const std::string& GetContentStatusMessage(const Editor& editor) {
        return editor.m_contentStatusMessage;
    }
    static void ToggleProjectPathFavorite(Editor& editor, const std::filesystem::path& relativePath) {
        editor.ToggleProjectPathFavorite(relativePath);
    }
    [[nodiscard]] static std::uintptr_t GetTextureThumbnail(Editor& editor,
                                                                const std::filesystem::path& relativePath) {
        return editor.GetTextureThumbnail(relativePath);
    }
    [[nodiscard]] static std::size_t GetTextureThumbnailCacheSize(const Editor& editor) noexcept {
        return editor.m_textureThumbnailCache.size();
    }
    [[nodiscard]] static std::uintptr_t GetMeshThumbnail(Editor& editor,
                                                             const std::filesystem::path& relativePath) {
        return editor.GetMeshThumbnail(relativePath);
    }
    [[nodiscard]] static std::size_t GetMeshThumbnailCacheSize(const Editor& editor) noexcept {
        return editor.m_meshThumbnailCache.size();
    }
    [[nodiscard]] static std::uintptr_t GetMaterialThumbnail(Editor& editor,
                                                                 const std::filesystem::path& relativePath) {
        return editor.GetMaterialThumbnail(relativePath);
    }
    static void InvalidateMaterialThumbnailCache(Editor& editor, const std::filesystem::path& absolutePath) {
        editor.InvalidateMaterialThumbnailCache(absolutePath);
    }
    static void RefreshProjectFileIndex(Editor& editor) { editor.RefreshProjectFileIndex(); }

    [[nodiscard]] static Asset::AssetGuid PublishTextureSource(Editor& editor,
                                                                     const std::filesystem::path& source) {
        return editor.PublishTextureSource(source);
    }

    [[nodiscard]] static std::optional<Asset::AssetGuid> DeriveNormalMapForAlbedo(
        Editor& editor, const Asset::AssetGuid albedo) {
        return editor.DeriveNormalMapForAlbedo(albedo);
    }

    [[nodiscard]] static bool IsLibraryWorkspaceActive(const Editor& editor) noexcept {
        return editor.m_activeWorkspace == Editor::EditorWorkspace::Library;
    }
    static void ActivateGameWorkspace(Editor& editor) noexcept {
        editor.m_activeWorkspace = Editor::EditorWorkspace::Game;
    }
    [[nodiscard]] static bool IsScriptingWorkspaceActive(const Editor& editor) noexcept {
        return editor.m_activeWorkspace == Editor::EditorWorkspace::Scripting;
    }
};

namespace {

[[nodiscard]] Core::EngineConfig MakeEditorTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_editor_tests.log";
    config.settingsFilePath = "uve_editor_tests_settings.json";
    config.projectSettingsFilePath = "uve_editor_tests_project_settings.json";
    config.inputMapFilePath = "uve_editor_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_editor_tests_saves";
    config.shaderCachePath = "uve_editor_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

void AttachRoot(Core::EngineCore& engine, const Scene::Entity entity,
                   const Scene::TransformComponent& transform) {
    Core::EngineServices& services = engine.GetServices();
    services.GetSceneGraph().AttachTransform(services.GetEntityManager(), entity, transform);
}

// Level objects live inside a folder; puts loose test entities into the level's object folder.
Scene::Entity PutInObjectFolder(Core::EngineCore& engine, Editor& editor,
                                    std::initializer_list<Scene::Entity> entities) {
    Core::EngineServices& services = engine.GetServices();
    const Scene::Entity folder = EditorAccess::GetObjectFolder(editor);
    for (const Scene::Entity entity : entities) {
        services.GetSceneGraph().SetParent(services.GetEntityManager(), entity, folder);
    }
    return folder;
}

struct UnregisteredEditorLifecycleComponent final {
    int value = 0;
};

TEST(EditorTest, Init_StartsRunningWithEmptyDocumentRootsAndSupportsHeadlessLifecycle) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_lifecycle.uvscene");
        editor.Init();

        EXPECT_EQ(editor.GetState(), EditorState::Running);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());

        editor.Shutdown();
        EXPECT_EQ(editor.GetState(), EditorState::Shutdown);
    }

    engine.Shutdown();
}

TEST(EditorTest, OpenScriptGraphForEntity_NoScriptComponent_ReturnsFalse) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Editor editor(engine.GetServices(), "uve_editor_tests_open_script_graph_none.uvscene");
    editor.Init();
    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();

    const Scene::Entity entity = entityManager.CreateEntity();
    AttachRoot(engine, entity, Scene::TransformComponent{});

    EXPECT_FALSE(editor.OpenScriptGraphForEntity(entity));
    EXPECT_FALSE(EditorAccess::IsScriptingWorkspaceActive(editor));

    editor.Shutdown();
    engine.Shutdown();
}

TEST(EditorTest, Init_DoesNotCreateAutomaticPreviewLighting) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_no_preview_light.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        std::size_t lightCount = 0U;
        entityManager.ForEach<Scene::LightComponent>(
            [&lightCount](Scene::Entity, Scene::LightComponent&) { ++lightCount; });
        EXPECT_EQ(lightCount, 0U);
        std::size_t meshCount = 0U;
        entityManager.ForEach<Scene::MeshComponent>(
            [&meshCount](Scene::Entity, Scene::MeshComponent&) { ++meshCount; });
        entityManager.ForEach<Scene::PrimitiveMeshComponent>(
            [&meshCount](Scene::Entity, Scene::PrimitiveMeshComponent&) { ++meshCount; });
        EXPECT_EQ(meshCount, 0U);
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Tick();
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        lightCount = 0U;
        entityManager.ForEach<Scene::LightComponent>(
            [&lightCount](Scene::Entity, Scene::LightComponent&) { ++lightCount; });
        EXPECT_EQ(lightCount, 0U);
        meshCount = 0U;
        entityManager.ForEach<Scene::MeshComponent>(
            [&meshCount](Scene::Entity, Scene::MeshComponent&) { ++meshCount; });
        entityManager.ForEach<Scene::PrimitiveMeshComponent>(
            [&meshCount](Scene::Entity, Scene::PrimitiveMeshComponent&) { ++meshCount; });
        EXPECT_EQ(meshCount, 0U);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, TwoDCanvasState_IsEditorOnlyAndValidated) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_2d_canvas.uvscene");
        editor.Init();
        const Editor2DCanvasState initial = editor.Get2DCanvasState();
        EXPECT_FLOAT_EQ(initial.zoom, 0.36F);
        EXPECT_FLOAT_EQ(initial.pan.x, 0.0F);
        EXPECT_FLOAT_EQ(initial.pan.y, 0.0F);
        EXPECT_TRUE(initial.gridVisible);
        EXPECT_TRUE(initial.safeAreaVisible);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        EXPECT_TRUE(editor.Set2DCanvasZoom(1.25F));
        EXPECT_FLOAT_EQ(editor.Get2DCanvasState().zoom, 1.25F);
        EXPECT_FALSE(editor.Set2DCanvasZoom(0.0F));
        EXPECT_FALSE(editor.Set2DCanvasZoom(5.0F));
        EXPECT_FALSE(editor.Set2DCanvasZoom(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FLOAT_EQ(editor.Get2DCanvasState().zoom, 1.25F);

        editor.Reset2DCanvasView();
        const Editor2DCanvasState reset = editor.Get2DCanvasState();
        EXPECT_FLOAT_EQ(reset.zoom, 0.36F);
        EXPECT_FLOAT_EQ(reset.pan.x, 0.0F);
        EXPECT_FLOAT_EQ(reset.pan.y, 0.0F);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, RenderOverlay_HeadlessWorkspaceCompositionDoesNotMutateEditorState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_workspace_layout.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        const Scene::Entity root = services.GetEntityManager().CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        editor.RenderOverlay();

        EXPECT_EQ(editor.GetState(), EditorState::Running);
        EXPECT_EQ(editor.GetSelectedEntity(), root);
        ASSERT_EQ(editor.GetSelectedEntities().size(), 1U);
        EXPECT_EQ(editor.GetSelectedEntities().front(), root);
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, InspectorDrawerRegistration_IncludesStableHierarchyDrawer) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_hierarchy_drawer_registration.uvscene");
        // 27, not the original 22: every one of the twenty-two ids below still registers, and
        // five sections were added - Visibility, which never had one, plus the four the common
        // Object section gained (Process, Thread Group, Auto Translate, Metadata). None of those
        // five needed a line of Inspector code: the drawers are generated from what each
        // component declares (RegisterMetadataInspectorDrawers), so this count follows the
        // declarations rather than a hand-written registration list.
        // 27 before the three abstract 3D bases, each of which brings one section; 30 before the
        // old Name and Hierarchy drawers were removed and SurfaceInstance3D, LightEmitter3D,
        // Decal3D and FogVolume3D each brought one.
        // 33 with Skeleton3D's own section; 34 with SolidBody3D's; 36 with AnimationDriver's; 37 with
        // DirectionalLight3D's; 38 with Kinematic3D's - the only section that needed a real engine
        // mover behind it, so its drawers describe a body that actually moves; 39 with SpringArm3D's,
        // whose authored fields were serialized and validated for as long as they have existed but
        // had no drawer to reach them from; 40 with SpawnPoint3D's, the first gameplay-owned
        // section - its fields were also serialized and validated with nothing to author them;
        // 41 with RayCast3D's, whose exclusions are entity references and needed the reference-list
        // drawer rather than a typed row; 42 with Projectile3D's, whose authored fields (including
        // the hit policy and its two bounce coefficients) had the same problem - serialized and
        // validated with no drawer to reach them from; 44 with Hitbox3D's and Hurtbox3D's, the two
        // halves of a strike, which have to be authorable together or neither is authorable at all:
        // the pair is what the engine's scan reads, and the hitbox additionally shows the strike
        // count its own scan resolved; 45 with LODGroup3D's, the first section whose shape is a
        // prefix of two parallel arrays - a threshold chain and a mesh per level - so it needed two
        // list drawers rather than typed rows, and the level count that decides the prefix is a
        // field of the section itself; 46 with BoneAttachment3D's, whose section is the part of that
        // component that had been missing rather than the part that was merely unauthorable - its
        // fields were serialized, validated and named in the registry from the start, but nothing in
        // the engine read them, so the section arrives together with the system that resolves a bone
        // and writes the transform; 48 with NavMeshVolume3D's and NavSeeker3D's, the navigation
        // pair - the region's bake settings and the agent's schedule had been serialized and
        // validated with nothing to author them from, and the route the step publishes is declared
        // as runtime-only state, so the Inspector shows where an agent is going while playing
        // without ever offering to save it; 49 with TwoBoneIK3D's, the chain's three bone
        // references, its target and pole, and the answers one solve writes back; the current
        // The metadata-driven registry now includes the expanded navigation, animation, and
        // world-authoring declarations in addition to the original 57 drawers.
        // 71 with FullBodyIK3D's: the root bone pair, its effector list (drawn by its own list
        // drawer rather than typed rows - each row is a bone, a target and a pull weight), the
        // solver's authored budget, and the answers one solve writes back.
        // +1: the Skin drawer from MeshInstance3D skinning, +1: Morph Targets, +1: the Sky section, +1: Ragdoll3D.
        EXPECT_EQ(EditorAccess::GetInspectorDrawerCount(editor), 75U);
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "full-body-ik-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "ragdoll-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "directional-light-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "sky-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "animation-mixer"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "solid-body"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "animation-tree"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "bone-attachment-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "two-bone-ik-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "nav-mesh-volume-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "nav-seeker-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "skeleton-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "surface-instance"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "light-emitter"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "decal-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "fog-volume-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "bone-modifier"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "physics-object"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "render-instance"));
        EXPECT_FALSE(EditorAccess::HasInspectorDrawer(editor, "name"));
        EXPECT_FALSE(EditorAccess::HasInspectorDrawer(editor, "hierarchy"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "transform"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "primitive-mesh"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "camera"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "mesh"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "light"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "collider"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "rigid-body"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "kinematic-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "spring-arm"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "spawn-point"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "ray-cast-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "projectile-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "hitbox-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "hurtbox-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "lod-group-3d"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "audio-source"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "particle-emitter"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "script"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "animation-player"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "world-environment"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "character-controller"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "prefab-instance"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "canvas"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "ui-text"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "ui-image"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "ui-button"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "physics-interpolation"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "editor-description"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "visibility"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "process"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "thread-group"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "auto-translate"));
        EXPECT_TRUE(EditorAccess::HasInspectorDrawer(editor, "object-metadata"));
        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, WorldEnvironmentComponent_AttachEditUndoRedoThroughEditorPath) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_world_environment.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity entity =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(entity, Scene::kInvalidEntity);
        ASSERT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(entity));

        Scene::WorldEnvironment3DComponent environment;
        environment.skyAssetPath = "environments/sunset.uvsky";
        environment.ambientColor = Math::Vector3{0.15F, 0.25F, 0.40F};
        environment.ambientEnergy = 1.75F;
        environment.exposure = 1.25F;
        environment.fogColor = Math::Vector3{0.30F, 0.35F, 0.45F};
        environment.fogDensity = 0.02F;
        environment.fogEnabled = true;
        environment.postProcessingEnabled = true;
        ASSERT_TRUE(Scene::IsWorldEnvironment3DObjectComponentValid(environment));
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::WorldEnvironment, environment));
        ASSERT_TRUE(entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(entity));
        EXPECT_EQ(entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(entity).skyAssetPath,
                  environment.skyAssetPath);
        EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(entity).ambientEnergy,
                        environment.ambientEnergy);
        EXPECT_TRUE(editor.CanUndo());

        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(entity));
        ASSERT_TRUE(editor.Redo());
        ASSERT_TRUE(entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(entity));
        EXPECT_EQ(entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(entity).fogEnabled,
                  environment.fogEnabled);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, OutlinerContext_UsesFixedSpecializedTagPriority) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_outliner_tags.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity plain = entityManager.CreateEntity();
        AttachRoot(engine, plain, Scene::TransformComponent{});
        EXPECT_TRUE(EditorAccess::GetOutlinerTypeTag(editor, plain).empty());

        const Scene::Entity collider = entityManager.CreateEntity();
        AttachRoot(engine, collider, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::ColliderComponent>(collider, Scene::ColliderComponent{});
        EXPECT_EQ(EditorAccess::GetOutlinerTypeTag(editor, collider), "Collision Box");

        const Scene::Entity light = entityManager.CreateEntity();
        AttachRoot(engine, light, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::LightComponent>(light, Scene::LightComponent{});
        EXPECT_EQ(EditorAccess::GetOutlinerTypeTag(editor, light), "Directional Light");

        const Scene::Entity camera = entityManager.CreateEntity();
        AttachRoot(engine, camera, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::CameraComponent>(camera, Scene::CameraComponent{});
        EXPECT_EQ(EditorAccess::GetOutlinerTypeTag(editor, camera), "Camera");

        const Scene::Entity primitive = entityManager.CreateEntity();
        AttachRoot(engine, primitive, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
            primitive, Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Plane, {0.4F, 0.5F, 0.6F}});
        entityManager.AddComponent<Scene::ColliderComponent>(primitive, Scene::ColliderComponent{});
        entityManager.AddComponent<Scene::CameraComponent>(primitive, Scene::CameraComponent{});
        EXPECT_EQ(EditorAccess::GetOutlinerTypeTag(editor, primitive), "Plane");

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, OutlinerContext_AncestryAndEligibleParentsExcludeSelectedSubtree) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_outliner_hierarchy_context.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity rootA = entityManager.CreateEntity();
        AttachRoot(engine, rootA, Scene::TransformComponent{});
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity selected = entityManager.CreateEntity();
        AttachRoot(engine, selected, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, selected, parent);
        const Scene::Entity descendant = entityManager.CreateEntity();
        AttachRoot(engine, descendant, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, descendant, selected);
        const Scene::Entity rootB = entityManager.CreateEntity();
        AttachRoot(engine, rootB, Scene::TransformComponent{});
        const Scene::Entity folder = EditorAccess::GetObjectFolder(editor);
        for (const Scene::Entity top : {rootA, parent, rootB}) {
            services.GetSceneGraph().SetParent(entityManager, top, folder);
        }

        EXPECT_EQ(EditorAccess::GetDocumentAncestry(editor, selected),
                  (std::vector<Scene::Entity>{editor.GetDocumentSceneRoot(),
                                                 EditorAccess::GetViewport(editor),
                                                 folder, parent, selected}));
        // An object may only move inside a folder: the folder leads, then its objects in order. The
        // scene root and the Viewport are not offered.
        EXPECT_EQ(EditorAccess::GetEligibleReparentParents(editor, selected),
                  (std::vector<Scene::Entity>{folder, rootA, parent, rootB}));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, AssetTableWorkflow_UsesPrimaryExtensionTagAndIndependentRegisteredFocus) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_asset_table_tags.uvscene");
        Asset::ProjectFileEntry registeredMesh;
        registeredMesh.relativePath = "Characters/Hero.UVMODEL";
        registeredMesh.kind = Asset::ProjectFileEntryKind::File;
        registeredMesh.registeredAssetGuid = Asset::AssetGuid{42U};

        Asset::ProjectFileEntry ordinaryFile;
        ordinaryFile.relativePath = "Notes/readme.txt";
        ordinaryFile.kind = Asset::ProjectFileEntryKind::File;

        Asset::ProjectFileEntry directory;
        directory.relativePath = "Characters";
        directory.kind = Asset::ProjectFileEntryKind::Directory;

        // A registered file keeps its semantic extension tag; registration remains an independent badge/focus.
        EXPECT_EQ(EditorAccess::GetAssetTableItemTypeLabel(registeredMesh), "Mesh");
        EXPECT_EQ(EditorAccess::GetAssetTableItemTypeLabel(ordinaryFile), "File");
        EXPECT_EQ(EditorAccess::GetAssetTableItemTypeLabel(directory), "Folder");

        EditorAccess::SelectAssetTableMeshFocus(editor);
        EXPECT_TRUE(EditorAccess::DoesAssetTableEntryMatchFocus(editor, registeredMesh));
        EXPECT_FALSE(EditorAccess::DoesAssetTableEntryMatchFocus(editor, ordinaryFile));

        EditorAccess::SelectAssetTableRegisteredFocus(editor);
        EXPECT_TRUE(EditorAccess::DoesAssetTableEntryMatchFocus(editor, registeredMesh));
        EXPECT_FALSE(EditorAccess::DoesAssetTableEntryMatchFocus(editor, ordinaryFile));
        EXPECT_FALSE(EditorAccess::DoesAssetTableEntryMatchFocus(editor, directory));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, AssetTableWorkflow_ScriptsAudioAndFontsAreTheirOwnTypesAndEveryTypeHasAnIcon) {
    const auto labelOf = [](const char* const path) {
        Asset::ProjectFileEntry entry;
        entry.relativePath = path;
        entry.kind = Asset::ProjectFileEntryKind::File;
        return EditorAccess::GetAssetTableItemTypeLabel(entry);
    };
    EXPECT_EQ(labelOf("Scripts/player.uvs"), "Script");
    EXPECT_EQ(labelOf("Audio/step.uvaudio"), "Audio");
    EXPECT_EQ(labelOf("Audio/Step.WAV"), "Audio");
    // Compressed sources are audio too: Vorbis, MP3 and FLAC each import into the same .uvaudio
    // envelope, and the extension is matched in any case exactly as WAV is.
    EXPECT_EQ(labelOf("Audio/theme.ogg"), "Audio");
    EXPECT_EQ(labelOf("Audio/Theme.OGG"), "Audio");
    EXPECT_EQ(labelOf("Audio/theme.mp3"), "Audio");
    EXPECT_EQ(labelOf("Audio/theme.flac"), "Audio");
    // A container no registered importer can read stays a plain file rather than promising playback.
    EXPECT_EQ(labelOf("Audio/theme.m4a"), "File");
    EXPECT_EQ(labelOf("Audio/theme.aac"), "File");
    EXPECT_EQ(labelOf("Fonts/title.ttf"), "Font");
    EXPECT_EQ(labelOf("Fonts/body.otf"), "Font");
    EXPECT_EQ(labelOf("Content/Hero.uventity"), "Entity");
    EXPECT_EQ(labelOf("Notes/readme.txt"), "File");
    // Without editor source metadata, raw model files use the extension-only mesh fallback.
    // The live Asset Table applies the FBX source summary to distinguish animation-only and rigged files.
    EXPECT_EQ(labelOf("Characters/hero.fbx"), "Mesh");
    EXPECT_EQ(labelOf("Characters/Hero.FBX"), "Mesh");

    // Each type shows its own picture in the browser, found by the label it is shown with.
    std::set<const EditorIconSource*> icons;
    for (const std::string& label : EditorAccess::GetEveryAssetTableTypeLabel()) {
        const EditorIconSource* const icon = FindEditorIconSource(EditorIconGroup::ContentType, label);
        EXPECT_NE(icon, nullptr) << "no icon for content type " << label;
        icons.insert(icon);
    }
    EXPECT_EQ(icons.size(), EditorAccess::GetEveryAssetTableTypeLabel().size());
}

TEST(EditorTest, AssetTableWorkflow_PersistsFiltersAndSafelyFallsBackWhenFolderDisappears) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_asset_table_navigation.uvscene");
        Asset::ProjectFileSnapshot snapshot;
        snapshot.entries.push_back(
            Asset::ProjectFileEntry{std::filesystem::path{"Scenes"}, Asset::ProjectFileEntryKind::Directory, std::nullopt});
        snapshot.entries.push_back(Asset::ProjectFileEntry{std::filesystem::path{"Scenes/City.uvscene"},
                                                               Asset::ProjectFileEntryKind::File, std::nullopt});

        EditorAccess::SetAssetFilter(editor, "city");
        EditorAccess::SelectAssetTableMeshFocus(editor);
        EditorAccess::SetAssetTableDirectory(editor, "Scenes");
        EditorAccess::ReconcileAssetTableDirectory(editor, snapshot);

        EXPECT_EQ(EditorAccess::GetAssetTableDirectory(editor), std::filesystem::path{"Scenes"});
        EXPECT_EQ(editor.GetAssetFilter(), "city");
        EXPECT_EQ(EditorAccess::GetAssetTableTypeFocusLabel(editor), "Mesh");

        snapshot.entries.clear();
        EditorAccess::ReconcileAssetTableDirectory(editor, snapshot);
        EXPECT_TRUE(EditorAccess::GetAssetTableDirectory(editor).empty());
        EXPECT_EQ(editor.GetAssetFilter(), "city");
        EXPECT_EQ(EditorAccess::GetAssetTableTypeFocusLabel(editor), "Mesh");

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, AssetTableAutoRefresh_RefreshesAfterEngineWatcherSequenceAndAcknowledgesIt) {
    const std::filesystem::path root = "uve_editor_tests_auto_refresh_content";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Scenes");
    {
        std::ofstream initialFile(root / "Scenes" / "Initial.txt", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(initialFile.is_open());
        initialFile << "initial";
        ASSERT_TRUE(initialFile.good());
    }

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = root;
    config.projectChangeWatchPollIntervalSeconds = 0.0;
    config.projectChangeJournalCapacity = 16U;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_asset_table_auto_refresh.uvscene");
        editor.Init();

        engine.TickFrame();
        editor.Tick();
        Asset::IProjectFileIndex& projectFileIndex = engine.GetServices().GetProjectFileIndex();
        Asset::ProjectFileSnapshot initialSnapshot = projectFileIndex.GetSnapshot();
        ASSERT_EQ(initialSnapshot.refreshGeneration, 1U);
        ASSERT_TRUE(std::any_of(initialSnapshot.entries.begin(), initialSnapshot.entries.end(),
                                [](const Asset::ProjectFileEntry& entry) {
                                    return entry.relativePath == std::filesystem::path{"Scenes/Initial.txt"};
                                }));
        EXPECT_TRUE(engine.GetServices().GetProjectChangeWatcher().GetSnapshot().changes.empty());

        {
            std::ofstream newFile(root / "Scenes" / "AutoRefresh.txt", std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(newFile.is_open());
            newFile << "created after baseline";
            ASSERT_TRUE(newFile.good());
        }

        engine.TickFrame();
        editor.Tick();
        const Asset::ProjectFileSnapshot refreshedSnapshot = projectFileIndex.GetSnapshot();
        ASSERT_GT(refreshedSnapshot.refreshGeneration, initialSnapshot.refreshGeneration);
        EXPECT_TRUE(std::any_of(refreshedSnapshot.entries.begin(), refreshedSnapshot.entries.end(),
                                [](const Asset::ProjectFileEntry& entry) {
                                    return entry.relativePath == std::filesystem::path{"Scenes/AutoRefresh.txt"};
                                }));
        const Asset::ProjectChangeSnapshot changeSnapshot =
            engine.GetServices().GetProjectChangeWatcher().GetSnapshot();
        EXPECT_TRUE(changeSnapshot.changes.empty());
        EXPECT_FALSE(changeSnapshot.rescanRequired);

        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorTest, TextureThumbnail_GracefullyReturnsZeroForMissingCorruptOrUnsupportedFormat) {
    const std::filesystem::path root = "uve_editor_tests_texture_thumbnail_content";
    std::filesystem::remove_all(root);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    {
        std::ofstream corrupt(root / "corrupt.uvtex", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt.is_open());
        corrupt << "not-a-uve-envelope";
    }
    {
        // A structurally valid envelope declaring an unsupported pixel format for thumbnails
        // (RGBA16Float): LoadTextureAsset succeeds, but GetTextureThumbnail must still
        // decline to upload it rather than misinterpreting the byte layout as RGBA8Unorm.
        Asset::TextureAsset unsupported;
        unsupported.width = 1U;
        unsupported.height = 1U;
        unsupported.format = Asset::TextureAssetFormat::RGBA16Float;
        unsupported.pixels.resize(Asset::BytesPerPixel(Asset::TextureAssetFormat::RGBA16Float));
        ASSERT_TRUE(Asset::SaveTextureAsset(unsupported, root / "unsupported.uvtex"));
    }

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = root;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_texture_thumbnail.uvscene");
        editor.Init();
        engine.TickFrame();
        editor.Tick();

        EXPECT_EQ(EditorAccess::GetTextureThumbnail(editor, "missing.uvtex"), 0U);
        EXPECT_EQ(EditorAccess::GetTextureThumbnail(editor, "corrupt.uvtex"), 0U);
        EXPECT_EQ(EditorAccess::GetTextureThumbnail(editor, "unsupported.uvtex"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorAccess::GetTextureThumbnailCacheSize(editor), 3U);
        EXPECT_EQ(EditorAccess::GetTextureThumbnail(editor, "missing.uvtex"), 0U);
        EXPECT_EQ(EditorAccess::GetTextureThumbnailCacheSize(editor), 3U);

        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorTest, MeshThumbnail_GracefullyReturnsZeroForMissingCorruptOrEmptyMesh) {
    const std::filesystem::path root = "uve_editor_tests_mesh_thumbnail_content";
    std::filesystem::remove_all(root);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    {
        std::ofstream corrupt(root / "corrupt.uvmodel", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt.is_open());
        corrupt << "not-a-uve-envelope";
    }
    {
        // A structurally valid mesh envelope with no geometry: LoadMeshAsset succeeds, but
        // RenderThumbnail has nothing to draw and must decline rather than issuing an empty
        // draw call.
        const Asset::MeshAsset empty;
        ASSERT_TRUE(Asset::SaveMeshAsset(empty, root / "empty.uvmodel"));
    }

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = root;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_mesh_thumbnail.uvscene");
        editor.Init();
        engine.TickFrame();
        editor.Tick();

        EXPECT_EQ(EditorAccess::GetMeshThumbnail(editor, "missing.uvmodel"), 0U);
        EXPECT_EQ(EditorAccess::GetMeshThumbnail(editor, "corrupt.uvmodel"), 0U);
        EXPECT_EQ(EditorAccess::GetMeshThumbnail(editor, "empty.uvmodel"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorAccess::GetMeshThumbnailCacheSize(editor), 3U);
        EXPECT_EQ(EditorAccess::GetMeshThumbnail(editor, "missing.uvmodel"), 0U);
        EXPECT_EQ(EditorAccess::GetMeshThumbnailCacheSize(editor), 3U);

        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(EditorTest, SessionSettings_MigratesWithoutHiddenWriteAndPreservesDocumentState) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Core::EngineServices& services = engine.GetServices();
        Config::IConfigManager& settings = services.GetConfigManager();
        settings.SetInt("editor.sessionSettingsVersion", 0);
        settings.SetBool("editor.panels.sceneVisible", false);
        settings.SetBool("editor.panels.inspectorVisible", false);
        settings.SetBool("editor.panels.bottomDockVisible", false);
        settings.SetBool("editor.viewport.snap.enabled", true);
        settings.SetDouble("editor.viewport.snap.translateStep", -1.0);
        settings.SetDouble("editor.viewport.snap.rotateStepDegrees", 45.0);
        settings.SetDouble("editor.viewport.snap.scaleStep", 0.25);

        Editor editor(services, "uve_editor_tests_session_settings.uvscene");
        editor.Init();
        EXPECT_FALSE(EditorAccess::IsScenePanelVisible(editor));
        EXPECT_FALSE(EditorAccess::IsInspectorPanelVisible(editor));
        EXPECT_FALSE(EditorAccess::IsBottomDockVisible(editor));
        EXPECT_FALSE(settings.HasKey("editor.workspace.active"));
        const EditorTransformSnappingSettings& snapping = editor.GetTransformSnappingSettings();
        EXPECT_TRUE(snapping.enabled);
        EXPECT_FLOAT_EQ(snapping.translateStep, 1.0F);
        EXPECT_FLOAT_EQ(snapping.rotateStepDegrees, 45.0F);
        EXPECT_FLOAT_EQ(snapping.scaleStep, 0.25F);
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_TRUE(editor.GetSelectedEntities().empty());

        EditorAccess::ApplyDefaultLayoutPreset(editor);
        EXPECT_TRUE(EditorAccess::IsScenePanelVisible(editor));
        EXPECT_TRUE(EditorAccess::IsInspectorPanelVisible(editor));
        EXPECT_TRUE(EditorAccess::IsBottomDockVisible(editor));
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_TRUE(editor.GetSelectedEntities().empty());

        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        EXPECT_EQ(settings.GetInt("editor.sessionSettingsVersion", -1), 1);
        EXPECT_TRUE(settings.HasKey("editor.workspace.active"));
        EXPECT_FALSE(editor.IsSceneDirty());
        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, SessionSettings_ReadsValuesSavedUnderTheOldNodeKeys) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
    namespace Id = EditorSettingId;
    const auto placement = [](const EditorNewObjectPlacement value) {
        return Config::SettingValue{static_cast<std::int64_t>(value)};
    };

    // A settings file written before the object rename carries only the old editor.nodes.* keys.
    settings.SetBool("editor.nodes.addUnderSelection", false);
    settings.SetInt("editor.nodes.placement", static_cast<std::int64_t>(EditorNewObjectPlacement::ViewFocus));
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_renamed_settings.uvscene");
        editor.Init();
        EXPECT_EQ(editor.GetEditorSetting(Id::kNewObjectsUnderSelection), Config::SettingValue{false});
        EXPECT_EQ(editor.GetEditorSetting(Id::kNewObjectPlacement), placement(EditorNewObjectPlacement::ViewFocus));
        // The registry migrates to the new ids and removes the old aliases on load.
        EXPECT_EQ(settings.GetBool("editor.objects.addUnderSelection", true), false);
        EXPECT_EQ(settings.GetInt("editor.objects.placement", -1),
                  static_cast<std::int64_t>(EditorNewObjectPlacement::ViewFocus));
        EXPECT_FALSE(settings.HasKey("editor.nodes.addUnderSelection"));
        EXPECT_FALSE(settings.HasKey("editor.nodes.placement"));
        editor.Shutdown();
    }

    // Next file: nothing stored under the new names, so the old ones decide again.
    ASSERT_TRUE(settings.RemoveKey("editor.objects.addUnderSelection"));
    ASSERT_TRUE(settings.RemoveKey("editor.objects.placement"));
    // An old value the setting's own rules refuse is not applied: a stale number never reaches the
    // editor, and neither does a value of the wrong type. Each setting keeps its default.
    settings.SetInt("editor.nodes.placement", 99);
    settings.SetString("editor.nodes.addUnderSelection", "yes");
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_renamed_settings_stale.uvscene");
        editor.Init();
        EXPECT_EQ(editor.GetEditorSetting(Id::kNewObjectsUnderSelection), Config::SettingValue{true});
        EXPECT_EQ(editor.GetEditorSetting(Id::kNewObjectPlacement), placement(EditorNewObjectPlacement::ParentOrigin));
        editor.Shutdown();
    }

    // A file that speaks both names keeps the new one: the rename is a fallback, never an override.
    settings.SetInt("editor.nodes.placement", static_cast<std::int64_t>(EditorNewObjectPlacement::ViewFocus));
    settings.SetInt(Id::kNewObjectPlacement, static_cast<std::int64_t>(EditorNewObjectPlacement::ParentOrigin));
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_renamed_settings_both.uvscene");
        editor.Init();
        EXPECT_EQ(editor.GetEditorSetting(Id::kNewObjectPlacement), placement(EditorNewObjectPlacement::ParentOrigin));
        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, EditorSettings_DescriptorDefaultsMatchTheEditorsOwnDefaults) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        // Not initialised, so nothing has been loaded: every value is the editor's in-class default.
        Editor editor(engine.GetServices(), "uve_editor_tests_setting_defaults.uvscene");
        const Config::SettingsRegistry& registry = editor.GetSettingsRegistry();
        // The declared preferences, and a primary and alternate shortcut for every command.
        ASSERT_EQ(registry.GetCount(),
                  163U + std::size(kRenamedSettingIds) + (2U * editor.GetEditorCommands().size()));
        for (const Config::SettingDescriptor* descriptor : registry.GetAll()) {
            // A renamed setting's old name is only a migration alias; the registry moves its value
            // to the replacement before normal settings are applied.
            if (descriptor->HasFlag(Config::kSettingFlagDeprecated)) {
                continue;
            }
            // Axis palette defaults belong to the host and are intentionally unset until it seeds
            // all three colours together; these hidden descriptors are tested by the palette tests.
            if (descriptor->id == EditorSettingId::kViewportAxisColorX ||
                descriptor->id == EditorSettingId::kViewportAxisColorY ||
                descriptor->id == EditorSettingId::kViewportAxisColorZ) {
                continue;
            }
            const std::optional<Config::SettingValue> value = editor.GetEditorSetting(descriptor->id);
            ASSERT_TRUE(value.has_value()) << descriptor->id;
            EXPECT_EQ(*value, descriptor->defaultValue) << descriptor->id;
        }
    }
    engine.Shutdown();
}

TEST(EditorTest, EditorSettings_SetAppliesAtOnceAndRefusesWhatItsDescriptorForbids) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_setting_set.uvscene");
        editor.Init();
        namespace Id = EditorSettingId;
        ASSERT_TRUE(editor.SetEditorSetting(Id::kGridOpacity, 0.5));
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacity(), 0.5F);
        ASSERT_TRUE(editor.SetEditorSetting(Id::kSelectionOutlineColor, Config::SettingColor{0.0F, 1.0F, 0.0F}));
        EXPECT_FLOAT_EQ(editor.GetViewportSelectionOutlineColor().g, 1.0F);
        ASSERT_TRUE(editor.SetEditorSetting(Id::kSnapRotateStepDegrees, 45.0));
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().rotateStepDegrees, 45.0F);

        // Out of range, the wrong type, and an unknown id change nothing.
        EXPECT_FALSE(editor.SetEditorSetting(Id::kGridOpacity, 0.0));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kGridOpacity, std::numeric_limits<double>::quiet_NaN()));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kGridOpacity, true));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kSnapRotateStepDegrees, 720.0));
        EXPECT_FALSE(editor.SetEditorSetting("editor.unknown", true));
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacity(), 0.5F);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().rotateStepDegrees, 45.0F);

        // A change made elsewhere (the menu) is what the setting reads back.
        ASSERT_TRUE(editor.SetViewportGridCellSize(5.0F));
        EXPECT_EQ(editor.GetEditorSetting(Id::kGridCellSize), Config::SettingValue{5.0});
        EXPECT_FALSE(editor.GetEditorSetting("editor.unknown").has_value());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ProjectSettings_ChangesAreSavedAndTheNextSessionRunsWithThem) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.projectSettingsFilePath);
    {
        Core::EngineCore engine(config);
        engine.Init();
        ASSERT_TRUE(engine.Load());
        Editor editor(engine.GetServices(), "uve_editor_tests_project_settings.uvscene");
        editor.Init();
        Config::SettingsDocument& project = engine.GetServices().GetProjectSettings();
        ASSERT_TRUE(project.SetValue(Core::EngineProjectSettingId::kPhysicsTicksPerSecond, 120.0));
        EXPECT_TRUE(project.IsDirty());
        // A setting left at its default never reaches the file.
        ASSERT_TRUE(project.SetValue(Core::EngineProjectSettingId::kShadowFilter, std::int64_t{1}));
        editor.Shutdown();
        EXPECT_FALSE(project.IsDirty());
        engine.Shutdown();
    }
    ASSERT_TRUE(std::filesystem::exists(config.projectSettingsFilePath));
    {
        Core::EngineCore engine(config);
        engine.Init();
        EXPECT_DOUBLE_EQ(engine.GetConfig().fixedUpdateFps, 120.0);
        EXPECT_FALSE(engine.GetServices().GetProjectSettings().GetStoredValue(
                         Core::EngineProjectSettingId::kShadowFilter)
                         .has_value());
        engine.Shutdown();
    }
    std::filesystem::remove(config.projectSettingsFilePath);
}

TEST(EditorTest, NewObjectDefaults_ParentAndPlacementFollowThePreferences) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_new_object_defaults.uvscene");
        editor.Init();
        Scene::IEntityManager& entities = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& graph = engine.GetServices().GetSceneGraph();
        const auto parentOf = [&entities](const Scene::Entity entity) {
            return entities.GetComponent<Scene::HierarchyComponent>(entity).parent;
        };
        const auto localPositionOf = [&entities](const Scene::Entity entity) {
            return entities.GetComponent<Scene::TransformComponent>(entity).localPosition;
        };
        namespace Id = EditorSettingId;

        // By default: under the selection, at its origin, whatever the camera looks at.
        editor.SetViewportCameraFocus(Math::Vector3{3.0F, 0.0F, -2.0F});
        const Scene::Entity parent = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntity);
        const Scene::Entity root = parentOf(parent);
        const Scene::Entity child = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::BoxMesh3D);
        ASSERT_NE(child, Scene::kInvalidEntity);
        EXPECT_EQ(parentOf(child), parent);
        EXPECT_FLOAT_EQ(localPositionOf(child).x, 0.0F);

        // Not under the selection: straight under the scene root.
        ASSERT_TRUE(editor.SetEditorSetting(Id::kNewObjectsUnderSelection, false));
        editor.SelectEntity(parent);
        const Scene::Entity sibling = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        EXPECT_EQ(parentOf(sibling), root);

        // At the view focus: under the root, that point itself...
        ASSERT_TRUE(editor.SetEditorSetting(Id::kNewObjectPlacement,
                                               static_cast<std::int64_t>(EditorNewObjectPlacement::ViewFocus)));
        const Scene::Entity focused = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        EXPECT_FLOAT_EQ(localPositionOf(focused).x, 3.0F);
        EXPECT_FLOAT_EQ(localPositionOf(focused).z, -2.0F);

        // ...and under a moved parent, the same point taken into the parent's space.
        ASSERT_TRUE(editor.SetEditorSetting(Id::kNewObjectsUnderSelection, true));
        Scene::TransformComponent moved = entities.GetComponent<Scene::TransformComponent>(parent);
        moved.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
        graph.SetLocalTransform(entities, parent, moved);
        graph.Update(entities);
        editor.SelectEntity(parent);
        const Scene::Entity nested = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        EXPECT_EQ(parentOf(nested), parent);
        EXPECT_FLOAT_EQ(localPositionOf(nested).x, 2.0F);
        EXPECT_FLOAT_EQ(localPositionOf(nested).z, -2.0F);

        // Undo and redo keep the place.
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.Redo());
        const Scene::Entity redone = editor.GetSelectedEntity();
        ASSERT_NE(redone, Scene::kInvalidEntity);
        EXPECT_FLOAT_EQ(localPositionOf(redone).x, 2.0F);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, PlayModePreferences_PauseSaveAndStayInTheTab) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_play_prefs.uvscene";
    std::filesystem::remove(scenePath);
    {
        Editor editor(engine.GetServices(), scenePath, 100U, &engine);
        editor.Init();
        namespace Id = EditorSettingId;

        // By default: running, in the Game tab, and the tab comes back at Stop.
        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_EQ(editor.GetPlayModeState(), EditorPlayModeState::Playing);
        EXPECT_FALSE(EditorAccess::IsLibraryWorkspaceActive(editor));
        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_TRUE(EditorAccess::IsLibraryWorkspaceActive(editor));

        // Paused on start, in the tab it was started from, with the dirty scene saved first.
        ASSERT_TRUE(editor.SetEditorSetting(Id::kPlayPauseOnStart, true));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kPlaySwitchToGame, false));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kPlaySaveSceneFirst, true));
        ASSERT_NE(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D), Scene::kInvalidEntity);
        ASSERT_TRUE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_EQ(editor.GetPlayModeState(), EditorPlayModeState::Paused);
        EXPECT_TRUE(EditorAccess::IsLibraryWorkspaceActive(editor));
        EXPECT_TRUE(std::filesystem::exists(scenePath));
        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_FALSE(editor.IsSceneDirty());
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorTest, ObjectTypes_EveryObjectIsTypedAndKeepsItThroughDuplicateUndoAndSave) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_object_types.uvscene";
    std::filesystem::remove(scenePath);
    {
        Editor editor(engine.GetServices(), scenePath);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        EXPECT_EQ(editor.GetObjectTypeName(root), "SceneRoot");

        // Every object the Add Object list makes reads back as the type it was made as, including the
        // ones whose components alone could not say (Static3D is built like a Collider3D).
        for (const Scene::Objects::SceneObjectDescriptor& descriptor : Scene::Objects::GetSceneObjectDescriptors()) {
            if (!descriptor.libraryCreatable) {
                continue;
            }
            editor.SelectEntity(root);
            const Scene::Entity object = editor.CreateDocumentSceneObject(descriptor.kind);
            ASSERT_NE(object, Scene::kInvalidEntity) << descriptor.displayName;
            EXPECT_EQ(editor.GetObjectTypeName(object), descriptor.displayName);
        }

        // The legacy creation commands type their objects as the same kinds.
        editor.SelectEntity(root);
        EXPECT_EQ(editor.GetObjectTypeName(editor.CreateDocumentEntity(EditorEntityKind::Cube)), "BoxMesh3D");

        // Duplicate, and undo then redo of a creation, carry the type with the object.
        editor.SelectEntity(root);
        const Scene::Entity body = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Static3D);
        ASSERT_NE(body, Scene::kInvalidEntity);
        editor.SelectEntity(body);
        const Scene::Entity copy = editor.DuplicateSelectedEntity();
        EXPECT_EQ(editor.GetObjectTypeName(copy), "Static3D");
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(editor.GetObjectTypeName(editor.GetSelectedEntity()), "Static3D");

        ASSERT_TRUE(editor.SaveScene());
        ASSERT_TRUE(editor.LoadScene());
        std::size_t staticBodies = 0U;
        entityManager.ForEach<Scene::NameComponent>(
            [&](const Scene::Entity entity, const Scene::NameComponent& name) {
                if (name.name.starts_with("Static3D")) {
                    ++staticBodies;
                    EXPECT_EQ(editor.GetObjectTypeName(entity), "Static3D");
                }
            });
        EXPECT_EQ(staticBodies, 2U); // the one from the list, and the one brought back by redo
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorTest, SiblingOrder_MoveDuplicateAndDeleteKeepPlacesThroughUndo) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_sibling_order.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();
        const Scene::Entity root = EditorAccess::GetObjectFolder(editor); // the level's only folder
        std::vector<Scene::Entity> objects;
        for (int i = 0; i < 3; ++i) {
            editor.SelectEntity(root);
            objects.push_back(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D));
            ASSERT_NE(objects.back(), Scene::kInvalidEntity);
        }
        const auto order = [&] { return sceneGraph.GetChildren(entityManager, root); };
        ASSERT_EQ(order(), objects);
        using Move = EditorSiblingMove;

        // At the ends, the moves toward that end are unavailable; the root never moves.
        EXPECT_FALSE(editor.CanMoveDocumentEntity(objects[0], Move::Up));
        EXPECT_FALSE(editor.CanMoveDocumentEntity(objects[2], Move::ToBottom));
        EXPECT_FALSE(editor.CanMoveDocumentEntity(root, Move::Down));

        ASSERT_TRUE(editor.MoveDocumentEntity(objects[2], Move::Up));
        EXPECT_EQ(order(), (std::vector<Scene::Entity>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.MoveDocumentEntity(objects[0], Move::ToBottom));
        EXPECT_EQ(order(), (std::vector<Scene::Entity>{objects[2], objects[1], objects[0]}));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(order(), (std::vector<Scene::Entity>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(order(), objects);
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(order(), (std::vector<Scene::Entity>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.Undo());

        // A copy lands right below its source, and comes back there on redo.
        editor.SelectEntity(objects[0]);
        const Scene::Entity copy = editor.DuplicateSelectedEntity();
        ASSERT_NE(copy, Scene::kInvalidEntity);
        EXPECT_EQ(sceneGraph.GetSiblingIndex(entityManager, copy), std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(sceneGraph.GetSiblingIndex(entityManager, editor.GetSelectedEntity()),
                  std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.Undo());
        ASSERT_EQ(order(), objects);

        // A deleted object comes back in its place, not at the end.
        editor.SelectEntity(objects[1]);
        ASSERT_TRUE(editor.DeleteSelectedEntity());
        ASSERT_TRUE(editor.Undo());
        const std::vector<Scene::Entity> after = order();
        ASSERT_EQ(after.size(), 3U);
        EXPECT_EQ(after[0], objects[0]);
        EXPECT_EQ(after[2], objects[2]);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, HierarchyPreferences_ApplyRefuseWhatIsOutOfRangeAndPersist) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    namespace Id = EditorSettingId;
    const std::string_view scenePath = "uve_editor_tests_hierarchy_prefs.uvscene";
    const auto expectChosen = [](const HierarchyViewSettings& view) {
        EXPECT_FALSE(view.revealSelection);
        EXPECT_FALSE(view.showTypeName);
        EXPECT_EQ(view.visibilityColumn, HierarchyVisibilityColumn::OnHover);
        EXPECT_EQ(view.doubleClick, HierarchyDoubleClick::FocusInViewport);
        EXPECT_EQ(view.treeLines, HierarchyTreeLines::ToEachChild);
        EXPECT_FLOAT_EQ(view.indentWidth, 30.0F);
        EXPECT_TRUE(view.showIcons);
        EXPECT_TRUE(view.dragToReparent);
    };
    {
        Editor editor(engine.GetServices(), scenePath);
        editor.Init();
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyRevealSelection, false));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyShowTypeName, false));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyVisibilityColumn,
                                               static_cast<std::int64_t>(HierarchyVisibilityColumn::OnHover)));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyDoubleClick,
                                               static_cast<std::int64_t>(HierarchyDoubleClick::FocusInViewport)));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyTreeLines,
                                               static_cast<std::int64_t>(HierarchyTreeLines::ToEachChild)));
        ASSERT_TRUE(editor.SetEditorSetting(Id::kHierarchyIndentWidth, 30.0));
        expectChosen(editor.GetHierarchyViewSettings());

        // An indent the tree could not draw, an action that does not exist, or a value of the wrong
        // type changes nothing.
        EXPECT_FALSE(editor.SetEditorSetting(Id::kHierarchyIndentWidth, 2.0));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kHierarchyIndentWidth, 400.0));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kHierarchyDoubleClick, std::int64_t{7}));
        EXPECT_FALSE(editor.SetEditorSetting(Id::kHierarchyShowIcons, std::int64_t{1}));
        expectChosen(editor.GetHierarchyViewSettings());
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        // The next session starts where this one left off.
        Editor reloaded(engine.GetServices(), scenePath);
        reloaded.Init();
        expectChosen(reloaded.GetHierarchyViewSettings());
        reloaded.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, EditorCommands_RunOnlyWhenAvailableAndKeepRebindsAcrossSessions) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_commands.uvscene");
        editor.Init();
        const std::vector<EditorCommand>& commands = editor.GetEditorCommands();
        // Ids are unique, and every default shortcut is one only its command has.
        for (std::size_t a = 0U; a < commands.size(); ++a) {
            for (std::size_t b = a + 1U; b < commands.size(); ++b) {
                EXPECT_NE(commands[a].id, commands[b].id);
                for (const EditorShortcut& shortcut : commands[a].defaultShortcuts) {
                    if (!shortcut.IsEmpty()) {
                        EXPECT_EQ(std::count(commands[b].defaultShortcuts.begin(), commands[b].defaultShortcuts.end(),
                                             shortcut),
                                  0)
                            << commands[a].id << " and " << commands[b].id;
                    }
                }
            }
        }

        // Undo is unavailable with nothing done; after creating an object it runs.
        EXPECT_FALSE(editor.RunEditorCommand("edit.undo"));
        ASSERT_TRUE(editor.RunEditorCommand("create.empty"));
        EXPECT_TRUE(editor.CanUndo());
        EXPECT_TRUE(editor.RunEditorCommand("edit.undo"));
        EXPECT_FALSE(editor.RunEditorCommand("no.such.command"));

        // A rebind is kept with the preferences; a key a shortcut may not use is refused.
        const std::optional<EditorShortcut> ctrlU = ParseEditorShortcut("Ctrl+U");
        ASSERT_TRUE(ctrlU.has_value());
        ASSERT_TRUE(editor.SetEditorCommandShortcut("edit.undo", 1U, *ctrlU));
        ASSERT_TRUE(editor.SetEditorCommandShortcut("play.step", 0U, EditorShortcut{}));
        EXPECT_FALSE(editor.SetEditorCommandShortcut("edit.undo", 2U, *ctrlU));
        EXPECT_FALSE(editor.SetEditorCommandShortcut("edit.undo", 0U, EditorShortcut{1, false, false, false}));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_commands_reload.uvscene");
        reloaded.Init();
        const auto find = [&reloaded](const std::string_view id) {
            const auto& commands = reloaded.GetEditorCommands();
            return *std::find_if(commands.begin(), commands.end(), [id](const auto& c) { return c.id == id; });
        };
        EXPECT_EQ(FormatEditorShortcut(find("edit.undo").shortcuts[0]), "Ctrl+Z");
        EXPECT_EQ(FormatEditorShortcut(find("edit.undo").shortcuts[1]), "Ctrl+U");
        EXPECT_TRUE(find("play.step").shortcuts[0].IsEmpty());
        reloaded.Shutdown();
    }
    // A shortcut the file garbled falls back to its default.
    engine.GetServices().GetConfigManager().SetString("editor.shortcuts.edit.undo.primary", "Ctrl+Nope");
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_commands_corrupt.uvscene");
        corrupt.Init();
        const auto& commands = corrupt.GetEditorCommands();
        const auto undo = std::find_if(commands.begin(), commands.end(), [](const auto& c) { return c.id == "edit.undo"; });
        EXPECT_EQ(FormatEditorShortcut(undo->shortcuts[0]), "Ctrl+Z");
        corrupt.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, SessionSettings_NeverRestoresTheGameWorkspace) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Config::IConfigManager& settings = engine.GetServices().GetConfigManager();

    // Leaving the editor in Game keeps the last workspace a session can reopen into.
    settings.SetInt("editor.workspace.active", 2);
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_workspace_game.uvscene");
        editor.Init();
        EXPECT_TRUE(EditorAccess::IsScriptingWorkspaceActive(editor));
        EditorAccess::ActivateGameWorkspace(editor);
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        EXPECT_EQ(settings.GetInt("editor.workspace.active", -1), 2);
        editor.Shutdown();
    }
    // A file that names Game anyway opens in Library.
    settings.SetInt("editor.workspace.active", 5);
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_workspace_game_file.uvscene");
        editor.Init();
        EXPECT_TRUE(EditorAccess::IsLibraryWorkspaceActive(editor));
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportView_NamedViewsGoOrthographicAutomaticallyUntilOrbited) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_viewport_views.uvscene");
        editor.Init();
        using View = Editor::ViewportView;
        EXPECT_EQ(editor.GetViewportView(), View::User);
        EXPECT_FALSE(editor.IsViewportOrthographic());
        const std::uint32_t serialBefore = editor.GetViewportViewRequestSerial();

        // A named view switches to orthographic by itself, and each request bumps the serial -
        // picking the same view again must re-snap a camera that has drifted.
        editor.RequestViewportView(View::Top);
        EXPECT_EQ(editor.GetViewportView(), View::Top);
        EXPECT_TRUE(editor.IsViewportOrthographic());
        editor.RequestViewportView(View::Top);
        EXPECT_EQ(editor.GetViewportViewRequestSerial(), serialBefore + 2U);

        // Orbiting out of it returns to a free perspective view.
        editor.NotifyViewportOrbited();
        EXPECT_EQ(editor.GetViewportView(), View::User);
        EXPECT_FALSE(editor.IsViewportOrthographic());

        // An orthographic the author chose sticks through views and orbits.
        editor.SetViewportOrthographic(true);
        editor.RequestViewportView(View::Front);
        editor.NotifyViewportOrbited();
        EXPECT_EQ(editor.GetViewportView(), View::User);
        EXPECT_TRUE(editor.IsViewportOrthographic());

        // Choosing perspective while in a named view keeps the view.
        editor.RequestViewportView(View::Right);
        editor.SetViewportOrthographic(false);
        EXPECT_EQ(editor.GetViewportView(), View::Right);
        EXPECT_FALSE(editor.IsViewportOrthographic());

        EXPECT_STREQ(Editor::GetViewportViewName(View::Back), "Back");

        // Keypad layout: 7/1/3 are Top/Front/Right, Ctrl gives the opposite side; others are not views.
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(7, false), View::Top);
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(7, true), View::Bottom);
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(1, false), View::Front);
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(1, true), View::Back);
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(3, false), View::Right);
        EXPECT_EQ(Editor::GetViewportViewForKeypadDigit(3, true), View::Left);
        EXPECT_FALSE(Editor::GetViewportViewForKeypadDigit(5, false).has_value());
        EXPECT_FALSE(Editor::GetViewportViewForKeypadDigit(0, true).has_value());
        EXPECT_STRNE(Editor::GetViewportViewShortcut(View::Left), "");
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, InspectorFolds_RememberedAcrossSessionReloadAndBounded) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_inspector_folds.uvscene");
        editor.Init();
        // An unknown key answers the caller's default, open or closed.
        EXPECT_TRUE(editor.IsInspectorFoldOpen("section:transform", true));
        EXPECT_FALSE(editor.IsInspectorFoldOpen("group:light/Shadow", false));

        editor.SetInspectorFoldOpen("section:transform", false);
        editor.SetInspectorFoldOpen("group:light/Shadow", true);
        editor.SetInspectorFoldOpen("", false); // an empty key is not a fold
        EXPECT_FALSE(editor.IsInspectorFoldOpen("section:transform", true));
        EXPECT_TRUE(editor.IsInspectorFoldOpen("group:light/Shadow", false));
        EXPECT_TRUE(editor.IsInspectorFoldOpen("", true));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_inspector_folds_reload.uvscene");
        reloaded.Init();
        EXPECT_FALSE(reloaded.IsInspectorFoldOpen("section:transform", true));
        EXPECT_TRUE(reloaded.IsInspectorFoldOpen("group:light/Shadow", false));

        // Bounded: past the cap, new keys are not remembered, but existing ones still update.
        for (std::size_t index = 0U; index < Editor::kMaxRememberedInspectorFolds + 10U; ++index) {
            reloaded.SetInspectorFoldOpen("section:filler-" + std::to_string(index), false);
        }
        EXPECT_TRUE(reloaded.IsInspectorFoldOpen(
            "section:filler-" + std::to_string(Editor::kMaxRememberedInspectorFolds + 5U), true));
        reloaded.SetInspectorFoldOpen("section:transform", true);
        EXPECT_TRUE(reloaded.IsInspectorFoldOpen("section:transform", false));
        reloaded.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, InspectorFolds_MigratesLegacyObjectEntriesToTypedStringList) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
    settings.SetInt("editor.inspector.folds.count", 2);
    settings.SetString("editor.inspector.folds.0.key", "section:transform");
    settings.SetBool("editor.inspector.folds.0.open", false);
    settings.SetString("editor.inspector.folds.1.key", "group:light/Shadow");
    settings.SetBool("editor.inspector.folds.1.open", true);
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_inspector_folds_migration.uvscene");
        editor.Init();
        EXPECT_FALSE(editor.IsInspectorFoldOpen("section:transform", true));
        EXPECT_TRUE(editor.IsInspectorFoldOpen("group:light/Shadow", false));
        EXPECT_EQ(settings.GetString("editor.inspector.folds.0", ""), "0:section:transform");
        EXPECT_EQ(settings.GetString("editor.inspector.folds.1", ""), "1:group:light/Shadow");
        EXPECT_FALSE(settings.HasKey("editor.inspector.folds.0.key"));
        EXPECT_FALSE(settings.HasKey("editor.inspector.folds.0.open"));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportGrid_RefusesBadOpacityAndPersistsAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_grid_prefs.uvscene");
        editor.Init();
        EXPECT_TRUE(editor.IsViewportGridVisible());
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacity(), 1.0F);

        // Out of range, zero (a hidden grid under another name) and NaN are refused whole.
        EXPECT_FALSE(editor.SetViewportGrid(false, 1.5F));
        EXPECT_FALSE(editor.SetViewportGrid(false, 0.0F));
        EXPECT_FALSE(editor.SetViewportGrid(false, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(editor.IsViewportGridVisible());
        EXPECT_FLOAT_EQ(editor.GetViewportGridOpacity(), 1.0F);

        ASSERT_TRUE(editor.SetViewportGrid(false, 0.35F));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_grid_prefs_reload.uvscene");
        reloaded.Init();
        EXPECT_FALSE(reloaded.IsViewportGridVisible());
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridOpacity(), 0.35F);
        reloaded.Shutdown();
    }

    // A corrupt stored opacity falls back to its default alone; the stored visibility survives it.
    engine.GetServices().GetConfigManager().SetDouble("editor.viewport.grid.opacity", 7.0);
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_grid_prefs_corrupt.uvscene");
        corrupt.Init();
        EXPECT_FALSE(corrupt.IsViewportGridVisible());
        EXPECT_FLOAT_EQ(corrupt.GetViewportGridOpacity(), 1.0F);
        corrupt.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportGridCellSize_RefusesBadSizesAndPersistsAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_grid_cell.uvscene");
        editor.Init();
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSize(), 1.0F);

        // Zero and negative sizes would blank the grid; NaN, infinity and sizes past the range
        // are refused too, and each refusal leaves the size as it was.
        EXPECT_FALSE(editor.SetViewportGridCellSize(0.0F));
        EXPECT_FALSE(editor.SetViewportGridCellSize(-1.0F));
        EXPECT_FALSE(editor.SetViewportGridCellSize(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_FALSE(editor.SetViewportGridCellSize(std::numeric_limits<float>::infinity()));
        EXPECT_FALSE(editor.SetViewportGridCellSize(Editor::kMinimumViewportGridCellSize * 0.5F));
        EXPECT_FALSE(editor.SetViewportGridCellSize(Editor::kMaximumViewportGridCellSize * 2.0F));
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSize(), 1.0F);

        ASSERT_TRUE(editor.SetViewportGridCellSize(0.25F));
        EXPECT_FLOAT_EQ(editor.GetViewportGridCellSize(), 0.25F);
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_grid_cell_reload.uvscene");
        reloaded.Init();
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridCellSize(), 0.25F);
        reloaded.Shutdown();
    }

    // A corrupt stored size falls back to the 1 m default.
    engine.GetServices().GetConfigManager().SetDouble("editor.viewport.grid.cellSize", -3.0);
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_grid_cell_corrupt.uvscene");
        corrupt.Init();
        EXPECT_FLOAT_EQ(corrupt.GetViewportGridCellSize(), 1.0F);
        corrupt.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportSelectionOutline_RefusesBadValuesAndPersistsAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    using Color = Editor::ViewportAxisColor;
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_outline_prefs.uvscene");
        editor.Init();
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisible());
        EXPECT_FLOAT_EQ(editor.GetViewportSelectionOutlineThickness(), 2.0F);

        // Each bad value is refused whole: nothing changes.
        EXPECT_FALSE(editor.SetViewportSelectionOutline(false, Color{1.5F, 0.0F, 0.0F}, 2.0F));
        EXPECT_FALSE(editor.SetViewportSelectionOutline(false, Color{1.0F, 0.0F, 0.0F}, 0.5F));
        EXPECT_FALSE(editor.SetViewportSelectionOutline(false, Color{1.0F, 0.0F, 0.0F}, 7.0F));
        EXPECT_FALSE(editor.SetViewportSelectionOutline(
            false, Color{1.0F, 0.0F, 0.0F}, std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisible());

        ASSERT_TRUE(editor.SetViewportSelectionOutline(false, Color{0.2F, 0.6F, 1.0F}, 4.0F));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_outline_prefs_reload.uvscene");
        reloaded.Init();
        EXPECT_FALSE(reloaded.IsViewportSelectionOutlineVisible());
        EXPECT_FLOAT_EQ(reloaded.GetViewportSelectionOutlineColor().g, 0.6F);
        EXPECT_FLOAT_EQ(reloaded.GetViewportSelectionOutlineThickness(), 4.0F);
        reloaded.Shutdown();
    }
    // A corrupt stored thickness falls back to its default alone; visibility and colour survive it.
    engine.GetServices().GetConfigManager().SetDouble("editor.viewport.selectionOutline.thickness", 40.0);
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_outline_prefs_corrupt.uvscene");
        corrupt.Init();
        EXPECT_FALSE(corrupt.IsViewportSelectionOutlineVisible());
        EXPECT_FLOAT_EQ(corrupt.GetViewportSelectionOutlineColor().g, 0.6F);
        EXPECT_FLOAT_EQ(corrupt.GetViewportSelectionOutlineThickness(), 2.0F);
        corrupt.Shutdown();
    }
    // One bad colour channel sends the whole colour back to its default, never a mixed colour.
    engine.GetServices().GetConfigManager().SetDouble("editor.viewport.selectionOutline.b", 3.0);
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_outline_prefs_corrupt_color.uvscene");
        corrupt.Init();
        const Color color = corrupt.GetViewportSelectionOutlineColor();
        EXPECT_FLOAT_EQ(color.r, 1.0F);
        EXPECT_FLOAT_EQ(color.g, 0.62F);
        EXPECT_FLOAT_EQ(color.b, 0.16F);
        EXPECT_FALSE(corrupt.IsViewportSelectionOutlineVisible());
        corrupt.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportAxisColors_RefuseInvalidChannelsAndPersistAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    using AxisColor = Editor::ViewportAxisColor;
    const AxisColor chosenX{0.90F, 0.10F, 0.40F};
    const AxisColor chosenY{0.20F, 0.80F, 0.30F};
    const AxisColor chosenZ{0.15F, 0.45F, 0.95F};

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_axis_colors.uvscene");
        editor.Init();

        // Nothing is seeded until the host - which owns the viewport's default hues - supplies
        // them, so the editor must say so rather than report three zeroes as a chosen palette.
        EXPECT_FALSE(editor.AreViewportAxisColorsSet());

        // All three or none: one bad channel must not leave a partially applied palette.
        EXPECT_FALSE(editor.SetViewportAxisColors(AxisColor{1.5F, 0.0F, 0.0F}, chosenY, chosenZ))
            << "a channel above 1 must be refused";
        EXPECT_FALSE(editor.SetViewportAxisColors(chosenX, AxisColor{0.0F, -0.3F, 0.0F}, chosenZ))
            << "a negative channel must be refused";
        EXPECT_FALSE(editor.SetViewportAxisColors(
            chosenX, chosenY, AxisColor{0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN()}))
            << "NaN must be refused, not compared its way through";
        EXPECT_FALSE(editor.AreViewportAxisColorsSet())
            << "a refused palette must leave the state untouched, not half-written";

        ASSERT_TRUE(editor.SetViewportAxisColors(chosenX, chosenY, chosenZ));
        EXPECT_TRUE(editor.AreViewportAxisColorsSet());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(0).r, chosenX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(1).g, chosenY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(2).b, chosenZ.b);

        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
        EXPECT_FALSE(settings.HasKey("editor.viewport.axisColors.set"));
        EXPECT_DOUBLE_EQ(settings.GetDouble("editor.viewport.axisColors.x.r", 0.0), chosenX.r);
        EXPECT_DOUBLE_EQ(settings.GetDouble("editor.viewport.axisColors.y.g", 0.0), chosenY.g);
        EXPECT_DOUBLE_EQ(settings.GetDouble("editor.viewport.axisColors.z.b", 0.0), chosenZ.b);
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_axis_colors_reload.uvscene");
        reloaded.Init();
        ASSERT_TRUE(reloaded.AreViewportAxisColorsSet())
            << "a saved palette must survive LoadSessionSettings on the next Init()";
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColor(0).r, chosenX.r);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColor(0).g, chosenX.g);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColor(1).g, chosenY.g);
        EXPECT_FLOAT_EQ(reloaded.GetViewportAxisColor(2).b, chosenZ.b);

        // Reset drops the choice so the host re-seeds its own defaults; it must not write
        // default-looking values here, which would make this module a second home for them.
        reloaded.ResetViewportAxisColors();
        EXPECT_FALSE(reloaded.AreViewportAxisColorsSet());
        reloaded.Shutdown();
    }
    {
        // A corrupt settings file must cost the colour choice, not produce a viewport drawing
        // axes in a colour nobody picked.
        Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
        settings.SetBool("editor.viewport.axisColors.set", true);
        settings.SetDouble("editor.viewport.axisColors.x.r", 7.5);
        ASSERT_TRUE(settings.Save());

        Editor corrupt(engine.GetServices(), "uve_editor_tests_axis_colors_corrupt.uvscene");
        corrupt.Init();
        EXPECT_FALSE(corrupt.AreViewportAxisColorsSet())
            << "an out-of-range persisted channel must fall back to unset, not be clamped in";
        corrupt.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ViewportAxisColors_InvalidOrMissingPaletteDoesNotOverwriteHostDefaults) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    using AxisColor = Editor::ViewportAxisColor;
    const AxisColor hostX{0.12F, 0.34F, 0.56F};
    const AxisColor hostY{0.23F, 0.45F, 0.67F};
    const AxisColor hostZ{0.31F, 0.53F, 0.75F};
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_axis_colors_host_defaults.uvscene");
        editor.Init();
        ASSERT_TRUE(editor.SetViewportAxisColors(hostX, hostY, hostZ)); // the host seeds its defaults

        // With no saved palette, loading session settings must not clear the host's palette.
        EditorAccess::LoadSessionSettings(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSet());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(0).r, hostX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(1).g, hostY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(2).b, hostZ.b);

        // The legacy explicit-unset marker and a corrupt, incomplete descriptor palette are also
        // not permission to replace host defaults with the editor's placeholder values.
        Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
        settings.SetBool("editor.viewport.axisColors.set", false);
        EditorAccess::LoadSessionSettings(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSet());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(0).r, hostX.r);

        settings.SetBool("editor.viewport.axisColors.set", true);
        settings.SetDouble("editor.viewport.axisColors.x.r", 1.5);
        EditorAccess::LoadSessionSettings(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSet());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(0).r, hostX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(1).g, hostY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColor(2).b, hostZ.b);
        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, Favorites_ToggleReflectsImmediatelyAndPersistsAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const std::filesystem::path firstFavorite{"editor"};
    const std::filesystem::path secondFavorite{"retarget/UNIVEX_bone_retarget_map.json"};

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_favorites.uvscene");
        editor.Init();
        EXPECT_FALSE(EditorAccess::IsProjectPathFavorited(editor, firstFavorite));

        EditorAccess::ToggleProjectPathFavorite(editor, firstFavorite);
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(editor, firstFavorite));
        EditorAccess::ToggleProjectPathFavorite(editor, secondFavorite);
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(editor, secondFavorite));

        EditorAccess::ToggleProjectPathFavorite(editor, firstFavorite);
        EXPECT_FALSE(EditorAccess::IsProjectPathFavorited(editor, firstFavorite))
            << "toggling twice must remove the favorite again";

        EditorAccess::ToggleProjectPathFavorite(editor, firstFavorite);
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_favorites_reload.uvscene");
        editor.Init();
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(editor, firstFavorite))
            << "a favorite saved before shutdown must survive LoadSessionSettings on the next Init()";
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(editor, secondFavorite));
        EXPECT_FALSE(EditorAccess::IsProjectPathFavorited(editor, std::filesystem::path{"never-favorited"}));
        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, FavoriteProjects_PersistenceIsBoundedAndKeepsPathOrder) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    constexpr std::size_t kExpectedFavoriteLimit = 128U;
    std::vector<std::filesystem::path> favorites;
    favorites.reserve(kExpectedFavoriteLimit + 2U);
    for (std::size_t index = 0U; index < kExpectedFavoriteLimit + 2U; ++index) {
        favorites.emplace_back("favorite-" + std::to_string(index));
    }
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_favorites_bound.uvscene");
        editor.Init();
        for (const std::filesystem::path& favorite : favorites) {
            EditorAccess::ToggleProjectPathFavorite(editor, favorite);
        }
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
        EXPECT_EQ(settings.GetInt("editor.favorites.count", -1),
                  static_cast<std::int64_t>(kExpectedFavoriteLimit));
        EXPECT_EQ(settings.GetString("editor.favorites.0", ""), favorites.front().generic_string());
        EXPECT_EQ(settings.GetString(
                      "editor.favorites." + std::to_string(kExpectedFavoriteLimit - 1U), ""),
                  favorites[kExpectedFavoriteLimit - 1U].generic_string());
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_favorites_bound_reload.uvscene");
        reloaded.Init();
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(reloaded, favorites.front()));
        EXPECT_TRUE(EditorAccess::IsProjectPathFavorited(
            reloaded, favorites[kExpectedFavoriteLimit - 1U]));
        EXPECT_FALSE(EditorAccess::IsProjectPathFavorited(
            reloaded, favorites[kExpectedFavoriteLimit]));
        EXPECT_FALSE(EditorAccess::IsProjectPathFavorited(reloaded, favorites.back()));
        reloaded.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, ContentShelvesPersistAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_shelves.uvscene");
        editor.Init();
        ContentShelves& shelves = EditorAccess::GetContentShelves(editor);
        ASSERT_EQ(shelves.Create("Props"), "Props");
        ASSERT_EQ(shelves.Create("Empty"), "Empty");
        ASSERT_TRUE(shelves.AddItem("Props", "Sea/rock.uvmodel"));
        ASSERT_TRUE(shelves.AddItem("Props", "Sea/Models"));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        editor.Shutdown();
    }
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_shelves_reload.uvscene");
        editor.Init();
        const ContentShelves& shelves = EditorAccess::GetContentShelves(editor);
        ASSERT_EQ(shelves.GetAll().size(), 2U);
        EXPECT_EQ(shelves.GetAll()[0].name, "Props");
        EXPECT_EQ(shelves.GetAll()[0].items,
                  (std::vector<std::filesystem::path>{"Sea/rock.uvmodel", "Sea/Models"}));
        EXPECT_EQ(shelves.GetAll()[1].name, "Empty");
        EXPECT_TRUE(shelves.GetAll()[1].items.empty());
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, TeamShelvesLiveInTheProjectAndFollowChangesOnDisk) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    std::filesystem::path shelvesPath;
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_team_shelves.uvscene");
        editor.Init();
        shelvesPath = EditorAccess::GetSharedShelvesPath(editor);
        std::filesystem::remove(shelvesPath);
        EXPECT_EQ(shelvesPath.filename(), "project.uvshelves");
        ContentShelves& shelves = EditorAccess::GetContentShelves(editor);
        // No team shelf: no file is made.
        ASSERT_EQ(shelves.Create("Mine"), "Mine");
        ASSERT_TRUE(EditorAccess::SaveSharedShelves(editor));
        EXPECT_FALSE(std::filesystem::exists(shelvesPath));

        ASSERT_EQ(shelves.Create("Props", true), "Props");
        ASSERT_TRUE(shelves.AddItem("Props", "Sea/rock.uvmodel"));
        ASSERT_TRUE(EditorAccess::SaveSharedShelves(editor));
        ASSERT_TRUE(std::filesystem::exists(shelvesPath));
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));

        // A teammate's change arrives on disk (a pull): the editor picks it up.
        {
            std::ofstream file(shelvesPath, std::ios::trunc);
            file << R"({"version":1,"shelves":[{"name":"Props","items":["Sea/rock.uvmodel","Sea/Wave.uvtex"]}]})";
        }
        std::filesystem::last_write_time(shelvesPath,
                                         std::filesystem::last_write_time(shelvesPath) + std::chrono::seconds{2});
        EditorAccess::ReloadSharedShelvesIfChanged(editor);
        EXPECT_TRUE(shelves.Contains("Props", "Sea/Wave.uvtex"));
        EXPECT_NE(shelves.Find("Mine"), nullptr) << "a reload leaves personal shelves alone";
        editor.Shutdown();
    }
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_team_shelves_reload.uvscene");
        editor.Init();
        const ContentShelves& shelves = EditorAccess::GetContentShelves(editor);
        ASSERT_NE(shelves.Find("Props"), nullptr);
        EXPECT_TRUE(shelves.Find("Props")->shared);
        EXPECT_EQ(shelves.Find("Props")->items.size(), 2U);
        ASSERT_NE(shelves.Find("Mine"), nullptr);
        EXPECT_FALSE(shelves.Find("Mine")->shared);
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(shelvesPath);
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, SaveAllWritesOnlyWhatChanged) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "uve_editor_tests_save_all.uvscene";
    std::filesystem::remove(scenePath);
    {
        Editor editor(engine.GetServices(), scenePath);
        editor.Init();
        EXPECT_EQ(editor.SaveAll(), 0U);
        EXPECT_EQ(EditorAccess::GetContentStatusMessage(editor), "Nothing to save.");

        const Scene::Entity root = engine.GetServices().GetEntityManager().CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);
        Scene::TransformComponent moved{};
        moved.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(moved));
        ASSERT_TRUE(editor.IsSceneDirty());

        EXPECT_EQ(editor.SaveAll(), 1U);
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_TRUE(std::filesystem::exists(scenePath));
        EXPECT_EQ(EditorAccess::GetContentStatusMessage(editor), "Saved 1 file.");
        EXPECT_EQ(editor.SaveAll(), 0U);
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorTest, SelectionAndInspectorTransformEdit_ValidateLifetimeAndFiniteValues) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_selection.uvscene");
        editor.Init();

        Core::EngineServices& services = engine.GetServices();
        const Scene::Entity root = services.GetEntityManager().CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});

        editor.SelectEntity(root);
        EXPECT_EQ(editor.GetSelectedEntity(), root);

        Scene::TransformComponent edited{};
        edited.localPosition = Math::Vector3{3.0F, -2.0F, 7.0F};
        edited.localScale = Math::Vector3{2.0F, 3.0F, 4.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(edited));
        EXPECT_TRUE(editor.IsSceneDirty());
        EXPECT_EQ(services.GetEntityManager().GetComponent<Scene::TransformComponent>(root).localPosition,
                  edited.localPosition);

        edited.localScale.x = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(editor.SetSelectedLocalTransform(edited));

        services.GetEntityManager().DestroyEntity(root);
        editor.Tick();
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, MultiSelection_ToggleMaintainsOrderActiveFallbackAndSingleCommandSafety) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_multi_selection.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity first = entityManager.CreateEntity();
        const Scene::Entity second = entityManager.CreateEntity();
        AttachRoot(engine, first, Scene::TransformComponent{});
        AttachRoot(engine, second, Scene::TransformComponent{});

        editor.SelectEntity(first);
        EXPECT_EQ(editor.GetSelectedEntities(), std::vector<Scene::Entity>{first});
        EXPECT_EQ(editor.GetSelectedEntity(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelection());

        editor.ToggleEntitySelection(second);
        EXPECT_EQ(editor.GetSelectedEntities(), (std::vector<Scene::Entity>{first, second}));
        EXPECT_EQ(editor.GetSelectedEntity(), second);
        EXPECT_FALSE(editor.HasSingleDocumentSelection());

        const Scene::TransformComponent before =
            entityManager.GetComponent<Scene::TransformComponent>(second);
        EXPECT_FALSE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 1.0F));
        EXPECT_FALSE(editor.DuplicateSelectedEntity() != Scene::kInvalidEntity);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(second).localPosition,
                  before.localPosition);

        editor.ToggleEntitySelection(second);
        EXPECT_EQ(editor.GetSelectedEntities(), std::vector<Scene::Entity>{first});
        EXPECT_EQ(editor.GetSelectedEntity(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelection());

        editor.ToggleEntitySelection(second);
        editor.ToggleEntitySelection(first);
        EXPECT_EQ(editor.GetSelectedEntities(), std::vector<Scene::Entity>{second});
        EXPECT_EQ(editor.GetSelectedEntity(), second);

        editor.ToggleEntitySelection(second);
        EXPECT_TRUE(editor.GetSelectedEntities().empty());
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.HasSingleDocumentSelection());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, MultiSelection_TickPrunesStaleEntitiesAndPromotesLastLiveSelection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_multi_selection_stale.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity first = entityManager.CreateEntity();
        const Scene::Entity second = entityManager.CreateEntity();
        AttachRoot(engine, first, Scene::TransformComponent{});
        AttachRoot(engine, second, Scene::TransformComponent{});

        editor.SelectEntity(first);
        editor.ToggleEntitySelection(second);
        entityManager.DestroyEntity(second);
        editor.Tick();
        EXPECT_EQ(editor.GetSelectedEntities(), std::vector<Scene::Entity>{first});
        EXPECT_EQ(editor.GetSelectedEntity(), first);
        EXPECT_TRUE(editor.HasSingleDocumentSelection());

        entityManager.DestroyEntity(first);
        editor.Tick();
        EXPECT_TRUE(editor.GetSelectedEntities().empty());
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, CreateDocumentEntity_CreatesSelectedDirtyRootArchetypes) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_create_entities.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity empty = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(empty));
        EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(empty));
        EXPECT_FALSE(entityManager.HasComponent<Scene::CameraComponent>(empty));
        EXPECT_FALSE(entityManager.HasComponent<Scene::LightComponent>(empty));
        EXPECT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(empty));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(empty));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(empty).name, "Object3D");
        EXPECT_EQ(editor.GetSelectedEntity(), empty);
        EXPECT_TRUE(editor.IsSceneDirty());

        const Scene::Entity camera = editor.CreateDocumentEntity(EditorEntityKind::Camera);
        ASSERT_TRUE(entityManager.IsAlive(camera));
        EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(camera));
        EXPECT_TRUE(entityManager.HasComponent<Scene::CameraComponent>(camera));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(camera));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(camera).name, "Camera");
        EXPECT_EQ(editor.GetSelectedEntity(), camera);
        EXPECT_TRUE(editor.IsSceneDirty());

        const Scene::Entity directionalLight =
            editor.CreateDocumentEntity(EditorEntityKind::DirectionalLight);
        ASSERT_TRUE(entityManager.IsAlive(directionalLight));
        EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(directionalLight));
        ASSERT_TRUE(entityManager.HasComponent<Scene::LightComponent>(directionalLight));
        EXPECT_EQ(entityManager.GetComponent<Scene::LightComponent>(directionalLight).type,
                  Scene::LightType::Directional);
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(directionalLight));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(directionalLight).name, "Directional Light");
        EXPECT_EQ(editor.GetSelectedEntity(), directionalLight);
        EXPECT_TRUE(editor.IsSceneDirty());

        const Scene::Entity collisionBox = editor.CreateDocumentEntity(EditorEntityKind::CollisionBox);
        ASSERT_TRUE(entityManager.IsAlive(collisionBox));
        EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(collisionBox));
        EXPECT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(collisionBox));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(collisionBox));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(collisionBox).name, "Collision Box");
        EXPECT_EQ(editor.GetSelectedEntity(), collisionBox);
        EXPECT_TRUE(editor.IsSceneDirty());

        const Scene::Entity cube = editor.CreateDocumentEntity(EditorEntityKind::Cube);
        ASSERT_TRUE(entityManager.IsAlive(cube));
        ASSERT_TRUE(entityManager.HasComponent<Scene::PrimitiveMeshComponent>(cube));
        ASSERT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(cube));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).kind,
                  Scene::PrimitiveMeshKind::Cube);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(cube).halfExtents,
                  (Math::Vector3{0.5F, 0.5F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(cube).name, "Cube");

        const Scene::Entity sphere = editor.CreateDocumentEntity(EditorEntityKind::UVSphere);
        ASSERT_TRUE(entityManager.IsAlive(sphere));
        ASSERT_TRUE(entityManager.HasComponent<Scene::PrimitiveMeshComponent>(sphere));
        ASSERT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(sphere));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(sphere).kind,
                  Scene::PrimitiveMeshKind::UVSphere);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(sphere).halfExtents,
                  (Math::Vector3{0.5F, 0.5F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(sphere).name, "UV Sphere");

        const Scene::Entity plane = editor.CreateDocumentEntity(EditorEntityKind::Plane);
        ASSERT_TRUE(entityManager.IsAlive(plane));
        ASSERT_TRUE(entityManager.HasComponent<Scene::PrimitiveMeshComponent>(plane));
        ASSERT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(plane));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(plane).kind,
                  Scene::PrimitiveMeshKind::Plane);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(plane).halfExtents,
                  (Math::Vector3{0.5F, 0.025F, 0.5F}));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(plane).name, "Plane");

        // One-root document: every created archetype lives under the scene root (chained by
        // creation-under-selection), and the document's single root is the SceneRoot itself.
        const std::vector<Scene::Entity> roots = editor.GetDocumentRoots();
        ASSERT_EQ(roots.size(), 1U);
        EXPECT_EQ(roots.front(), editor.GetDocumentSceneRoot());
        EXPECT_TRUE(entityManager.IsAlive(empty));
        EXPECT_TRUE(entityManager.IsAlive(camera));
        EXPECT_TRUE(entityManager.IsAlive(directionalLight));
        EXPECT_TRUE(entityManager.IsAlive(collisionBox));
        EXPECT_TRUE(entityManager.IsAlive(cube));
        EXPECT_TRUE(entityManager.IsAlive(sphere));
        EXPECT_TRUE(entityManager.IsAlive(plane));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PrimitiveAppearance_UpdatesColliderAndSupportsAtomicUndoRedo) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_primitive_appearance.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity cube = editor.CreateDocumentEntity(EditorEntityKind::Cube);
        ASSERT_TRUE(entityManager.IsAlive(cube));
        const Scene::PrimitiveMeshComponent before =
            entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube);

        const Scene::PrimitiveMeshComponent after{Scene::PrimitiveMeshKind::Plane,
                                                     Math::Vector3{0.1F, 0.4F, 0.9F}};
        ASSERT_TRUE(editor.SetSelectedPrimitiveMesh(after));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).baseColor, after.baseColor);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(cube).halfExtents,
                  (Math::Vector3{0.5F, 0.025F, 0.5F}));

        const Scene::PrimitiveMeshComponent invalid{static_cast<Scene::PrimitiveMeshKind>(99),
                                                       Math::Vector3{0.1F, 0.4F, 0.9F}};
        EXPECT_FALSE(editor.SetSelectedPrimitiveMesh(invalid));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).baseColor, after.baseColor);

        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).kind, before.kind);
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).baseColor, before.baseColor);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(cube).halfExtents,
                  (Math::Vector3{0.5F, 0.5F, 0.5F}));
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).kind, after.kind);
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(cube).baseColor, after.baseColor);
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(cube).halfExtents,
                  (Math::Vector3{0.5F, 0.025F, 0.5F}));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, CreateDocumentEntity_RejectsInvalidKindsAndNonRunningStates) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_create_invalid.uvscene");
        EXPECT_EQ(editor.CreateDocumentEntity(EditorEntityKind::Empty), Scene::kInvalidEntity);

        editor.Init();
        EXPECT_EQ(editor.CreateDocumentEntity(static_cast<EditorEntityKind>(999)),
                  Scene::kInvalidEntity);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
        EXPECT_EQ(editor.CreateDocumentEntity(EditorEntityKind::Empty), Scene::kInvalidEntity);
    }

    engine.Shutdown();
}

TEST(EditorTest, CreateDocumentEntity_AllocatesUniqueNames) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_create_names.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity firstCamera = editor.CreateDocumentEntity(EditorEntityKind::Camera);
        const Scene::Entity secondCamera = editor.CreateDocumentEntity(EditorEntityKind::Camera);
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(firstCamera));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(secondCamera));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(firstCamera).name, "Camera");
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(secondCamera).name, "Camera 2");

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, SetSelectedEntityName_ValidatesInputAndMarksDocumentDirty) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_rename.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});

        EXPECT_FALSE(editor.SetSelectedEntityName("Unselected"));
        editor.SelectEntity(root);
        ASSERT_TRUE(editor.SetSelectedEntityName("Gameplay Root"));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(root));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(root).name, "Gameplay Root");
        EXPECT_EQ(editor.GetSelectedEntity(), root);
        EXPECT_TRUE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.SetSelectedEntityName("Gameplay Root"));
        EXPECT_FALSE(editor.SetSelectedEntityName(""));
        EXPECT_FALSE(editor.SetSelectedEntityName("   \t"));
        EXPECT_FALSE(editor.SetSelectedEntityName(std::string(97U, 'n')));

        entityManager.DestroyEntity(root);
        editor.Tick();
        EXPECT_FALSE(editor.SetSelectedEntityName("Destroyed"));
        editor.Shutdown();
        EXPECT_FALSE(editor.SetSelectedEntityName("Shutdown"));
    }

    engine.Shutdown();
}

TEST(EditorTest, ObjectWarnings_ReportSetupProblemsAndScriptPath) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_object_warnings.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        // A plain, well-formed object has nothing to report and no script.
        const Scene::Entity clean = entityManager.CreateEntity();
        AttachRoot(engine, clean, Scene::TransformComponent{});
        EXPECT_TRUE(editor.GetObjectWarnings(clean).empty());
        EXPECT_FALSE(editor.GetObjectScriptPath(clean).has_value());

        // A mesh object with no mesh, and one pointing at a mesh the project does not have.
        const Scene::Entity noMesh = entityManager.CreateEntity();
        AttachRoot(engine, noMesh, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::MeshComponent>(noMesh, Scene::MeshComponent{});
        ASSERT_EQ(editor.GetObjectWarnings(noMesh).size(), 1U);
        EXPECT_NE(editor.GetObjectWarnings(noMesh).front().find("No mesh"), std::string::npos);

        const Scene::Entity lostMesh = entityManager.CreateEntity();
        AttachRoot(engine, lostMesh, Scene::TransformComponent{});
        Scene::MeshComponent lost{};
        lost.meshGuid = Asset::AssetGuid{0x12345678ULL};
        lost.materialGuid = Asset::AssetGuid{0x9abcdef0ULL};
        entityManager.AddComponent<Scene::MeshComponent>(lostMesh, lost);
        EXPECT_EQ(editor.GetObjectWarnings(lostMesh).size(), 2U) << "missing mesh and missing material";

        // A script: a valid path is reported as the object's script; an invalid one is a warning too.
        const Scene::Entity scripted = entityManager.CreateEntity();
        AttachRoot(engine, scripted, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::ScriptComponent>(scripted, Scene::ScriptComponent{"scripts/player.uvs"});
        ASSERT_TRUE(editor.GetObjectScriptPath(scripted).has_value());
        EXPECT_EQ(*editor.GetObjectScriptPath(scripted), "scripts/player.uvs");
        EXPECT_TRUE(editor.GetObjectWarnings(scripted).empty());
        entityManager.GetComponent<Scene::ScriptComponent>(scripted).scriptAssetPath = "../outside.uvs";
        EXPECT_EQ(editor.GetObjectWarnings(scripted).size(), 1U);

        // A Skeleton3D with no source model has no bones to show.
        const Scene::Entity skeleton = entityManager.CreateEntity();
        AttachRoot(engine, skeleton, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::Skeleton3DComponent>(skeleton, Scene::Skeleton3DComponent{});
        EXPECT_EQ(editor.GetObjectWarnings(skeleton).size(), 1U);

        // Not a document entity: nothing to say.
        EXPECT_TRUE(editor.GetObjectWarnings(Scene::kInvalidEntity).empty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, InspectorClipboard_CopyPasteResetComponentsAndTransformUndoably) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_inspector_clipboard.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Core::TypeMetadataRegistry& registry = Scene::GetSceneComponentMetadataRegistry();
        const Core::TypeMetadataEntry* const primitive =
            registry.FindTypeByIndex(std::type_index(typeid(Scene::PrimitiveMeshComponent)));
        const Core::TypeMetadataEntry* const visibility =
            registry.FindTypeByIndex(std::type_index(typeid(Scene::VisibilityComponent)));
        ASSERT_NE(primitive, nullptr);
        ASSERT_NE(visibility, nullptr);

        const Scene::Entity source = entityManager.CreateEntity();
        const Scene::Entity target = entityManager.CreateEntity();
        Scene::TransformComponent placed{};
        placed.localPosition = Math::Vector3{3.0F, 1.0F, -2.0F};
        placed.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
        AttachRoot(engine, source, placed);
        AttachRoot(engine, target, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
            source, Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::UVSphere, Math::Vector3{0.9F, 0.1F, 0.2F}});
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(target, Scene::PrimitiveMeshComponent{});

        // Nothing copied yet: nothing to paste.
        editor.SelectEntity(target);
        EXPECT_FALSE(editor.CanPasteSelectedComponent(*primitive));
        EXPECT_FALSE(editor.PasteSelectedComponent(*primitive));

        editor.SelectEntity(source);
        ASSERT_TRUE(editor.CopySelectedComponent(*primitive));
        ASSERT_TRUE(editor.CopySelectedTransform());
        // A clipboard of one type never pastes into another.
        EXPECT_FALSE(editor.CanPasteSelectedComponent(*visibility));

        editor.SelectEntity(target);
        ASSERT_TRUE(editor.PasteSelectedComponent(*primitive));
        const auto& pasted = entityManager.GetComponent<Scene::PrimitiveMeshComponent>(target);
        EXPECT_EQ(pasted.kind, Scene::PrimitiveMeshKind::UVSphere);
        EXPECT_FLOAT_EQ(pasted.baseColor.x, 0.9F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(target).kind,
                  Scene::PrimitiveMeshKind::Cube);

        // Transform: only the local pose travels.
        ASSERT_TRUE(editor.PasteSelectedTransform());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(target).localPosition,
                  placed.localPosition);
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(target).localScale, placed.localScale);

        // Reset writes the type's defaults, and is one undoable step.
        editor.SelectEntity(source);
        ASSERT_TRUE(editor.ResetSelectedComponent(*primitive));
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(source).kind,
                  Scene::PrimitiveMeshComponent{}.kind);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::PrimitiveMeshComponent>(source).kind,
                  Scene::PrimitiveMeshKind::UVSphere);
        ASSERT_TRUE(editor.ResetSelectedTransform());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(source).localPosition, Math::Vector3{});
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(source).localScale,
                  (Math::Vector3{1.0F, 1.0F, 1.0F}));

        // An object without the component cannot be copied from.
        editor.ClearSelection();
        EXPECT_FALSE(editor.CopySelectedComponent(*primitive));
        EXPECT_FALSE(editor.CopySelectedTransform());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, SetEntityVisible_TogglesAnyRowUndoablyWithoutTouchingSelection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_visibility_toggle.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity shown = entityManager.CreateEntity();
        AttachRoot(engine, shown, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::VisibilityComponent>(shown, Scene::VisibilityComponent{});
        const Scene::Entity other = entityManager.CreateEntity();
        AttachRoot(engine, other, Scene::TransformComponent{});
        editor.SelectEntity(other);

        // The eye works on a row that is not selected, and leaves the selection alone.
        ASSERT_TRUE(editor.SetEntityVisible(shown, false));
        EXPECT_FALSE(entityManager.GetComponent<Scene::VisibilityComponent>(shown).visible);
        EXPECT_EQ(editor.GetSelectedEntity(), other);
        EXPECT_TRUE(editor.IsSceneDirty());

        // Setting the value it already has is not an edit.
        EXPECT_FALSE(editor.SetEntityVisible(shown, false));

        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.GetComponent<Scene::VisibilityComponent>(shown).visible);
        EXPECT_EQ(editor.GetSelectedEntity(), other);
        EXPECT_FALSE(editor.IsSceneDirty());

        ASSERT_TRUE(editor.Redo());
        EXPECT_FALSE(entityManager.GetComponent<Scene::VisibilityComponent>(shown).visible);

        // An object without a Visibility component has no eye to toggle.
        EXPECT_FALSE(entityManager.HasComponent<Scene::VisibilityComponent>(other));
        EXPECT_FALSE(editor.SetEntityVisible(other, false));
        EXPECT_FALSE(editor.SetEntityVisible(Scene::kInvalidEntity, false));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, ViewportFocusRequest_OnlyForObjectsWithAPlaceInTheScene) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_viewport_focus.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity placed = entityManager.CreateEntity();
        AttachRoot(engine, placed, Scene::TransformComponent{});
        // An entity with no transform is like the scene root or a plain Object: nothing to look at.
        const Scene::Entity unplaced = entityManager.CreateEntity();

        const std::uint32_t serialBefore = editor.GetViewportFocusRequestSerial();
        EXPECT_TRUE(editor.CanFocusEntityInViewport(placed));
        ASSERT_TRUE(editor.RequestViewportFocus(placed));
        EXPECT_EQ(editor.GetViewportFocusEntity(), placed);
        EXPECT_EQ(editor.GetViewportFocusRequestSerial(), serialBefore + 1U);

        // Asking again is a new request, so the host focuses again even on the same object.
        ASSERT_TRUE(editor.RequestViewportFocus(placed));
        EXPECT_EQ(editor.GetViewportFocusRequestSerial(), serialBefore + 2U);

        // Refused requests leave the last one as it was.
        EXPECT_FALSE(editor.CanFocusEntityInViewport(unplaced));
        EXPECT_FALSE(editor.RequestViewportFocus(unplaced));
        EXPECT_FALSE(editor.RequestViewportFocus(Scene::kInvalidEntity));
        EXPECT_EQ(editor.GetViewportFocusEntity(), placed);
        EXPECT_EQ(editor.GetViewportFocusRequestSerial(), serialBefore + 2U);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, HierarchyBranchOpen_QueuesEveryRowWithChildrenInTheBranch) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_hierarchy_branch.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const auto makeObject = [&](const Scene::Entity parent) {
            const Scene::Entity object = entityManager.CreateEntity();
            AttachRoot(engine, object, Scene::TransformComponent{});
            if (parent != Scene::kInvalidEntity) {
                services.GetSceneGraph().SetParent(entityManager, object, parent);
            }
            return object;
        };
        // branch -> middle -> leaf, branch -> sibling leaf; and a separate tree beside it.
        const Scene::Entity branch = makeObject(Scene::kInvalidEntity);
        const Scene::Entity middle = makeObject(branch);
        const Scene::Entity leaf = makeObject(middle);
        const Scene::Entity siblingLeaf = makeObject(branch);
        const Scene::Entity otherRoot = makeObject(Scene::kInvalidEntity);
        const Scene::Entity otherChild = makeObject(otherRoot);

        ASSERT_TRUE(editor.SetHierarchyBranchOpen(branch, true));
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(branch), std::optional<bool>{true});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(middle), std::optional<bool>{true});
        // Leaves have nothing to open, and the tree beside it is not part of the branch.
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpen(leaf).has_value());
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpen(siblingLeaf).has_value());
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpen(otherRoot).has_value());

        // Collapsing replaces the pending state for the whole branch.
        ASSERT_TRUE(editor.SetHierarchyBranchOpen(branch, false));
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(branch), std::optional<bool>{false});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(middle), std::optional<bool>{false});

        // A pending row that is deleted before it is drawn is dropped by the next request.
        entityManager.DestroyEntity(middle);
        ASSERT_TRUE(editor.SetHierarchyBranchOpen(otherRoot, true));
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpen(middle).has_value());
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(branch), std::optional<bool>{false});
        EXPECT_EQ(editor.GetPendingHierarchyRowOpen(otherRoot), std::optional<bool>{true});
        EXPECT_FALSE(editor.GetPendingHierarchyRowOpen(otherChild).has_value());

        EXPECT_FALSE(editor.SetHierarchyBranchOpen(Scene::kInvalidEntity, true));
        EXPECT_FALSE(editor.SetHierarchyBranchOpen(middle, true));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_TransformUndoRedoRestoresSelectionAndDirtyState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_history_transform.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        Scene::TransformComponent moved{};
        moved.localPosition = Math::Vector3{4.0F, -3.0F, 2.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(moved));
        EXPECT_TRUE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());
        EXPECT_TRUE(editor.IsSceneDirty());

        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(editor.GetSelectedEntity(), root);
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(root).localPosition,
                  Math::Vector3{});
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_TRUE(editor.CanRedo());

        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(editor.GetSelectedEntity(), root);
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(root).localPosition,
                  moved.localPosition);
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_NameUndoRedoRestoresOptionalComponentState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_history_name.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        ASSERT_TRUE(editor.SetSelectedEntityName("Level Root"));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(root));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(root).name, "Level Root");
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.HasComponent<Scene::NameComponent>(root));
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Redo());
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(root));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(root).name, "Level Root");
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_CreationUndoRedoRecreatesArchetypeAndName) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_history_create.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity created = editor.CreateDocumentEntity(EditorEntityKind::CollisionBox);
        ASSERT_TRUE(entityManager.IsAlive(created));
        ASSERT_TRUE(editor.CanUndo());
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(created));
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.IsSceneDirty());

        ASSERT_TRUE(editor.Redo());
        const Scene::Entity recreated = editor.GetSelectedEntity();
        ASSERT_TRUE(entityManager.IsAlive(recreated));
        EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(recreated));
        EXPECT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(recreated));
        ASSERT_TRUE(entityManager.HasComponent<Scene::NameComponent>(recreated));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(recreated).name, "Collision Box");
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_NewMutationClearsRedoAndCapacityDiscardsOldestCommand) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_history_capacity.uvscene", 1U);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        Scene::TransformComponent first{};
        first.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(first));
        Scene::TransformComponent second = first;
        second.localPosition = Math::Vector3{2.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(second));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(root).localPosition,
                  first.localPosition);
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_TRUE(editor.CanRedo());

        Scene::TransformComponent third = first;
        third.localPosition = Math::Vector3{3.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(third));
        EXPECT_FALSE(editor.CanRedo());
        EXPECT_FALSE(editor.SetSelectedLocalTransform(third));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_StaleTargetsAndNonRunningStateFailWithoutMutation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_history_stale.uvscene");
        EXPECT_FALSE(editor.Undo());
        EXPECT_FALSE(editor.Redo());
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        Scene::TransformComponent moved{};
        moved.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(moved));
        entityManager.DestroyEntity(root);
        editor.Tick();
        EXPECT_FALSE(editor.Undo());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());

        editor.Shutdown();
        EXPECT_FALSE(editor.Undo());
        EXPECT_FALSE(editor.Redo());
    }

    engine.Shutdown();
}

TEST(EditorTest, DuplicateSelectedEntity_RootCreatesNamedSiblingWithCopiedComponents) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_duplicate_root.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity source = entityManager.CreateEntity();
        Scene::TransformComponent sourceTransform{};
        sourceTransform.localPosition = Math::Vector3{2.0F, 4.0F, 6.0F};
        AttachRoot(engine, source, sourceTransform);
        entityManager.AddComponent<Scene::ColliderComponent>(source);
        entityManager.AddComponent<Scene::NameComponent>(source, Scene::NameComponent{"Lamp"});
        editor.SelectEntity(source);

        const Scene::Entity duplicate = editor.DuplicateSelectedEntity();
        ASSERT_TRUE(entityManager.IsAlive(duplicate));
        EXPECT_NE(duplicate, source);
        EXPECT_EQ(editor.GetSelectedEntity(), duplicate);
        EXPECT_TRUE(editor.IsSceneDirty());
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(duplicate).name, "Lamp 2");
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(duplicate).localPosition,
                  sourceTransform.localPosition);
        EXPECT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(duplicate));

        const std::vector<Scene::Entity> roots = editor.GetDocumentRoots();
        ASSERT_EQ(roots.size(), 3U); // the scene root + the raw source + its duplicate
        EXPECT_NE(std::find(roots.begin(), roots.end(), source), roots.end());
        EXPECT_NE(std::find(roots.begin(), roots.end(), duplicate), roots.end());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, DuplicateSelectedEntity_ChildRestoresAsSiblingUnderSameParent) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_duplicate_child.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        entityManager.AddComponent<Scene::NameComponent>(child, Scene::NameComponent{"Child"});
        editor.SelectEntity(child);

        const Scene::Entity duplicate = editor.DuplicateSelectedEntity();
        ASSERT_TRUE(entityManager.IsAlive(duplicate));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(duplicate).name, "Child 2");
        const std::vector<Scene::Entity> children =
            services.GetSceneGraph().GetChildren(entityManager, parent);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_NE(std::find(children.begin(), children.end(), child), children.end());
        EXPECT_NE(std::find(children.begin(), children.end(), duplicate), children.end());
        EXPECT_EQ(editor.GetSelectedEntity(), duplicate);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, DeleteSelectedEntity_DeletesSubtreeAndSelectsLiveParent) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_delete_subtree.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        const Scene::Entity grandchild = entityManager.CreateEntity();
        AttachRoot(engine, grandchild, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, grandchild, child);
        editor.SelectEntity(child);

        ASSERT_TRUE(editor.DeleteSelectedEntity());
        EXPECT_FALSE(entityManager.IsAlive(child));
        EXPECT_FALSE(entityManager.IsAlive(grandchild));
        EXPECT_TRUE(entityManager.IsAlive(parent));
        EXPECT_EQ(editor.GetSelectedEntity(), parent);
        EXPECT_TRUE(editor.IsSceneDirty());
        EXPECT_TRUE(editor.CanUndo());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, DeleteSelectedEntity_RootClearsSelection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_delete_root.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        editor.SelectEntity(root);

        ASSERT_TRUE(editor.DeleteSelectedEntity());
        EXPECT_FALSE(entityManager.IsAlive(root));
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_DuplicateUndoRedoUsesFreshHandlesAndRestoresDirtySelection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_duplicate_history.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity source = entityManager.CreateEntity();
        AttachRoot(engine, source, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(source, Scene::NameComponent{"Actor"});
        editor.SelectEntity(source);

        const Scene::Entity duplicate = editor.DuplicateSelectedEntity();
        ASSERT_TRUE(entityManager.IsAlive(duplicate));
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(duplicate));
        EXPECT_EQ(editor.GetSelectedEntity(), source);
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Redo());
        const Scene::Entity recreated = editor.GetSelectedEntity();
        EXPECT_TRUE(entityManager.IsAlive(recreated));
        EXPECT_NE(recreated, duplicate);
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(recreated).name, "Actor 2");
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_DeleteUndoRedoRestoresSubtreeUnderOriginalParentWithFreshHandles) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_delete_history.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        entityManager.AddComponent<Scene::NameComponent>(child, Scene::NameComponent{"Deleted Child"});
        editor.SelectEntity(child);

        ASSERT_TRUE(editor.DeleteSelectedEntity());
        EXPECT_FALSE(entityManager.IsAlive(child));
        EXPECT_EQ(editor.GetSelectedEntity(), parent);
        ASSERT_TRUE(editor.Undo());
        const Scene::Entity restored = editor.GetSelectedEntity();
        EXPECT_TRUE(entityManager.IsAlive(restored));
        EXPECT_NE(restored, child);
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(restored).name, "Deleted Child");
        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(entityManager, parent);
        EXPECT_NE(std::find(children.begin(), children.end(), restored), children.end());
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Redo());
        EXPECT_FALSE(entityManager.IsAlive(restored));
        EXPECT_EQ(editor.GetSelectedEntity(), parent);
        EXPECT_TRUE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_DeleteUndoRejectsStaleParentAndClearsTimeline) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_delete_stale_parent.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        editor.SelectEntity(child);

        ASSERT_TRUE(editor.DeleteSelectedEntity());
        entityManager.DestroyEntity(parent);
        EXPECT_FALSE(editor.Undo());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());
        // The ever-present scene root is the only thing left in the document.
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_NewMutationAfterDuplicateUndoInvalidatesRedo) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_duplicate_redo_invalidation.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity source = entityManager.CreateEntity();
        AttachRoot(engine, source, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(source, Scene::NameComponent{"Source"});
        editor.SelectEntity(source);

        ASSERT_NE(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.CanRedo());
        ASSERT_TRUE(editor.SetSelectedEntityName("Source Revised"));
        EXPECT_FALSE(editor.CanRedo());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EntityLifecycle_RejectsUnselectedStaleNonRunningAndUnsupportedCapture) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_lifecycle_safety.uvscene");
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        editor.Init();
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.DeleteSelectedEntity());

        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity unsupported = entityManager.CreateEntity();
        AttachRoot(engine, unsupported, Scene::TransformComponent{});
        entityManager.AddComponent<UnregisteredEditorLifecycleComponent>(
            unsupported, UnregisteredEditorLifecycleComponent{7});
        editor.SelectEntity(unsupported);
        const std::size_t entityCountBefore = entityManager.GetEntityCount();
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.IsAlive(unsupported));
        EXPECT_EQ(entityManager.GetEntityCount(), entityCountBefore);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        EXPECT_TRUE(entityManager.IsAlive(unsupported));

        entityManager.DestroyEntity(unsupported);
        editor.Tick();
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        editor.Shutdown();
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
    }

    engine.Shutdown();
}

TEST(EditorTest, ReparentSelectedEntity_RootMovesBelowTargetAndPreservesLocalTransform) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_root.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity target = entityManager.CreateEntity();
        AttachRoot(engine, target, Scene::TransformComponent{});
        const Scene::Entity moved = entityManager.CreateEntity();
        Scene::TransformComponent localTransform{};
        localTransform.localPosition = Math::Vector3{2.0F, -3.0F, 7.0F};
        AttachRoot(engine, moved, localTransform);
        const Scene::Entity folder = PutInObjectFolder(engine, editor, {target, moved});
        editor.SelectEntity(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntity(target));
        const std::vector<Scene::Entity> targetChildren =
            services.GetSceneGraph().GetChildren(entityManager, target);
        EXPECT_NE(std::find(targetChildren.begin(), targetChildren.end(), moved), targetChildren.end());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(moved).localPosition,
                  localTransform.localPosition);
        EXPECT_EQ(editor.GetSelectedEntity(), moved);
        EXPECT_TRUE(editor.IsSceneDirty());
        EXPECT_TRUE(editor.CanUndo());
        const std::vector<Scene::Entity> folderChildren =
            services.GetSceneGraph().GetChildren(entityManager, folder);
        EXPECT_EQ(folderChildren, std::vector<Scene::Entity>{target}); // the target stays in the folder

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, ReparentSelectedEntity_ChildCanReturnToRootWithoutDetachingDescendants) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_root_detach.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        const Scene::Entity grandchild = entityManager.CreateEntity();
        AttachRoot(engine, grandchild, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, grandchild, child);
        const Scene::Entity folder = PutInObjectFolder(engine, editor, {parent});
        editor.SelectEntity(child);

        ASSERT_TRUE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_TRUE(services.GetSceneGraph().GetChildren(entityManager, parent).empty());
        // "Return to root" in a level means the top of the object's folder.
        const std::vector<Scene::Entity> sceneRootChildren =
            services.GetSceneGraph().GetChildren(entityManager, folder);
        EXPECT_NE(std::find(sceneRootChildren.begin(), sceneRootChildren.end(), child), sceneRootChildren.end());
        const std::vector<Scene::Entity> childChildren =
            services.GetSceneGraph().GetChildren(entityManager, child);
        EXPECT_NE(std::find(childChildren.begin(), childChildren.end(), grandchild), childChildren.end());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_ReparentUndoRedoRestoresParentsSelectionAndDirtyState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_history.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity oldParent = entityManager.CreateEntity();
        AttachRoot(engine, oldParent, Scene::TransformComponent{});
        const Scene::Entity newParent = entityManager.CreateEntity();
        AttachRoot(engine, newParent, Scene::TransformComponent{});
        const Scene::Entity moved = entityManager.CreateEntity();
        AttachRoot(engine, moved, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, moved, oldParent);
        entityManager.AddComponent<Scene::NameComponent>(moved, Scene::NameComponent{"Moved"});
        static_cast<void>(PutInObjectFolder(engine, editor, {oldParent, newParent}));
        editor.SelectEntity(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntity(newParent));
        const std::vector<Scene::Entity> childrenAfterReparent =
            services.GetSceneGraph().GetChildren(entityManager, newParent);
        EXPECT_NE(std::find(childrenAfterReparent.begin(), childrenAfterReparent.end(), moved),
                  childrenAfterReparent.end());
        ASSERT_TRUE(editor.Undo());
        const std::vector<Scene::Entity> childrenAfterUndo =
            services.GetSceneGraph().GetChildren(entityManager, oldParent);
        EXPECT_NE(std::find(childrenAfterUndo.begin(), childrenAfterUndo.end(), moved), childrenAfterUndo.end());
        EXPECT_EQ(editor.GetSelectedEntity(), moved);
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Redo());
        const std::vector<Scene::Entity> childrenAfterRedo =
            services.GetSceneGraph().GetChildren(entityManager, newParent);
        EXPECT_NE(std::find(childrenAfterRedo.begin(), childrenAfterRedo.end(), moved), childrenAfterRedo.end());
        EXPECT_TRUE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Undo());
        ASSERT_TRUE(editor.SetSelectedEntityName("Moved Again"));
        EXPECT_FALSE(editor.CanRedo());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, SceneRoot_ChildrenOfTheTransformlessRootStayFullyEditable) {
    // The scene root is a pure Object: in the hierarchy, with no transform. Everything that used to
    // read the parent's world transform must treat it as identity, the way the scene graph does -
    // otherwise every direct child of the root would refuse to move, rotate or be reparented.
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_scene_root_children.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        // In a level, objects sit in a folder - a transformless pure Object, like the scene root.
        const Scene::Entity sceneRoot = EditorAccess::GetObjectFolder(editor);
        ASSERT_NE(sceneRoot, Scene::kInvalidEntity);
        ASSERT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(sceneRoot));

        const Scene::Entity parent = entityManager.CreateEntity();
        Scene::TransformComponent parentTransform{};
        parentTransform.localPosition = Math::Vector3{0.0F, 10.0F, 0.0F};
        AttachRoot(engine, parent, parentTransform);
        services.GetSceneGraph().SetParent(entityManager, parent, sceneRoot);
        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent childTransform{};
        childTransform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, child, childTransform);
        services.GetSceneGraph().SetParent(entityManager, child, sceneRoot);
        services.GetSceneGraph().Update(entityManager);

        // A gizmo drag on a direct child of the root: world delta == local delta.
        editor.SelectEntity(child);
        ASSERT_TRUE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 2.0F));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(child).localPosition,
                  (Math::Vector3{3.0F, 2.0F, 3.0F}));
        EXPECT_TRUE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::Y, 0.5F));
        services.GetSceneGraph().Update(entityManager);

        // Keep-world reparent out from under the root and back onto it by name, then undo and
        // redo both: every step names the root as a parent, which a spatial-only check refused.
        ASSERT_TRUE(editor.SetReparentTransformMode(EditorReparentTransformMode::KeepWorld));
        const Math::Vector3 worldBefore =
            entityManager.GetComponent<Scene::WorldTransformComponent>(child).worldPosition;
        ASSERT_TRUE(editor.ReparentSelectedEntity(parent));
        services.GetSceneGraph().Update(entityManager);
        EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(child).worldPosition.y,
                    worldBefore.y, 1.0e-5F);
        ASSERT_TRUE(editor.ReparentSelectedEntity(sceneRoot));
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, sceneRoot);
        services.GetSceneGraph().Update(entityManager);
        EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(child).worldPosition.y,
                    worldBefore.y, 1.0e-5F);

        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, parent);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, sceneRoot);
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, parent);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_RedoOfMoveToDocumentRootLandsUnderTheSceneRoot) {
    // "Move to document root" is spelled as no parent, and means the scene root. History has to
    // record the parent the entity actually got, or redo would set no parent at all and leave a
    // stray beside the root - a second top-level object in a one-root document.
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_to_root_redo.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        // In a level, "the top" for an object is its folder.
        const Scene::Entity sceneRoot = EditorAccess::GetObjectFolder(editor);
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, parent, sceneRoot);
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        editor.SelectEntity(child);

        ASSERT_TRUE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, sceneRoot);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, parent);
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(child).parent, sceneRoot);
        EXPECT_EQ(editor.GetDocumentRoots(), std::vector<Scene::Entity>{editor.GetDocumentSceneRoot()});

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, ReparentSelectedEntity_RejectsCyclesNoOpNonDocumentStaleAndNonRunningStates) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_safety.uvscene");
        EXPECT_FALSE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        AttachRoot(engine, child, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, child, root);
        editor.SelectEntity(root);
        EXPECT_FALSE(editor.ReparentSelectedEntity(root));
        EXPECT_FALSE(editor.ReparentSelectedEntity(child));
        // kInvalidEntity as the new parent now means "move under the scene root" (see
        // ReparentDocumentEntity), so rejection is exercised with a dead handle instead.
        EXPECT_FALSE(editor.ReparentSelectedEntity(Scene::Entity{9999U, 1U}));
        editor.SelectEntity(child);
        EXPECT_FALSE(editor.ReparentSelectedEntity(root));
        const Scene::Entity nonDocumentEntity = entityManager.CreateEntity();
        editor.SelectEntity(nonDocumentEntity);
        EXPECT_FALSE(editor.ReparentSelectedEntity(root));

        const Scene::Entity staleTarget = entityManager.CreateEntity();
        AttachRoot(engine, staleTarget, Scene::TransformComponent{});
        entityManager.DestroyEntity(staleTarget);
        editor.SelectEntity(root);
        EXPECT_FALSE(editor.ReparentSelectedEntity(staleTarget));

        const Scene::Entity malformed = entityManager.CreateEntity();
        editor.SelectEntity(malformed);
        EXPECT_FALSE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        editor.Shutdown();
        EXPECT_FALSE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
    }

    engine.Shutdown();
}

TEST(EditorTest, EditorHistory_ReparentUndoRejectsStalePriorParentAndClearsTimeline) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_reparent_stale_parent.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity oldParent = entityManager.CreateEntity();
        AttachRoot(engine, oldParent, Scene::TransformComponent{});
        const Scene::Entity newParent = entityManager.CreateEntity();
        AttachRoot(engine, newParent, Scene::TransformComponent{});
        const Scene::Entity moved = entityManager.CreateEntity();
        AttachRoot(engine, moved, Scene::TransformComponent{});
        services.GetSceneGraph().SetParent(entityManager, moved, oldParent);
        static_cast<void>(PutInObjectFolder(engine, editor, {oldParent, newParent}));
        editor.SelectEntity(moved);

        ASSERT_TRUE(editor.ReparentSelectedEntity(newParent));
        entityManager.DestroyEntity(oldParent);
        EXPECT_FALSE(editor.Undo());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, KeepWorldReparent_PreservesCompatibleWorldTrsAndHistory) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_keep_world_reparent.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        Scene::TransformComponent parentTransform{};
        parentTransform.localPosition = Math::Vector3{10.0F, -2.0F, 5.0F};
        parentTransform.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.5F,
                                              parentTransform.localRotation));
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, parentTransform);
        Scene::TransformComponent movedTransform{};
        movedTransform.localPosition = Math::Vector3{4.0F, 3.0F, -2.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              movedTransform.localRotation));
        const Scene::Entity moved = entityManager.CreateEntity();
        AttachRoot(engine, moved, movedTransform);
        static_cast<void>(PutInObjectFolder(engine, editor, {parent, moved}));
        services.GetSceneGraph().Update(entityManager);
        const Scene::WorldTransformComponent worldBefore =
            entityManager.GetComponent<Scene::WorldTransformComponent>(moved);
        editor.SelectEntity(moved);
        ASSERT_TRUE(editor.SetReparentTransformMode(EditorReparentTransformMode::KeepWorld));
        ASSERT_TRUE(editor.ReparentSelectedEntity(parent));
        services.GetSceneGraph().Update(entityManager);
        const Scene::WorldTransformComponent& worldAfter =
            entityManager.GetComponent<Scene::WorldTransformComponent>(moved);
        EXPECT_NEAR(worldAfter.worldPosition.x, worldBefore.worldPosition.x, 0.0001F);
        EXPECT_NEAR(worldAfter.worldPosition.y, worldBefore.worldPosition.y, 0.0001F);
        EXPECT_NEAR(worldAfter.worldPosition.z, worldBefore.worldPosition.z, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.x, worldBefore.worldScale.x, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.y, worldBefore.worldScale.y, 0.0001F);
        EXPECT_NEAR(worldAfter.worldScale.z, worldBefore.worldScale.z, 0.0001F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(moved).localPosition,
                  movedTransform.localPosition);
        ASSERT_TRUE(editor.Redo());
        EXPECT_NE(entityManager.GetComponent<Scene::TransformComponent>(moved).localPosition,
                  movedTransform.localPosition);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, KeepWorldReparent_RejectsShearProneAndNearZeroScaleParents) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_keep_world_reject.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        Scene::TransformComponent shearParentTransform{};
        shearParentTransform.localScale = Math::Vector3{2.0F, 3.0F, 4.0F};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              shearParentTransform.localRotation));
        const Scene::Entity shearParent = entityManager.CreateEntity();
        AttachRoot(engine, shearParent, shearParentTransform);
        Scene::TransformComponent movedTransform{};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.25F,
                                              movedTransform.localRotation));
        const Scene::Entity moved = entityManager.CreateEntity();
        AttachRoot(engine, moved, movedTransform);
        static_cast<void>(PutInObjectFolder(engine, editor, {shearParent, moved}));
        services.GetSceneGraph().Update(entityManager);
        editor.SelectEntity(moved);
        ASSERT_TRUE(editor.SetReparentTransformMode(EditorReparentTransformMode::KeepWorld));
        EXPECT_FALSE(editor.ReparentSelectedEntity(shearParent));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(moved).localPosition,
                  movedTransform.localPosition);
        EXPECT_FALSE(editor.IsSceneDirty());
        Scene::TransformComponent tinyParentTransform{};
        tinyParentTransform.localScale = Math::Vector3{0.0001F, 1.0F, 1.0F};
        const Scene::Entity tinyParent = entityManager.CreateEntity();
        AttachRoot(engine, tinyParent, tinyParentTransform);
        static_cast<void>(PutInObjectFolder(engine, editor, {tinyParent}));
        services.GetSceneGraph().Update(entityManager);
        EXPECT_FALSE(editor.ReparentSelectedEntity(tinyParent));
        EXPECT_FALSE(editor.IsSceneDirty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, SaveThenLoadScene_RoundTripsDocumentRootsWithoutSerializingEditorCamera) {
    const std::filesystem::path scenePath = "uve_editor_tests_round_trip.uvscene";
    std::filesystem::remove(scenePath);
    std::filesystem::remove(scenePath.string() + ".editor-recovery");

    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), scenePath);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();

        const Scene::Entity root = services.GetEntityManager().CreateEntity();
        Scene::TransformComponent rootTransform{};
        rootTransform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, root, Scene::TransformComponent{});

        const Scene::Entity child = services.GetEntityManager().CreateEntity();
        Scene::TransformComponent childTransform{};
        childTransform.localPosition = Math::Vector3{4.0F, 5.0F, 6.0F};
        AttachRoot(engine, child, childTransform);
        services.GetSceneGraph().SetParent(services.GetEntityManager(), child, root);

        editor.SelectEntity(root);
        ASSERT_TRUE(editor.SetSelectedLocalTransform(rootTransform));
        ASSERT_TRUE(editor.SaveScene());
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(std::filesystem::exists(scenePath));

        Scene::TransformComponent modified = rootTransform;
        modified.localPosition = Math::Vector3{9.0F, 9.0F, 9.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(modified));
        ASSERT_TRUE(editor.LoadScene());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());

        const std::vector<Scene::Entity> loadedRoots = editor.GetDocumentRoots();
        ASSERT_EQ(loadedRoots.size(), 1U);
        // The single root is the scene root; the load wrapped the pre-root save's top-level
        // entities beneath it, and the Outliner layout then moved the authored root (with its
        // saved transform and its own child) into the level's object folder.
        EXPECT_EQ(loadedRoots.front(), editor.GetDocumentSceneRoot());
        const std::vector<Scene::Entity> loadedSceneRootChildren = services.GetSceneGraph().GetChildren(
            services.GetEntityManager(), EditorAccess::GetObjectFolder(editor));
        ASSERT_EQ(loadedSceneRootChildren.size(), 1U);
        const Scene::TransformComponent& loadedTransform =
            services.GetEntityManager().GetComponent<Scene::TransformComponent>(
                loadedSceneRootChildren.front());
        EXPECT_EQ(loadedTransform.localPosition, rootTransform.localPosition);
        EXPECT_EQ(services.GetSceneGraph()
                      .GetChildren(services.GetEntityManager(), loadedSceneRootChildren.front())
                      .size(),
                  1U);
        EXPECT_TRUE(editor.IsSceneDirty()); // the load wrapped the file's top level

        editor.Shutdown();
    }

    engine.Shutdown();
    std::filesystem::remove(scenePath);
    std::filesystem::remove(scenePath.string() + ".editor-recovery");
}

TEST(EditorTest, TranslateSelectedAlongAxis_UpdatesLocalTransformAndConvertsParentScale) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gizmo.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity parent = entityManager.CreateEntity();
        Scene::TransformComponent parentTransform{};
        parentTransform.localScale = Math::Vector3{2.0F, 3.0F, 4.0F};
        AttachRoot(engine, parent, parentTransform);

        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent childTransform{};
        childTransform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, child, childTransform);
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        services.GetSceneGraph().Update(entityManager);

        editor.SelectEntity(child);
        EXPECT_TRUE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 2.0F));
        const Scene::TransformComponent& translated =
            entityManager.GetComponent<Scene::TransformComponent>(child);
        EXPECT_NEAR(translated.localPosition.x, 2.0F, 0.0001F);
        EXPECT_NEAR(translated.localPosition.y, 2.0F, 0.0001F);
        EXPECT_NEAR(translated.localPosition.z, 3.0F, 0.0001F);
        EXPECT_TRUE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Undo());
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(child).localPosition.x,
                    1.0F, 0.0001F);
        ASSERT_TRUE(editor.Redo());
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(child).localPosition.x,
                    2.0F, 0.0001F);
        EXPECT_FALSE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::None, 1.0F));
        EXPECT_FALSE(editor.TranslateSelectedAlongAxis(
            EditorTransformAxis::Y, std::numeric_limits<float>::infinity()));

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, RotateSelectedAroundWorldAxis_RotatesRootPreservesOtherLocalFieldsAndReplaysHistory) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_rotate_root.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent initial{};
        initial.localPosition = Math::Vector3{2.0F, 3.0F, 4.0F};
        initial.localScale = Math::Vector3{2.0F, 3.0F, 4.0F};
        AttachRoot(engine, entity, initial);

        editor.SelectEntity(entity);
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::Z,
                                                            std::numbers::pi_v<float> * 0.5F));
        const Scene::TransformComponent& rotated =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        const Math::Vector3 localXAxis =
            Math::RotateVector(rotated.localRotation, Math::Vector3{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(localXAxis.x, 0.0F, 0.0001F);
        EXPECT_NEAR(localXAxis.y, 1.0F, 0.0001F);
        EXPECT_NEAR(localXAxis.z, 0.0F, 0.0001F);
        EXPECT_EQ(rotated.localPosition, initial.localPosition);
        EXPECT_EQ(rotated.localScale, initial.localScale);
        EXPECT_TRUE(editor.IsSceneDirty());

        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
                  initial.localRotation);
        ASSERT_TRUE(editor.Redo());
        const Math::Vector3 replayedXAxis = Math::RotateVector(
            entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
            Math::Vector3{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(replayedXAxis.x, 0.0F, 0.0001F);
        EXPECT_NEAR(replayedXAxis.y, 1.0F, 0.0001F);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, RotateSelectedAroundWorldAxis_ConvertsParentWorldRotationToLocalRotation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_rotate_parented.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        Math::Quaternion parentRotation{};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.5F, parentRotation));
        const Scene::Entity parent = entityManager.CreateEntity();
        Scene::TransformComponent parentTransform{};
        parentTransform.localRotation = parentRotation;
        AttachRoot(engine, parent, parentTransform);

        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent childTransform{};
        childTransform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        childTransform.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
        AttachRoot(engine, child, childTransform);
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        services.GetSceneGraph().Update(entityManager);

        editor.SelectEntity(child);
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::X,
                                                            std::numbers::pi_v<float> * 0.5F));
        services.GetSceneGraph().Update(entityManager);

        Math::Quaternion worldDelta{};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F},
                                              std::numbers::pi_v<float> * 0.5F, worldDelta));
        const Math::Quaternion expectedWorld = Math::Multiply(worldDelta, parentRotation);
        const Scene::WorldTransformComponent& childWorld =
            entityManager.GetComponent<Scene::WorldTransformComponent>(child);
        const Math::Vector3 expectedProbe =
            Math::RotateVector(expectedWorld, Math::Vector3{0.0F, 1.0F, 0.0F});
        const Math::Vector3 actualProbe =
            Math::RotateVector(childWorld.worldRotation, Math::Vector3{0.0F, 1.0F, 0.0F});
        EXPECT_NEAR(actualProbe.x, expectedProbe.x, 0.0001F);
        EXPECT_NEAR(actualProbe.y, expectedProbe.y, 0.0001F);
        EXPECT_NEAR(actualProbe.z, expectedProbe.z, 0.0001F);
        const Scene::TransformComponent& rotated =
            entityManager.GetComponent<Scene::TransformComponent>(child);
        EXPECT_EQ(rotated.localPosition, childTransform.localPosition);
        EXPECT_EQ(rotated.localScale, childTransform.localScale);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, RotateSelectedAroundWorldAxis_RejectsInvalidOrUnsafeStateWithoutMutation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_rotate_safety.uvscene");
        editor.Init();
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::Z, 1.0F));

        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        AttachRoot(engine, entity, Scene::TransformComponent{});
        editor.SelectEntity(entity);
        const Scene::TransformComponent before =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::None, 1.0F));
        EXPECT_FALSE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::Y,
                                                             std::numeric_limits<float>::infinity()));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
                  before.localRotation);
        EXPECT_FALSE(editor.IsSceneDirty());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, ScaleSelectedAlongAxis_UpdatesOnlyPositiveLocalScaleAndReplaysHistory) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_scale.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity parent = entityManager.CreateEntity();
        AttachRoot(engine, parent, Scene::TransformComponent{});
        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent initial{};
        initial.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        initial.localScale = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, child, initial);
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        services.GetSceneGraph().Update(entityManager);

        editor.SelectEntity(child);
        ASSERT_TRUE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::Y, 1.5F));
        const Scene::TransformComponent& scaled =
            entityManager.GetComponent<Scene::TransformComponent>(child);
        EXPECT_EQ(scaled.localPosition, initial.localPosition);
        EXPECT_EQ(scaled.localRotation, initial.localRotation);
        EXPECT_NEAR(scaled.localScale.x, 1.0F, 0.0001F);
        EXPECT_NEAR(scaled.localScale.y, 3.5F, 0.0001F);
        EXPECT_NEAR(scaled.localScale.z, 3.0F, 0.0001F);
        EXPECT_TRUE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(child).localScale,
                  initial.localScale);
        ASSERT_TRUE(editor.Redo());
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(child).localScale.y,
                    3.5F, 0.0001F);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ScaleSelectedAlongAxis_RejectsUnsafeInputWithoutMutation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_scale_safety.uvscene");
        editor.Init();
        EXPECT_FALSE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::X, 1.0F));
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        AttachRoot(engine, entity, Scene::TransformComponent{});
        editor.SelectEntity(entity);
        const Scene::TransformComponent before = entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_FALSE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::None, 1.0F));
        EXPECT_FALSE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::X, -1.0F));
        EXPECT_FALSE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::Z,
                                                       std::numeric_limits<float>::infinity()));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(entity).localScale,
                  before.localScale);
        EXPECT_FALSE(editor.IsSceneDirty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ScaleSelectedUniformly_AppliesAdditiveOffsetAndSnapping) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_uniform_scale_offset.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent initial{};
        initial.localScale = Math::Vector3{2.0F, 1.0F, 1.0F};
        AttachRoot(engine, entity, initial);
        editor.SelectEntity(entity);
        ASSERT_TRUE(editor.ScaleSelectedUniformly(1.0F));
        const Scene::TransformComponent& additive =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_NEAR(additive.localScale.x, 3.0F, 0.0001F);
        EXPECT_NEAR(additive.localScale.y, 2.0F, 0.0001F);
        EXPECT_NEAR(additive.localScale.z, 2.0F, 0.0001F);
        EditorTransformSnappingSettings settings{};
        settings.enabled = true;
        settings.scaleStep = 0.25F;
        ASSERT_TRUE(editor.SetTransformSnappingSettings(settings));
        ASSERT_TRUE(editor.ScaleSelectedUniformly(0.37F));
        const Scene::TransformComponent& snapped =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_NEAR(snapped.localScale.x, 3.25F, 0.0001F);
        EXPECT_NEAR(snapped.localScale.y, 2.25F, 0.0001F);
        EXPECT_NEAR(snapped.localScale.z, 2.25F, 0.0001F);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ScaleSelectedUniformly_RejectsAsymmetricFloorWithoutPartialMutation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_uniform_scale_floor.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent initial{};
        initial.localScale = Math::Vector3{0.01F, 5.0F, 5.0F};
        AttachRoot(engine, entity, initial);
        editor.SelectEntity(entity);
        EXPECT_FALSE(editor.ScaleSelectedUniformly(-0.02F));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(entity).localScale,
                  initial.localScale);
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, SelectedBoundsQuery_BuildsIdentityWorldBoxWithoutMutatingEditorState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_selection_bounds_identity.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = Math::Vector3{4.0F, -5.0F, 6.0F};
        AttachRoot(engine, entity, transform);
        editor.SelectEntity(entity);
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        Scene::ColliderComponent collider{};
        collider.halfExtents = Math::Vector3{1.0F, 2.0F, 3.0F};
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        services.GetSceneGraph().Update(entityManager);
        editor.SelectEntity(entity);

        const std::optional<EditorSelectionBounds> bounds = editor.TryGetSelectedBounds();
        ASSERT_TRUE(bounds.has_value());
        EXPECT_EQ(bounds->worldCenter, transform.localPosition);
        EXPECT_EQ(bounds->worldCorners[0], (Math::Vector3{3.0F, -7.0F, 3.0F}));
        EXPECT_EQ(bounds->worldCorners[6], (Math::Vector3{5.0F, -3.0F, 9.0F}));
        const Scene::TransformComponent& afterQuery =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        EXPECT_EQ(afterQuery.localPosition, transform.localPosition);
        EXPECT_EQ(afterQuery.localRotation, transform.localRotation);
        EXPECT_EQ(afterQuery.localScale, transform.localScale);
        EXPECT_EQ(editor.GetSelectedEntity(), entity);
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.CanRedo());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, SelectedBoundsQuery_UsesDerivedParentTransformAndRejectsUnsafeState) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_selection_bounds_parented.uvscene");
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        Math::Quaternion parentRotation{};
        ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 0.0F, 1.0F},
                                              std::numbers::pi_v<float> * 0.5F, parentRotation));
        const Scene::Entity parent = entityManager.CreateEntity();
        Scene::TransformComponent parentTransform{};
        parentTransform.localPosition = Math::Vector3{10.0F, 20.0F, 30.0F};
        parentTransform.localRotation = parentRotation;
        parentTransform.localScale = Math::Vector3{2.0F, 3.0F, 4.0F};
        AttachRoot(engine, parent, parentTransform);

        const Scene::Entity child = entityManager.CreateEntity();
        Scene::TransformComponent childTransform{};
        childTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
        AttachRoot(engine, child, childTransform);
        Scene::ColliderComponent collider{};
        collider.halfExtents = Math::Vector3{0.5F, 1.0F, 1.5F};
        entityManager.AddComponent<Scene::ColliderComponent>(child, collider);
        services.GetSceneGraph().SetParent(entityManager, child, parent);
        services.GetSceneGraph().Update(entityManager);
        editor.SelectEntity(child);

        const std::optional<EditorSelectionBounds> bounds = editor.TryGetSelectedBounds();
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

        entityManager.GetComponent<Scene::ColliderComponent>(child).halfExtents.x = 0.0F;
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        entityManager.GetComponent<Scene::ColliderComponent>(child).halfExtents = collider.halfExtents;
        const auto& liveWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(child);
        liveWorld.dirty = true;
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        liveWorld.dirty = false;
        auto worldTransform = liveWorld;
        // Malformed initial snapshots exercise the same consumer rejection paths; live cache
        // pose mutation is no longer available through ordinary entity-manager access.
        const auto replaceCache = [&] {
            entityManager.RemoveComponent<Scene::WorldTransformComponent>(child);
            entityManager.AddComponent<Scene::WorldTransformComponent>(child, worldTransform);
        };
        const Math::Vector3 savedScale = worldTransform.worldScale;
        worldTransform.worldScale.x = std::numeric_limits<float>::infinity();
        replaceCache();
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        worldTransform.worldScale = savedScale;
        const Math::Quaternion savedRotation = worldTransform.worldRotation;
        worldTransform.worldRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
        replaceCache();
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
        worldTransform.worldRotation = savedRotation;
        replaceCache();
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());

        const Scene::Entity nonDocumentEntity = entityManager.CreateEntity();
        editor.SelectEntity(nonDocumentEntity);
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());

        editor.Shutdown();
        EXPECT_FALSE(editor.TryGetSelectedBounds().has_value());
    }

    engine.Shutdown();
}

TEST(EditorTest, TransformSnappingSettings_ExposeSafeDefaultsAndRejectInvalidValues) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_snapping_settings.uvscene");
        editor.Init();

        const EditorTransformSnappingSettings defaults = editor.GetTransformSnappingSettings();
        EXPECT_FALSE(defaults.enabled);
        EXPECT_FLOAT_EQ(defaults.translateStep, 1.0F);
        EXPECT_FLOAT_EQ(defaults.rotateStepDegrees, 15.0F);
        EXPECT_FLOAT_EQ(defaults.scaleStep, 0.1F);

        EditorTransformSnappingSettings configured{true, 0.5F, 45.0F, 0.25F};
        ASSERT_TRUE(editor.SetTransformSnappingSettings(configured));
        EXPECT_EQ(editor.GetTransformSnappingSettings().enabled, configured.enabled);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().translateStep, configured.translateStep);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().rotateStepDegrees, configured.rotateStepDegrees);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().scaleStep, configured.scaleStep);

        EditorTransformSnappingSettings invalid = configured;
        invalid.translateStep = 0.0F;
        EXPECT_FALSE(editor.SetTransformSnappingSettings(invalid));
        invalid = configured;
        invalid.rotateStepDegrees = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(editor.SetTransformSnappingSettings(invalid));
        invalid = configured;
        invalid.scaleStep = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.SetTransformSnappingSettings(invalid));
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().translateStep, configured.translateStep);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().rotateStepDegrees, configured.rotateStepDegrees);
        EXPECT_FLOAT_EQ(editor.GetTransformSnappingSettings().scaleStep, configured.scaleStep);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, TransformSnapping_QuantizesCommandsWithoutHistoryDriftAndReplaysRotation) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_snapping_commands.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent initial{};
        initial.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, entity, initial);
        editor.SelectEntity(entity);

        ASSERT_TRUE(editor.SetTransformSnappingSettings(
            EditorTransformSnappingSettings{true, 0.5F, 15.0F, 0.25F}));
        EXPECT_FALSE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 0.1F));
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());

        ASSERT_TRUE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 0.74F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    1.5F, 0.0001F);
        ASSERT_TRUE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, -0.74F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    initial.localPosition.x, 0.0001F);

        ASSERT_TRUE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::X, 0.37F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localScale.x,
                    1.25F, 0.0001F);
        EXPECT_FALSE(editor.ScaleSelectedAlongAxis(EditorTransformAxis::X, -1.24F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localScale.x,
                    1.25F, 0.0001F);

        const float twentyDegreesRadians = (20.0F * std::numbers::pi_v<float>) / 180.0F;
        ASSERT_TRUE(editor.RotateSelectedAroundWorldAxis(EditorTransformAxis::Z, twentyDegreesRadians));
        const Math::Vector3 rotatedXAxis = Math::RotateVector(
            entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
            Math::Vector3{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(rotatedXAxis.x, std::cos(std::numbers::pi_v<float> / 12.0F), 0.0001F);
        EXPECT_NEAR(rotatedXAxis.y, std::sin(std::numbers::pi_v<float> / 12.0F), 0.0001F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
                  initial.localRotation);
        ASSERT_TRUE(editor.Redo());
        const Math::Vector3 replayedXAxis = Math::RotateVector(
            entityManager.GetComponent<Scene::TransformComponent>(entity).localRotation,
            Math::Vector3{1.0F, 0.0F, 0.0F});
        EXPECT_NEAR(replayedXAxis.x, rotatedXAxis.x, 0.0001F);
        EXPECT_NEAR(replayedXAxis.y, rotatedXAxis.y, 0.0001F);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, LoadMissingScene_FailsWithoutDestroyingCurrentDocument) {
    const std::filesystem::path missingScenePath = "uve_editor_tests_missing.uvscene";
    std::filesystem::remove(missingScenePath);

    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), missingScenePath);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        const Scene::Entity root = services.GetEntityManager().CreateEntity();
        AttachRoot(engine, root, Scene::TransformComponent{});

        EXPECT_FALSE(editor.LoadScene());
        const std::vector<Scene::Entity> roots = editor.GetDocumentRoots();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the surviving authored root
        EXPECT_NE(std::find(roots.begin(), roots.end(), root), roots.end());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_RestoresSnapshotRejectsAuthoringAndPreservesSelectionIntent) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_restore.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = entityManager.CreateEntity();
        Scene::TransformComponent authored{};
        authored.localPosition = Math::Vector3{2.0F, 3.0F, -4.0F};
        AttachRoot(engine, root, authored);
        editor.SelectEntity(root);

        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_EQ(editor.GetPlayModeState(), EditorPlayModeState::Playing);
        EXPECT_TRUE(engine.IsTransientSimulationSessionActive());
        EXPECT_FALSE(editor.SetSelectedLocalTransform(Scene::TransformComponent{}));
        EXPECT_EQ(editor.CreateDocumentEntity(EditorEntityKind::Empty), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.Undo());

        ASSERT_TRUE(editor.PausePlayMode());
        EXPECT_EQ(engine.GetSimulationExecutionMode(), Core::SimulationExecutionMode::Paused);
        ASSERT_TRUE(editor.StepPlayMode());
        EXPECT_FALSE(editor.StepPlayMode());
        engine.TickFrame();
        ASSERT_TRUE(editor.ResumePlayMode());
        EXPECT_EQ(engine.GetSimulationExecutionMode(), Core::SimulationExecutionMode::Running);

        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_EQ(editor.GetPlayModeState(), EditorPlayModeState::Edit);
        EXPECT_FALSE(engine.IsTransientSimulationSessionActive());
        const std::vector<Scene::Entity> restoredRoots = editor.GetDocumentRoots();
        ASSERT_EQ(restoredRoots.size(), 2U); // the scene root + the restored authored root
        const Scene::Entity restoredAuthored =
            restoredRoots.front() == editor.GetDocumentSceneRoot() ? restoredRoots.back()
                                                                       : restoredRoots.front();
        EXPECT_NE(restoredAuthored, root);
        EXPECT_EQ(editor.GetSelectedEntity(), restoredAuthored);
        const Scene::TransformComponent& restored =
            entityManager.GetComponent<Scene::TransformComponent>(restoredAuthored);
        EXPECT_EQ(restored.localPosition, authored.localPosition);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, GetDocumentRoots_ExcludesEditorInternalEntitiesAndTheySurvivePlayStop) {
    // Regression test for a real, reproducible crash: entities tagged EditorInternalEntityComponent
    // (e.g. the editor Viewport's own hidden free-look-camera proxy) are scene roots just like real
    // document content (AttachTransform always creates a root), but must never be swept up by
    // Play-mode's destroy/recreate snapshot cycle in StopPlayMode() - before this fix, they were,
    // which left a cached Entity elsewhere pointing at a destroyed entity and crashed the next
    // time it was dereferenced. See GetDocumentRoots()'s own comment for the fix.
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_internal_entity.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity documentRoot = entityManager.CreateEntity();
        AttachRoot(engine, documentRoot, Scene::TransformComponent{});

        const Scene::Entity internalEntity = entityManager.CreateEntity();
        AttachRoot(engine, internalEntity, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::EditorInternalEntityComponent>(internalEntity);

        const std::vector<Scene::Entity> roots = editor.GetDocumentRoots();
        ASSERT_EQ(roots.size(), 2U); // the scene root + the authored document root
        EXPECT_NE(std::find(roots.begin(), roots.end(), documentRoot), roots.end());
        EXPECT_EQ(std::find(roots.begin(), roots.end(), internalEntity), roots.end());

        editor.SelectEntity(documentRoot);
        ASSERT_TRUE(editor.EnterPlayMode());
        ASSERT_TRUE(editor.StopPlayMode());

        // The actual bug: confirm the internal entity is untouched (still alive, same handle) by
        // the destroy/recreate cycle that just ran on every *document* root.
        EXPECT_TRUE(entityManager.IsAlive(internalEntity));
        EXPECT_TRUE(entityManager.HasComponent<Scene::EditorInternalEntityComponent>(internalEntity));

        const std::vector<Scene::Entity> rootsAfterStop = editor.GetDocumentRoots();
        ASSERT_EQ(rootsAfterStop.size(), 2U); // the scene root + the restored authored root
        EXPECT_NE(rootsAfterStop.back(), documentRoot); // restored as a fresh handle, like every real root

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_SpawnPointTeleportsThePlayerAndStopGivesEverythingBack) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_spawn.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();

        // The player: a root-level entity with the controller component, authored at origin.
        const Scene::Entity player = entityManager.CreateEntity();
        AttachRoot(engine, player, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::CharacterControllerComponent>(
            player, Scene::CharacterControllerComponent{});

        // The spawn point: another root-level entity at (3, 1, -2), no offset, one-shot so both
        // sandbox mutations (teleport, spend) can be measured in one session.
        const Scene::Entity spawn = entityManager.CreateEntity();
        Scene::TransformComponent spawnTransform{};
        spawnTransform.localPosition = Math::Vector3{3.0F, 1.0F, -2.0F};
        AttachRoot(engine, spawn, spawnTransform);
        Scene::SpawnPoint3DComponent spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponent<Scene::SpawnPoint3DComponent>(spawn, spawnPoint);

        // Compose reads the spawn object's WORLD pose, so the sweep must have run since attaching.
        services.GetSceneGraph().Update(entityManager);

        ASSERT_TRUE(editor.EnterPlayMode());
        const Scene::TransformComponent& playedTransform =
            entityManager.GetComponent<Scene::TransformComponent>(player);
        EXPECT_NEAR(playedTransform.localPosition.x, 3.0F, 1.0e-5F);
        EXPECT_NEAR(playedTransform.localPosition.y, 1.0F, 1.0e-5F);
        EXPECT_NEAR(playedTransform.localPosition.z, -2.0F, 1.0e-5F);
        // The documented simulation-write rule: the quaternion is now the truth, so the authored
        // Euler cache stops replaying over this teleport.
        EXPECT_EQ(playedTransform.rotationEditMode, Scene::RotationEditMode::Quaternion);
        // One-shot is spent inside the sandbox.
        EXPECT_FALSE(entityManager.GetComponent<Scene::SpawnPoint3DComponent>(spawn).enabled);

        services.GetSceneGraph().Update(entityManager);
        EXPECT_NEAR(entityManager.GetComponent<Scene::WorldTransformComponent>(player)
                        .worldPosition.x,
                    3.0F, 1.0e-5F);

        // Stop hands BOTH back - the snapshot remakes document entities, so the two roles are
        // found again by component, never by the old handles (see the restore test above).
        ASSERT_TRUE(editor.StopPlayMode());
        Scene::Entity restoredPlayer = Scene::kInvalidEntity;
        entityManager.ForEach<Scene::CharacterControllerComponent>(
            [&restoredPlayer](const Scene::Entity entity, Scene::CharacterControllerComponent&) {
                restoredPlayer = entity;
            });
        ASSERT_NE(restoredPlayer, Scene::kInvalidEntity);
        const Scene::TransformComponent& restoredTransform =
            entityManager.GetComponent<Scene::TransformComponent>(restoredPlayer);
        EXPECT_EQ(restoredTransform.localPosition.x, 0.0F);
        EXPECT_EQ(restoredTransform.localPosition.z, 0.0F);
        EXPECT_EQ(restoredTransform.rotationEditMode, Scene::RotationEditMode::Euler);
        Scene::Entity restoredSpawn = Scene::kInvalidEntity;
        entityManager.ForEach<Scene::SpawnPoint3DComponent>(
            [&restoredSpawn](const Scene::Entity entity, Scene::SpawnPoint3DComponent&) {
                restoredSpawn = entity;
            });
        ASSERT_NE(restoredSpawn, Scene::kInvalidEntity);
        EXPECT_TRUE(
            entityManager.GetComponent<Scene::SpawnPoint3DComponent>(restoredSpawn).enabled);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_SpawnPointWithOffsetAndParentPlacesRespectingBothKinds) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_spawn_offset.uvscene", 100U,
                         &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();
        Scene::ISceneGraph& sceneGraph = services.GetSceneGraph();

        // Spawn point object at (4, 0, 0) whose authored offset raises the player by (0, 0.5, 0);
        // one-shot false, so the point stays live through the session.
        const Scene::Entity spawn = entityManager.CreateEntity();
        Scene::TransformComponent spawnTransform{};
        spawnTransform.localPosition = Math::Vector3{4.0F, 0.0F, 0.0F};
        AttachRoot(engine, spawn, spawnTransform);
        Scene::SpawnPoint3DComponent spawnPoint{};
        spawnPoint.localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};
        entityManager.AddComponent<Scene::SpawnPoint3DComponent>(spawn, spawnPoint);

        // The player this time is a child of a scaled, translated parent: the world pose must
        // arrive through the sweep's exact inverse, not by pretending the parent is identity.
        const Scene::Entity group = entityManager.CreateEntity();
        Scene::TransformComponent groupTransform{};
        groupTransform.localPosition = Math::Vector3{10.0F, 0.0F, 0.0F};
        groupTransform.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
        AttachRoot(engine, group, groupTransform);
        const Scene::Entity player = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, player, Scene::TransformComponent{});
        sceneGraph.SetParent(entityManager, player, group);
        entityManager.AddComponent<Scene::CharacterControllerComponent>(
            player, Scene::CharacterControllerComponent{});

        sceneGraph.Update(entityManager);
        ASSERT_TRUE(editor.EnterPlayMode());

        // Expected world pose = (4, 0.5, 0); expected local = ((4-10)/2, (0.5-0)/2, 0).
        const Scene::TransformComponent& local =
            entityManager.GetComponent<Scene::TransformComponent>(player);
        EXPECT_NEAR(local.localPosition.x, -3.0F, 1.0e-5F);
        EXPECT_NEAR(local.localPosition.y, 0.25F, 1.0e-5F);
        EXPECT_NEAR(local.localPosition.z, 0.0F, 1.0e-5F);
        sceneGraph.Update(entityManager);
        const Scene::WorldTransformComponent& world =
            entityManager.GetComponent<Scene::WorldTransformComponent>(player);
        EXPECT_NEAR(world.worldPosition.x, 4.0F, 1.0e-4F);
        EXPECT_NEAR(world.worldPosition.y, 0.5F, 1.0e-4F);
        // A reusable point is not spent.
        EXPECT_TRUE(entityManager.GetComponent<Scene::SpawnPoint3DComponent>(spawn).enabled);

        ASSERT_TRUE(editor.StopPlayMode());
        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_SpawnPointWithNoPlayerJustPlays) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_spawn_noop_a.uvscene", 100U,
                         &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();

        // A spawn point with no player to absorb it: play still enters, and the point keeps its
        // one-shot loaded - resolution is defined as "nothing to do", never a failure.
        const Scene::Entity spawn = entityManager.CreateEntity();
        AttachRoot(engine, spawn, Scene::TransformComponent{});
        Scene::SpawnPoint3DComponent spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponent<Scene::SpawnPoint3DComponent>(spawn, spawnPoint);
        services.GetSceneGraph().Update(entityManager);
        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_TRUE(entityManager.GetComponent<Scene::SpawnPoint3DComponent>(spawn).enabled);
        ASSERT_TRUE(editor.StopPlayMode());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_PlayerWithNoSpawnPointKeepsItsAuthoredPose) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_spawn_noop_b.uvscene", 100U,
                         &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();

        // The inverse of the previous test, in a document of its own: a player with nothing to
        // spawn at keeps its authored pose through the whole sandbox cycle.
        const Scene::Entity player = entityManager.CreateEntity();
        Scene::TransformComponent authored{};
        authored.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
        AttachRoot(engine, player, authored);
        entityManager.AddComponent<Scene::CharacterControllerComponent>(
            player, Scene::CharacterControllerComponent{});
        services.GetSceneGraph().Update(entityManager);
        ASSERT_TRUE(editor.EnterPlayMode());
        const Scene::TransformComponent& during =
            entityManager.GetComponent<Scene::TransformComponent>(player);
        EXPECT_EQ(during.localPosition.x, 1.0F);
        EXPECT_EQ(during.localPosition.y, 2.0F);
        ASSERT_TRUE(editor.StopPlayMode());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_InstantiatesDefaultPlayerAndUsesItsCamera) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("play_default_player");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();

        const auto created = editor.CreateContentCatalogueItem("player", content);
        ASSERT_TRUE(created.has_value());
        // A Player3D arrives on its own - +Add puts nothing inside what it makes - so the view is
        // the author's to add. The test builds the same body an author would: a camera under the
        // player, authored in the Entity Editor and saved into the asset.
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        const Scene::Entity playerBody = editor.GetEntityEditorRoot();
        ASSERT_NE(playerBody, Scene::kInvalidEntity);
        const Scene::Entity view =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D);
        ASSERT_NE(view, Scene::kInvalidEntity);
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(view).parent, playerBody);
        ASSERT_TRUE(editor.CloseEntityEditor(true));
        EXPECT_FALSE(editor.IsEntityEditorOpen());
        ASSERT_TRUE(editor.SetDefaultPlayerEntity(created->filename()));

        const Scene::Entity spawn = entityManager.CreateEntity();
        Scene::TransformComponent spawnTransform{};
        spawnTransform.localPosition = Math::Vector3{5.0F, 1.0F, -2.0F};
        AttachRoot(engine, spawn, spawnTransform);
        entityManager.AddComponent<Scene::SpawnPoint3DComponent>(spawn, Scene::SpawnPoint3DComponent{});
        services.GetSceneGraph().Update(entityManager);

        EXPECT_EQ(Scene::ResolvePossessedPlayer(entityManager), Scene::kInvalidEntity);
        ASSERT_TRUE(editor.EnterPlayMode());

        const Scene::Entity player = Scene::ResolvePossessedPlayer(entityManager);
        ASSERT_NE(player, Scene::kInvalidEntity);
        services.GetSceneGraph().Update(entityManager);
        const Scene::WorldTransformComponent& played =
            entityManager.GetComponent<Scene::WorldTransformComponent>(player);
        EXPECT_NEAR(played.worldPosition.x, 5.0F, 1.0e-4F);
        EXPECT_NEAR(played.worldPosition.y, 1.0F, 1.0e-4F);
        EXPECT_NEAR(played.worldPosition.z, -2.0F, 1.0e-4F);

        const Scene::Entity camera = Scene::FindPlayerCamera(entityManager, player);
        ASSERT_NE(camera, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.GetComponent<Scene::CameraComponent>(camera).current);
        const std::optional<Scene::Entity> current = Scene::FindCurrentCameraEntity(entityManager);
        ASSERT_TRUE(current.has_value());
        EXPECT_EQ(*current, camera);

        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_EQ(Scene::ResolvePossessedPlayer(entityManager), Scene::kInvalidEntity);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, ViewportBookmarks_StoreRestoreClearAndRejectBadInput) {
    // The session bookmark store itself: the standard Ctrl+digit/digit slots as editor-owned
    // transient state - isolated per slot, validated on the way in, honest about what is not
    // inside it (no document coupling, no scene dirty flag touched).
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_bookmarks.uvscene", 100U, &engine);
        editor.Init();

        // An untouched slot answers no value, and every slot index out of range fails closed.
        EXPECT_FALSE(editor.GetViewportBookmark(0U).has_value());
        EXPECT_FALSE(editor.GetViewportBookmark(Editor::kEditorViewportBookmarkSlotCount).has_value());
        EXPECT_FALSE(editor.ClearViewportBookmark(Editor::kEditorViewportBookmarkSlotCount));

        const Editor::EditorViewportBookmark first{
            Math::Vector3{1.0F, 2.0F, 3.0F}, 0.4F, -0.2F, 7.5F};
        ASSERT_TRUE(editor.SetViewportBookmark(0U, first));
        const Editor::EditorViewportBookmark other{
            Math::Vector3{-8.0F, 0.0F, 2.0F}, 2.1F, 0.9F, 12.0F};
        ASSERT_TRUE(editor.SetViewportBookmark(Editor::kEditorViewportBookmarkSlotCount - 1U, other));

        // Round trip is exact - the store must not smear floats while they are only passing through.
        const std::optional<Editor::EditorViewportBookmark> restored =
            editor.GetViewportBookmark(0U);
        ASSERT_TRUE(restored.has_value());
        EXPECT_EQ(restored->target.x, 1.0F);
        EXPECT_EQ(restored->yawRadians, 0.4F);
        EXPECT_EQ(restored->pitchRadians, -0.2F);
        EXPECT_EQ(restored->distance, 7.5F);
        // Slots stay independent; clearing an occupied slot reports it, an empty one does not.
        ASSERT_TRUE(editor.GetViewportBookmark(9U).has_value());
        EXPECT_TRUE(editor.ClearViewportBookmark(0U));
        EXPECT_FALSE(editor.GetViewportBookmark(0U).has_value());
        EXPECT_TRUE(editor.GetViewportBookmark(9U).has_value());
        EXPECT_FALSE(editor.ClearViewportBookmark(1U));

        // Storing over an occupied slot replaces it.
        ASSERT_TRUE(editor.SetViewportBookmark(9U, first));
        EXPECT_EQ(editor.GetViewportBookmark(9U)->target.x, 1.0F);

        // Garbage in, nothing stored: out-of-range slot, NaN, and a non-positive distance.
        EXPECT_FALSE(editor.SetViewportBookmark(10U, first));
        EXPECT_FALSE(editor.SetViewportBookmark(
            1U, Editor::EditorViewportBookmark{
                    Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                    0.0F, 0.0F, 5.0F}));
        EXPECT_FALSE(editor.SetViewportBookmark(
            1U, Editor::EditorViewportBookmark{{}, 0.0F, 0.0F, 0.0F}));
        EXPECT_FALSE(editor.GetViewportBookmark(1U).has_value());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, Marker3DFocusBookmark_FliesTheCameraIntoTheMarkerViewpoint) {
    // ComposeMarker3DFocusBookmark is the live consumer that separates UVE's Marker3D from
    // Godot's inert annotation: the marker's authored offset+rotation compose under the object's
    // world pose, the eye lands exactly ON the marker looking along its composed -Z, and the
    // orbit inverse then hands back a target/yaw/pitch the viewport camera can hold verbatim -
    // measured here by running the camera's own forward formula back to the eye (round trip).
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_marker_focus.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Core::EngineServices& services = engine.GetServices();

        // Marker object at (3,1,-2), yawed 90 degrees about Y (-Z faces -X), local offset (0,2,4).
        const Scene::Entity markerObject = entityManager.CreateEntity();
        Scene::TransformComponent markerTransform{};
        markerTransform.localPosition = Math::Vector3{3.0F, 1.0F, -2.0F};
        markerTransform.localRotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};
        AttachRoot(engine, markerObject, markerTransform);
        Scene::Marker3DComponent marker{};
        marker.markerName = "Boss view";
        marker.localPosition = Math::Vector3{0.0F, 2.0F, 4.0F};
        entityManager.AddComponent<Scene::Marker3DComponent>(markerObject, marker);
        services.GetSceneGraph().Update(entityManager);

        const std::optional<Editor::EditorViewportBookmark> bookmark =
            editor.ComposeMarker3DFocusBookmark(markerObject);
        ASSERT_TRUE(bookmark.has_value());
        // Composed eye: rotation maps (0,2,4) to (4,2,0) about Y with sin90, added to the object.
        const float expectedEyeX = 3.0F + 4.0F;
        const float expectedEyeY = 1.0F + 2.0F;
        const float expectedEyeZ = -2.0F;
        // Composed forward: -Z rotated 90 degrees about Y points along -X.
        // The camera convention: eye = target + offset(yaw,pitch) * distance with
        // offset=(cosy*cosp, sinp, siny*cosp) - run it forward and require the eye back.
        const float cosPitch = std::cos(bookmark->pitchRadians);
        const Math::Vector3 offset{
            std::cos(bookmark->yawRadians) * cosPitch,
            std::sin(bookmark->pitchRadians),
            std::sin(bookmark->yawRadians) * cosPitch,
        };
        const Math::Vector3 roundTripEye =
            bookmark->target + offset * bookmark->distance;
        EXPECT_NEAR(roundTripEye.x, expectedEyeX, 1.0e-4F);
        EXPECT_NEAR(roundTripEye.y, expectedEyeY, 1.0e-4F);
        EXPECT_NEAR(roundTripEye.z, expectedEyeZ, 1.0e-4F);
        EXPECT_NEAR(bookmark->distance, Editor::kEditorMarkerFocusDistance, 1.0e-6F);
        // And the view direction itself is -X: target - eye points along composed -Z.
        const Math::Vector3 viewDirection =
            (bookmark->target - roundTripEye) * (1.0F / Editor::kEditorMarkerFocusDistance);
        EXPECT_NEAR(viewDirection.x, -1.0F, 1.0e-4F);
        EXPECT_NEAR(viewDirection.y, 0.0F, 1.0e-4F);
        EXPECT_NEAR(viewDirection.z, 0.0F, 1.0e-4F);

        // A disabled or invalid marker, or a plain entity, composes nothing - fail-closed.
        entityManager.GetComponent<Scene::Marker3DComponent>(markerObject).enabled = false;
        EXPECT_FALSE(editor.ComposeMarker3DFocusBookmark(markerObject).has_value());
        const Scene::Entity plain = entityManager.CreateEntity();
        AttachRoot(engine, plain, Scene::TransformComponent{});
        EXPECT_FALSE(editor.ComposeMarker3DFocusBookmark(plain).has_value());

        // Plain-entity focus still answers the authored world position when a transform exists.
        Scene::TransformComponent plainTransform{};
        plainTransform.localPosition = Math::Vector3{-4.0F, 0.5F, 8.0F};
        services.GetSceneGraph().SetLocalTransform(entityManager, plain, plainTransform);
        services.GetSceneGraph().Update(entityManager);
        const std::optional<Math::Vector3> focusTarget =
            editor.ResolveEntityFocusTarget(plain);
        ASSERT_TRUE(focusTarget.has_value());
        EXPECT_NEAR(focusTarget->x, -4.0F, 1.0e-5F);
        EXPECT_NEAR(focusTarget->y, 0.5F, 1.0e-5F);
        EXPECT_NEAR(focusTarget->z, 8.0F, 1.0e-5F);

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, OrbitBookmarkInverse_RoundTripsTheCameraEyeAndGuardsThePoles) {
    // The pure inverse: craft any in-range yaw/pitch, build the camera's own offset formula
    // forward from it, give that eye+forward to ResolveOrbitBookmarkFromLook, and require the
    // recovered pose reproduces the same eye through the same forward formula - the exact
    // measured round trip (identical claim style to SpringArm3D's sweep inverse, below 1e-4).
    const float distance = 6.0F;
    const float cases[][2] = {{0.0F, 0.0F}, {0.7553F, -0.4561F}, {-2.2F, 1.2F}, {3.0F, -1.55F}};
    for (const auto& yawPitch : cases) {
        const float cosPitch = std::cos(yawPitch[1]);
        const Math::Vector3 offset{
            std::cos(yawPitch[0]) * cosPitch, std::sin(yawPitch[1]),
            std::sin(yawPitch[0]) * cosPitch};
        const Math::Vector3 target{2.0F, -1.0F, 5.0F};
        const Math::Vector3 eye = target + offset * distance;
        const Math::Vector3 forward = offset * (-1.0F);
        const std::optional<Editor::EditorViewportBookmark> recovered =
            Editor::Editor::ResolveOrbitBookmarkFromLook(eye, forward, distance);
        ASSERT_TRUE(recovered.has_value());
        const float recoveredCosPitch = std::cos(recovered->pitchRadians);
        const Math::Vector3 recoveredOffset{
            std::cos(recovered->yawRadians) * recoveredCosPitch,
            std::sin(recovered->pitchRadians),
            std::sin(recovered->yawRadians) * recoveredCosPitch};
        const Math::Vector3 recoveredEye =
            recovered->target + recoveredOffset * recovered->distance;
        EXPECT_NEAR(recoveredEye.x, eye.x, 1.0e-4F) << "yaw in case: " << yawPitch[0];
        EXPECT_NEAR(recoveredEye.y, eye.y, 1.0e-4F) << "yaw in case: " << yawPitch[0];
        EXPECT_NEAR(recoveredEye.z, eye.z, 1.0e-4F) << "yaw in case: " << yawPitch[0];
    }

    // The poles: a straight-down look can only snap to the clamped pitch with yaw 0 (the
    // convention), and garbage never yields a pose at all.
    const std::optional<Editor::EditorViewportBookmark> straightDown =
        Editor::Editor::ResolveOrbitBookmarkFromLook(
            {}, Math::Vector3{0.0F, -1.0F, 0.0F}, distance);
    ASSERT_TRUE(straightDown.has_value());
    // The camera formula has offset.y = sin(pitch): a down-looking forward (-Y) means the eye
    // sits ABOVE the target, so the inverse of a -Y forward is pitch = +pi/2, not -pi/2. The
    // camera's own ~89-degree clamp is applied only when the pose is handed to it
    // (OrbitCamera::SetYawPitch), and yaw is clamped to 0 at the pole where it is unobservable.
    EXPECT_NEAR(straightDown->pitchRadians, std::numbers::pi_v<float> * 0.5F, 1.0e-6F);
    EXPECT_EQ(straightDown->yawRadians, 0.0F);
    // The opposite pole: a straight-up look inverts to pitch = -pi/2 (eye below the target).
    const std::optional<Editor::EditorViewportBookmark> straightUp =
        Editor::Editor::ResolveOrbitBookmarkFromLook(
            {}, Math::Vector3{0.0F, 1.0F, 0.0F}, distance);
    ASSERT_TRUE(straightUp.has_value());
    EXPECT_NEAR(straightUp->pitchRadians, -std::numbers::pi_v<float> * 0.5F, 1.0e-6F);
    EXPECT_EQ(straightUp->yawRadians, 0.0F);

    EXPECT_FALSE(Editor::Editor::ResolveOrbitBookmarkFromLook(
                     {}, Math::Vector3{0.0F, 0.0F, 0.0F}, distance)
                     .has_value());
    EXPECT_FALSE(Editor::Editor::ResolveOrbitBookmarkFromLook(
                     {}, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                     distance)
                     .has_value());
    EXPECT_FALSE(Editor::Editor::ResolveOrbitBookmarkFromLook(
                     {}, Math::Vector3{1.0F, 0.0F, 0.0F}, -1.0F)
                     .has_value());
}

TEST(EditorTest, PlayModeSandbox_RestoresOrderedMultiSelectionAndActiveEntity) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_editor_tests_play_multi_selection.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity first = entityManager.CreateEntity();
        const Scene::Entity second = entityManager.CreateEntity();
        AttachRoot(engine, first, Scene::TransformComponent{});
        AttachRoot(engine, second, Scene::TransformComponent{});
        editor.SelectEntity(first);
        editor.ToggleEntitySelection(second);

        ASSERT_TRUE(editor.EnterPlayMode());
        ASSERT_TRUE(editor.StopPlayMode());

        const std::vector<Scene::Entity> restoredRoots = editor.GetDocumentRoots();
        ASSERT_EQ(restoredRoots.size(), 3U); // the scene root + the two restored authored roots
        EXPECT_EQ(restoredRoots[0], editor.GetDocumentSceneRoot());
        EXPECT_NE(restoredRoots[1], first);
        EXPECT_NE(restoredRoots[2], second);
        // The restored selection is the authored pair (the two non-scene-root restored roots,
        // in their restored order); the scene root itself is never part of it.
        EXPECT_EQ(editor.GetSelectedEntities(), (std::vector<Scene::Entity>{restoredRoots[1], restoredRoots[2]}));
        EXPECT_EQ(editor.GetSelectedEntity(), restoredRoots[2]);
        EXPECT_FALSE(editor.HasSingleDocumentSelection());

        editor.Shutdown();
    }

    engine.Shutdown();
}

TEST(EditorTest, PlayModeSandbox_HandlesEmptyDocumentAndMissingControlSafely) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor withoutControl(engine.GetServices(), "uve_editor_tests_play_no_control.uvscene");
        withoutControl.Init();
        EXPECT_FALSE(withoutControl.EnterPlayMode());
        withoutControl.Shutdown();

        Editor editor(engine.GetServices(), "uve_editor_tests_play_empty.uvscene", 100U, &engine);
        editor.Init();
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        ASSERT_TRUE(editor.EnterPlayMode());
        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_EQ(editor.GetPlayModeState(), EditorPlayModeState::Edit);
        // One-root documents: an otherwise-empty document holds exactly the scene root.
        ASSERT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], editor.GetDocumentSceneRoot());
        editor.Shutdown();
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
[[nodiscard]] Scene::Entity SelectFreshRoot(Core::EngineCore& engine, Editor& editor,
                                                  const Math::Vector3 position) {
    Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform{};
    transform.localPosition = position;
    AttachRoot(engine, entity, transform);
    engine.GetServices().GetSceneGraph().Update(entityManager);
    editor.SelectEntity(entity);
    return entity;
}

TEST(EditorTest, TransformGesture_ManyPreviewsCollapseIntoExactlyOneUndoStep) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_commit.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = SelectFreshRoot(engine, editor, Math::Vector3{});

        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        EXPECT_EQ(editor.GetToolSessionPhase(), EditorToolSessionPhase::Previewing);

        // A drag reports its TOTAL offset each frame. Feeding 1, 2, ... 10 must land on 10, not on
        // their sum - that difference is the whole reason previews run off the baseline.
        for (int step = 1; step <= 10; ++step) {
            ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::X,
                                                          static_cast<float>(step)));
        }
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    10.0F, 0.0001F);

        ASSERT_TRUE(editor.CommitTransformGesture());
        EXPECT_EQ(editor.GetToolSessionPhase(), EditorToolSessionPhase::Idle);
        EXPECT_EQ(editor.GetLastToolSessionOutcome(), EditorToolSessionOutcome::Committed);

        // Ten previews, one undo step: straight back to the start, and nothing left to undo after.
        ASSERT_TRUE(editor.Undo());
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    0.0F, 0.0001F);
        EXPECT_FALSE(editor.Undo());

        ASSERT_TRUE(editor.Redo());
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    10.0F, 0.0001F);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, TransformGesture_CancelRestoresTheBaselineAndLeavesNoHistory) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_cancel.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity =
            SelectFreshRoot(engine, editor, Math::Vector3{3.0F, 0.0F, 0.0F});
        const bool dirtyBeforeGesture = editor.IsSceneDirty();

        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::X, 25.0F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    28.0F, 0.0001F);

        ASSERT_TRUE(editor.CancelTransformGesture());
        EXPECT_EQ(editor.GetLastToolSessionOutcome(), EditorToolSessionOutcome::Cancelled);
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    3.0F, 0.0001F);
        // An abandoned drag must not leave the document looking modified, or the user is prompted
        // to save a change they explicitly threw away.
        EXPECT_EQ(editor.IsSceneDirty(), dirtyBeforeGesture);
        EXPECT_FALSE(editor.Undo());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, TransformGesture_CancelRefusesToOverwriteAnExternalChange) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_conflict.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = SelectFreshRoot(engine, editor, Math::Vector3{});

        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::X, 5.0F));

        // Something other than this gesture moves the entity - another tool, a script, the
        // runtime. The baseline is now stale.
        Scene::TransformComponent external{};
        external.localPosition = Math::Vector3{-99.0F, 0.0F, 0.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransform(external));

        // Cancel must decline rather than silently restore over that change.
        EXPECT_FALSE(editor.CancelTransformGesture());
        EXPECT_EQ(editor.GetLastToolSessionOutcome(),
                  EditorToolSessionOutcome::ExternalTransformConflict);
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.x,
                    -99.0F, 0.0001F);
        EXPECT_EQ(editor.GetToolSessionPhase(), EditorToolSessionPhase::Idle);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, TransformGesture_RejectsMultiSelectionAndOutOfOrderCalls) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_guards.uvscene");
        editor.Init();

        // Nothing is in flight, so preview/commit/cancel have nothing to act on.
        EXPECT_FALSE(editor.PreviewTransformGesture(EditorTransformAxis::X, 1.0F));
        EXPECT_FALSE(editor.CommitTransformGesture());
        EXPECT_FALSE(editor.CancelTransformGesture());

        const Scene::Entity first = SelectFreshRoot(engine, editor, Math::Vector3{});
        const Scene::Entity second =
            SelectFreshRoot(engine, editor, Math::Vector3{5.0F, 0.0F, 0.0F});
        ASSERT_NE(first, second);

        // Two entities selected: a transform gesture has no single pivot to act on, matching the
        // existing single-selection rule the four axis commands already enforce.
        editor.SelectEntity(first);
        editor.ToggleEntitySelection(second);
        EXPECT_FALSE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));

        // Back to one, and a re-entrant Begin is refused without disturbing the live session.
        editor.SelectEntity(first);
        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Scale));
        EXPECT_FALSE(editor.BeginTransformGesture(EditorToolSessionMode::Rotate));
        EXPECT_EQ(editor.GetToolSessionPhase(), EditorToolSessionPhase::Previewing);

        // The mode captured at Begin is the one that applies, so this previews a SCALE even though
        // a rotate Begin was attempted in between.
        ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::Y, 1.5F));
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(first).localScale.y,
                    2.5F, 0.0001F);

        // The scale floor still rejects, and a rejected preview leaves the last good one standing.
        EXPECT_FALSE(editor.PreviewTransformGesture(EditorTransformAxis::Y, -50.0F));
        EXPECT_NEAR(entityManager.GetComponent<Scene::TransformComponent>(first).localScale.y,
                    2.5F, 0.0001F);
        ASSERT_TRUE(editor.CancelTransformGesture());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, TransformGesture_NoOpDragCommitsWithoutHistoryOrDirtyingTheScene) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_noop.uvscene");
        editor.Init();
        static_cast<void>(SelectFreshRoot(engine, editor, Math::Vector3{}));
        const bool dirtyBeforeGesture = editor.IsSceneDirty();

        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::X, 0.0F));
        ASSERT_TRUE(editor.CommitTransformGesture());

        EXPECT_EQ(editor.GetLastToolSessionOutcome(),
                  EditorToolSessionOutcome::CompletedWithoutChange);
        EXPECT_EQ(editor.IsSceneDirty(), dirtyBeforeGesture);
        EXPECT_FALSE(editor.Undo());

        editor.Shutdown();
    }
    engine.Shutdown();
}

// Snapping must mean the same thing on both paths, or a drag with Snap on would quantise
// differently from the keyboard command that nominally does the same operation.
TEST(EditorTest, TransformGesture_SnappingQuantisesIdenticallyToTheEquivalentCommand) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_gesture_snap.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        EditorTransformSnappingSettings snapping{};
        snapping.enabled = true;
        snapping.translateStep = 0.5F;
        ASSERT_TRUE(editor.SetTransformSnappingSettings(snapping));

        const Scene::Entity viaCommand = SelectFreshRoot(engine, editor, Math::Vector3{});
        ASSERT_TRUE(editor.TranslateSelectedAlongAxis(EditorTransformAxis::X, 1.31F));
        const float commandResult =
            entityManager.GetComponent<Scene::TransformComponent>(viaCommand).localPosition.x;

        static_cast<void>(SelectFreshRoot(engine, editor, Math::Vector3{}));
        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        ASSERT_TRUE(editor.PreviewTransformGesture(EditorTransformAxis::X, 1.31F));
        ASSERT_TRUE(editor.CommitTransformGesture());
        const float gestureResult = entityManager
                                        .GetComponent<Scene::TransformComponent>(
                                            editor.GetSelectedEntity())
                                        .localPosition.x;

        EXPECT_NEAR(gestureResult, commandResult, 1e-6F);
        EXPECT_NEAR(gestureResult, 1.5F, 1e-6F);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, RetargetPreview_PutsTheSceneAsideAndBringsItBack) {
    Core::EngineConfig config = MakeEditorTestConfig();
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_retarget_preview.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity mine = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(mine, Scene::kInvalidEntity);
        const auto namesInWorld = [&entityManager] {
            std::vector<std::string> names;
            entityManager.ForEach<Scene::NameComponent>(
                [&names](const Scene::Entity, const Scene::NameComponent& name) { names.push_back(name.name); });
            return names;
        };
        const auto has = [&namesInWorld](const std::string& name) {
            const std::vector<std::string> names = namesInWorld();
            return std::ranges::find(names, name) != names.end();
        };
        ASSERT_TRUE(editor.IsSceneDirty());

        editor.OpenRetargetWindow({});
        ASSERT_TRUE(editor.IsRetargetWindowOpen());
        ASSERT_TRUE(EditorAccess::IsRetargetPreviewActive(editor));
        // The scene is set aside, and nothing of the preview is authored into it.
        EXPECT_FALSE(entityManager.IsAlive(mine));
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.SaveScene());
        EXPECT_FALSE(editor.EnterPlayMode());
        EXPECT_EQ(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D), Scene::kInvalidEntity);

        // The world: a sun and the humanoid with a colour for each of its bones (the sky, the ground
        // and the floor are the viewport's studio view, not objects).
        EditorAccess::RebuildRetargetPreview(editor, RetargetPlan{}, {});
        for (const char* const name : {"Retarget Preview", "Sun", "Figures", "Humanoid", "Humanoid Skeleton"}) {
            EXPECT_TRUE(has(name)) << name;
        }
        EXPECT_FALSE(has("Character")) << "no character was picked";
        const Scene::Entity humanoid = EditorAccess::GetRetargetSourceSkeleton(editor);
        ASSERT_NE(humanoid, Scene::kInvalidEntity);
        const auto& bones = entityManager.GetComponent<Scene::Skeleton3DComponent>(humanoid).bones;
        EXPECT_EQ(bones.size(), 162U);
        EXPECT_EQ(EditorAccess::GetRetargetSourceColourCount(editor), bones.size());
        // A rebuild replaces the world, it does not pile a second one on.
        EditorAccess::RebuildRetargetPreview(editor, RetargetPlan{}, {});
        const std::vector<std::string> names = namesInWorld();
        EXPECT_EQ(std::ranges::count(names, "Sun"), 1);

        editor.CloseRetargetWindow();
        EXPECT_FALSE(editor.IsRetargetWindowOpen());
        EXPECT_FALSE(EditorAccess::IsRetargetPreviewActive(editor));
        EXPECT_FALSE(has("Retarget Preview"));
        EXPECT_FALSE(has("Humanoid"));
        EXPECT_TRUE(has("Object3D")) << "the scene is back";
        EXPECT_TRUE(editor.IsSceneDirty()) << "as unsaved as it was";
        EXPECT_TRUE(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D) != Scene::kInvalidEntity);
        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace
namespace {

/// A 2x2 24-bit BMP: the smallest source image a picker can hand over, and one whose bytes this
/// test can state without a codec dependency. The publish path is the same for every source image
/// (the importer is chosen by extension), and the PNG/JPEG/TGA importers have their own tests.
[[nodiscard]] std::vector<std::uint8_t> MakeTwoByTwoBmp() {
    std::vector<std::uint8_t> bytes;
    const auto append16 = [&bytes](const std::uint16_t value) {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    };
    const auto append32 = [&bytes](const std::uint32_t value) {
        for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    };
    append16(0x4D42U);            // "BM"
    append32(70U);                // whole file: 14-byte header + 40-byte info + two padded rows
    append16(0U);
    append16(0U);
    append32(54U);                // pixel data offset
    append32(40U);                // BITMAPINFOHEADER
    append32(2U);                 // width
    append32(2U);                 // height
    append16(1U);                 // planes
    append16(24U);                // bits per pixel
    append32(0U);                 // BI_RGB
    append32(16U);                // pixel bytes
    append32(2835U);
    append32(2835U);
    append32(0U);
    append32(0U);
    // Bottom row first, BGR, each row padded to a multiple of four bytes.
    for (const std::array<std::uint8_t, 3U>& pixel : {std::array<std::uint8_t, 3U>{144U, 128U, 112U},
                                                       std::array<std::uint8_t, 3U>{192U, 176U, 160U}}) {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    bytes.insert(bytes.end(), {0U, 0U});
    for (const std::array<std::uint8_t, 3U>& pixel : {std::array<std::uint8_t, 3U>{48U, 32U, 16U},
                                                       std::array<std::uint8_t, 3U>{96U, 80U, 64U}}) {
        bytes.insert(bytes.end(), pixel.begin(), pixel.end());
    }
    bytes.insert(bytes.end(), {0U, 0U});
    return bytes;
}

} // namespace

// A texture slot must end up holding something the project can ship. The picker is handed whatever
// file the author has - usually the source image, freshly dropped into Content - and the material
// stores a GUID, so this is where the two have to be reconciled: publish the source to the .uvtex
// beside it and hand back that GUID. Re-picking an image whose .uvtex already exists must reuse it,
// or every pick would publish a second copy and change what the material points at.
TEST(EditorTest, PickingASourceImagePublishesTheTextureBesideItAndReusesIt) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("texture_publish");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);
    const std::filesystem::path sourcePath = content / "rock.bmp";
    const std::filesystem::path publishedPath = content / "rock.uvtex";
    {
        const std::vector<std::uint8_t> bytes = MakeTwoByTwoBmp();
        std::ofstream file(sourcePath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file.good());
    }

    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene");
        editor.Init();

        const Asset::AssetGuid published = EditorAccess::PublishTextureSource(editor, sourcePath);
        ASSERT_NE(published, Asset::kInvalidAssetGuid);
        ASSERT_TRUE(std::filesystem::is_regular_file(publishedPath));
        const std::uintmax_t publishedSize = std::filesystem::file_size(publishedPath);

        // What the slot now holds is a real texture, not a path that happens to parse.
        Asset::TextureAsset texture;
        ASSERT_TRUE(Asset::LoadTextureAsset(publishedPath, texture));
        EXPECT_EQ(texture.width, 2U);
        EXPECT_EQ(texture.height, 2U);
        ASSERT_EQ(texture.pixels.size(), 2U * 2U * 4U);
        EXPECT_EQ(texture.pixels[0], std::byte{16});
        EXPECT_EQ(texture.pixels[3], std::byte{255});

        // The same picture picked again is the same asset: the .uvtex beside it is reused, not
        // published a second time under another name.
        EXPECT_EQ(EditorAccess::PublishTextureSource(editor, sourcePath), published);
        EXPECT_EQ(std::filesystem::file_size(publishedPath), publishedSize);
        // And a pick that already names the published file registers it as it stands.
        EXPECT_EQ(EditorAccess::PublishTextureSource(editor, publishedPath), published);

        // The same pick is also what gives a flat picture something for the light to catch: the
        // normal map is derived beside the albedo, as an asset in its own right, so the author can
        // replace it without touching the picture it came from.
        const std::optional<Asset::AssetGuid> normal =
            EditorAccess::DeriveNormalMapForAlbedo(editor, published);
        ASSERT_TRUE(normal.has_value());
        const std::filesystem::path normalPath = content / "rock_normal.uvtex";
        ASSERT_TRUE(std::filesystem::is_regular_file(normalPath));
        Asset::TextureAsset normalTexture;
        ASSERT_TRUE(Asset::LoadTextureAsset(normalPath, normalTexture));
        EXPECT_EQ(normalTexture.usage, Asset::TextureUsage::Normal);
        EXPECT_EQ(normalTexture.colorSpace, Asset::TextureAssetColorSpace::Linear);
        EXPECT_EQ(normalTexture.width, 2U);
        // Picking the same picture again derives nothing new: the map beside it is the one it
        // produced the first time.
        EXPECT_EQ(EditorAccess::DeriveNormalMapForAlbedo(editor, published), normal);

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

} // namespace UVE::Editor::Tests

namespace UVE::Editor::Tests {
namespace {

TEST(EditorTest, SceneRootInspector_ShowsExactlyTheObjectSectionInOrder) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_scene_root_inspector.uvscene");
        editor.Init();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        // No Name, Hierarchy or Transform; Thread Group lives inside Process.
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, root),
                  (std::vector<std::string>{"process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));

        // On an object without Process, Thread Group still has a section of its own.
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity object = entityManager.CreateEntity();
        AttachRoot(engine, object, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::ThreadGroupComponent>(object, Scene::ThreadGroupComponent{});
        const std::vector<std::string> ids = EditorAccess::GetEligibleInspectorDrawerIds(editor, object);
        EXPECT_NE(std::find(ids.begin(), ids.end(), "thread-group"), ids.end());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ScriptSlot_NewUVScriptOpensATextEditorThatChecksAsYouType) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_uvscript_slot.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        editor.SelectEntity(root);
        ASSERT_TRUE(editor.SetSelectedEntityName("hero"));

        ASSERT_TRUE(editor.CreateUVScriptForSelectedEntity());
        const std::string path = entityManager.GetComponent<Scene::ScriptComponent>(root).scriptAssetPath;
        EXPECT_EQ(path.rfind("scripts/hero", 0), 0U);
        EXPECT_TRUE(path.ends_with(".uvs"));
        EXPECT_TRUE(editor.DescribeScriptAssetProblem(path).empty());
        EXPECT_TRUE(EditorAccess::IsScriptingWorkspaceActive(editor));
        ASSERT_TRUE(editor.GetOpenUVScript().has_value());
        const Editor::UVScriptDocument& document = *editor.GetOpenUVScript();
        EXPECT_EQ(document.path, path);
        EXPECT_EQ(document.text.rfind("entity hero : ", 0), 0U);
        EXPECT_TRUE(document.diagnostics.empty()); // The template compiles against the object.
        EXPECT_FALSE(document.IsDirty());
        EXPECT_FALSE(editor.CreateUVScriptForSelectedEntity()); // The slot is filled now.

        // A mistake is reported while typing, with its line.
        editor.SetOpenUVScriptText("on tick(dt):\n    nope += 1\n");
        ASSERT_FALSE(editor.GetOpenUVScript()->diagnostics.empty());
        EXPECT_EQ(editor.GetOpenUVScript()->diagnostics.front().at.line, 2U);

        // Fixed and saved: the file holds the new text and nothing is unsaved.
        const std::string fixed = "var ticks = 0\n\non tick(dt):\n    ticks += 1\n";
        editor.SetOpenUVScriptText(fixed);
        EXPECT_TRUE(editor.GetOpenUVScript()->diagnostics.empty());
        EXPECT_TRUE(editor.GetOpenUVScript()->IsDirty());
        ASSERT_TRUE(editor.SaveOpenUVScript());
        EXPECT_FALSE(editor.GetOpenUVScript()->IsDirty());
        std::ifstream file(path, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), fixed);

        // Close returns to the scene; opening the object's script comes back to the text editor.
        editor.CloseOpenUVScript();
        EXPECT_FALSE(editor.GetOpenUVScript().has_value());
        EXPECT_FALSE(EditorAccess::IsScriptingWorkspaceActive(editor));
        ASSERT_TRUE(editor.OpenScriptGraphForEntity(root));
        ASSERT_TRUE(editor.GetOpenUVScript().has_value());
        EXPECT_EQ(editor.GetOpenUVScript()->text, fixed);

        editor.CloseOpenUVScript();
        std::error_code error;
        std::filesystem::remove(path, error);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ScriptExports_ShowTheScriptsExportsAndStoreTheObjectsValues) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_script_exports.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        editor.SelectEntity(root);
        ASSERT_TRUE(editor.SetSelectedEntityName("exporter"));
        EXPECT_TRUE(editor.GetSelectedScriptExports().empty()); // No script yet.

        ASSERT_TRUE(editor.CreateUVScriptForSelectedEntity());
        editor.SetOpenUVScriptText("export speed = 6.0\nexport jumps = 2\nexport label = \"hero\"\n"
                                      "export offset = vec3(0.0, 1.0, 0.0)\nvar hidden = 1\n");
        ASSERT_TRUE(editor.SaveOpenUVScript());
        const std::string path = entityManager.GetComponent<Scene::ScriptComponent>(root).scriptAssetPath;
        editor.CloseOpenUVScript();

        // Exports only, in declaration order, at the script's defaults.
        std::vector<Editor::ScriptExportRow> rows = editor.GetSelectedScriptExports();
        ASSERT_EQ(rows.size(), 4U);
        EXPECT_EQ(rows[0].name, "speed");
        EXPECT_EQ(rows[0].valueText, "6.0");
        EXPECT_EQ(rows[1].valueText, "2");
        EXPECT_EQ(rows[2].valueText, "hero");
        EXPECT_EQ(rows[3].valueText, "(0.0, 1.0, 0.0)");
        EXPECT_FALSE(rows[0].overridden);

        // A value of the right type is stored in its canonical form; a wrong one is refused.
        ASSERT_TRUE(editor.SetSelectedScriptExport("speed", "9"));
        EXPECT_EQ(entityManager.GetComponent<Scene::ScriptComponent>(root).exportValues.at("speed"), "9.0");
        EXPECT_FALSE(editor.SetSelectedScriptExport("jumps", "lots"));
        EXPECT_FALSE(editor.SetSelectedScriptExport("hidden", "3"));
        rows = editor.GetSelectedScriptExports();
        EXPECT_TRUE(rows[0].overridden);
        EXPECT_EQ(rows[0].valueText, "9.0");
        EXPECT_EQ(rows[0].defaultText, "6.0");

        // One undo step per edit; resetting drops the stored value.
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.GetComponent<Scene::ScriptComponent>(root).exportValues.empty());
        ASSERT_TRUE(editor.Redo());
        ASSERT_TRUE(editor.SetSelectedScriptExport("speed", std::nullopt));
        EXPECT_TRUE(entityManager.GetComponent<Scene::ScriptComponent>(root).exportValues.empty());

        std::error_code error;
        std::filesystem::remove(path, error);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ObjectMetadata_EveryEditIsOneUndoStep) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_object_metadata.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        editor.SelectEntity(root);
        const auto entries = [&] { return entityManager.GetComponent<Scene::ObjectMetadataComponent>(root).entries; };

        EXPECT_FALSE(editor.AddSelectedObjectMetadata("2bad", Core::Variant::MakeInt(1)));
        ASSERT_TRUE(editor.AddSelectedObjectMetadata("charges", Core::Variant::MakeFloat(3.5)));
        EXPECT_FALSE(editor.AddSelectedObjectMetadata("charges", Core::Variant::MakeInt(1)));
        ASSERT_TRUE(editor.RenameSelectedObjectMetadata("charges", "ammo"));
        // 3.5 -> int loses the fraction: refused unless the author confirms.
        EXPECT_FALSE(editor.ChangeSelectedObjectMetadataType("ammo", Core::VariantType::Int, false));
        ASSERT_TRUE(editor.ChangeSelectedObjectMetadataType("ammo", Core::VariantType::Int, true));
        EXPECT_EQ(entries().front().value.GetType(), Core::VariantType::Int);
        ASSERT_TRUE(editor.RemoveSelectedObjectMetadata("ammo"));
        EXPECT_TRUE(entries().empty());

        ASSERT_TRUE(editor.Undo()); // remove
        EXPECT_EQ(entries().front().value.GetType(), Core::VariantType::Int);
        ASSERT_TRUE(editor.Undo()); // retype
        EXPECT_EQ(entries().front().value, Core::Variant::MakeFloat(3.5));
        ASSERT_TRUE(editor.Undo()); // rename
        EXPECT_EQ(entries().front().key, "charges");
        ASSERT_TRUE(editor.Undo()); // add
        EXPECT_TRUE(entries().empty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, RenderInstanceChildInspector_IsOwnSectionThenBasesThenObject3DThenObject) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_render_instance_inspector.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity decal = entityManager.CreateEntity();
        AttachRoot(engine, decal, Scene::TransformComponent{});
        Scene::ApplyDecal3DObjectDefinition(entityManager, decal, Scene::Decal3DObjectDefinition{});
        const Scene::Entity fog = entityManager.CreateEntity();
        AttachRoot(engine, fog, Scene::TransformComponent{});
        Scene::ApplyFogVolume3DObjectDefinition(entityManager, fog, Scene::FogVolume3DObjectDefinition{});
        const auto expected = [](const std::string& own) {
            return std::vector<std::string>{own,       "render-instance",       "transform",      "visibility",
                                            "process", "physics-interpolation", "auto-translate", "editor-description",
                                            "script",  "object-metadata"};
        };
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, decal), expected("decal-3d"));
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, fog), expected("fog-volume-3d"));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, SunAndWorldEnvironmentInspectors_FollowTheirClassChains) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_sun_environment_inspector.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        // DirectionalLight3D: its own section, then LightEmitter3D, RenderInstance3D, Object3D, Object.
        const Scene::Entity sun = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::DirectionalLight3D);
        ASSERT_NE(sun, Scene::kInvalidEntity);
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, sun), Scene::Objects::SceneObjectKind::DirectionalLight3D);
        EXPECT_TRUE(entityManager.GetComponent<Scene::LightEmitterComponent>(sun).shadowEnabled)
            << "a sun casts shadows by default";
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, sun),
                  (std::vector<std::string>{"directional-light-3d", "light-emitter", "render-instance", "transform",
                                            "visibility", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        // WorldEnvironment: a pure Object - its own section, then Object's; no Transform, no Visibility.
        const Scene::Entity environment =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::WorldEnvironment3D);
        ASSERT_NE(environment, Scene::kInvalidEntity);
        EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(environment));
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, environment),
                  (std::vector<std::string>{"world-environment", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        editor.Shutdown();
    }
    engine.Shutdown();
}

// The 16 kinds that carry a component but no ObjectDefinition of their own used to be named through
// EditorEntityKind::Empty, so every one of them appeared in the Outliner as "Object3D". This
// locks each to its own registry displayName, and locks the uniqueness rule that keeps a second one
// from colliding with the first.
TEST(EditorTest, ComponentOnlySceneObjects_AreNamedForTheirOwnKindNotObject3D) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_component_only_names.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        constexpr std::array<Scene::Objects::SceneObjectKind, 16> kComponentOnlyKinds{
            Scene::Objects::SceneObjectKind::RayCast3D,
            Scene::Objects::SceneObjectKind::NavMeshVolume3D,
            Scene::Objects::SceneObjectKind::NavSeeker3D,
            Scene::Objects::SceneObjectKind::BoneAttachment3D,
            Scene::Objects::SceneObjectKind::Marker3D,
            Scene::Objects::SceneObjectKind::Hitbox3D,
            Scene::Objects::SceneObjectKind::Hurtbox3D,
            Scene::Objects::SceneObjectKind::Projectile3D,
            Scene::Objects::SceneObjectKind::InteractionArea3D,
            Scene::Objects::SceneObjectKind::ReflectionProbe3D,
            Scene::Objects::SceneObjectKind::LODGroup3D,
            Scene::Objects::SceneObjectKind::Occluder3D,
            Scene::Objects::SceneObjectKind::VisibilityRegion3D,
            Scene::Objects::SceneObjectKind::SpawnPoint3D,
            Scene::Objects::SceneObjectKind::LevelStreamer3D,
            Scene::Objects::SceneObjectKind::WorldPartition3D,
        };

        for (const Scene::Objects::SceneObjectKind kind : kComponentOnlyKinds) {
            const Scene::Objects::SceneObjectDescriptor* const descriptor =
                Scene::Objects::FindSceneObjectDescriptor(kind);
            ASSERT_NE(descriptor, nullptr);

            const Scene::Entity first = editor.CreateDocumentSceneObject(kind);
            ASSERT_NE(first, Scene::kInvalidEntity) << descriptor->displayName;
            EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(first).name, descriptor->displayName)
                << "a freshly added " << descriptor->displayName << " must not be called Object3D";

            // Same rule the 29 kinds with an ObjectDefinition follow: the second one is suffixed
            // rather than given a duplicate name.
            const Scene::Entity second = editor.CreateDocumentSceneObject(kind);
            ASSERT_NE(second, Scene::kInvalidEntity) << descriptor->displayName;
            EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(second).name,
                      std::string{descriptor->displayName} + " 2")
                << "the second " << descriptor->displayName << " must not collide with the first";
        }
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, SurfaceInstanceChildInspector_IsOwnSectionThenSurfaceRenderObject3DObject) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_surface_instance_inspector.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const auto create = [&](const auto& apply) {
            const Scene::Entity entity = entityManager.CreateEntity();
            AttachRoot(engine, entity, Scene::TransformComponent{});
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
                                            "object-metadata"};
        };
        const Scene::Entity mesh = create([&](const Scene::Entity entity) {
            Scene::ApplyMeshInstance3DObjectDefinition(entityManager, entity, Scene::MeshInstance3DObjectDefinition{});
        });
        const Scene::Entity box = create([&](const Scene::Entity entity) {
            Scene::ApplyBoxMesh3DObjectDefinition(entityManager, entity, Scene::BoxMesh3DObjectDefinition{});
        });
        const Scene::Entity sphere = create([&](const Scene::Entity entity) {
            Scene::ApplySphereMesh3DObjectDefinition(entityManager, entity, Scene::SphereMesh3DObjectDefinition{});
        });
        const Scene::Entity plane = create([&](const Scene::Entity entity) {
            Scene::ApplyPlaneMesh3DObjectDefinition(entityManager, entity, Scene::PlaneMesh3DObjectDefinition{});
        });
        const Scene::Entity particles = create([&](const Scene::Entity entity) {
            Scene::ApplyParticleEmitter3DObjectDefinition(entityManager, entity,
                                                           Scene::ParticleEmitter3DObjectDefinition{});
        });
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, mesh), expected("mesh"));
        // The primitive's collision is drawn inside its own section, not as a section of its own.
        for (const Scene::Entity primitive : {box, sphere, plane}) {
            EXPECT_TRUE(entityManager.HasComponent<Scene::ColliderComponent>(primitive));
            EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, primitive),
                      expected("primitive-mesh"));
        }
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, particles),
                  expected("particle-emitter"));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, CharacterBodyInspector_IsItsChainToTheRootAndNothingElse) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_character_body_inspector.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity body = entityManager.CreateEntity();
        AttachRoot(engine, body, Scene::TransformComponent{});
        Scene::ApplyCharacter3DObjectDefinition(
            entityManager, body, Scene::Character3DObjectDefinition::WithDefaultCollider());
        // Character3D > SolidBody3D > PhysicsObject3D > Object3D > Object. The capsule, layer and mask
        // are drawn inside PhysicsObject3D, so the collider has no section of its own.
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, body),
                  (std::vector<std::string>{"character-controller", "solid-body", "physics-object", "transform",
                                            "visibility", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        // A primitive still draws its collider in its own section, not in PhysicsObject3D's.
        const Scene::Entity box = entityManager.CreateEntity();
        AttachRoot(engine, box, Scene::TransformComponent{});
        Scene::ApplyBoxMesh3DObjectDefinition(entityManager, box, Scene::BoxMesh3DObjectDefinition{});
        const std::vector<std::string> boxIds = EditorAccess::GetEligibleInspectorDrawerIds(editor, box);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "collider"), 0);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "physics-object"), 0);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, Folder_GroupsObjectsWithoutMovingThem) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_folder.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity cube = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::BoxMesh3D);
        ASSERT_NE(cube, Scene::kInvalidEntity);
        entityManager.GetComponent<Scene::TransformComponent>(cube).localPosition = Math::Vector3{3.0F, 0.0F, 0.0F};
        editor.SelectEntity(editor.GetDocumentSceneRoot());
        const Scene::Entity folder = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Folder);
        ASSERT_NE(folder, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(folder));
        EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(folder));
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, folder), Scene::Objects::SceneObjectKind::Folder);
        // Moving the cube into the folder keeps it where it was.
        editor.SelectEntity(cube);
        ASSERT_TRUE(editor.ReparentSelectedEntity(folder));
        EXPECT_EQ(entityManager.GetComponent<Scene::TransformComponent>(cube).localPosition.x, 3.0F);
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, folder), (std::vector<std::string>{"Object"}));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, InspectorHeaders_SpellOutTheClassChain) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_chain_headers.uvscene");
        editor.Init();
        const Scene::Entity body =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Character3D);
        const Scene::Entity player =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationSequencer);
        ASSERT_NE(body, Scene::kInvalidEntity);
        ASSERT_NE(player, Scene::kInvalidEntity);
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, body),
                  (std::vector<std::string>{"Character3D", "SolidBody3D", "PhysicsObject3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, player),
                  (std::vector<std::string>{"AnimationSequencer", "AnimationDriver", "Object"}));
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, editor.GetDocumentSceneRoot()),
                  (std::vector<std::string>{"Object"}));

        const Scene::Entity occluder =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Occluder3D);
        const Scene::Entity fog =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::FogVolume3D);
        const Scene::Entity sun =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::DirectionalLight3D);
        ASSERT_NE(occluder, Scene::kInvalidEntity);
        ASSERT_NE(fog, Scene::kInvalidEntity);
        ASSERT_NE(sun, Scene::kInvalidEntity);
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, occluder),
                  (std::vector<std::string>{"Occluder3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, fog),
                  (std::vector<std::string>{"FogVolume3D", "RenderInstance3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorAccess::GetInspectorGroupHeaders(editor, sun),
                  (std::vector<std::string>{"DirectionalLight3D", "LightEmitter3D", "RenderInstance3D", "Object3D",
                                            "Object"}));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, AnimationObjects_InspectorIsTheirOwnSectionThenTheObjectSection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_animation_inspector.uvscene");
        editor.Init();
        const Scene::Entity player =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationSequencer);
        const Scene::Entity tree = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationGraph);
        ASSERT_NE(player, Scene::kInvalidEntity);
        ASSERT_NE(tree, Scene::kInvalidEntity);
        // AnimationSequencer > AnimationDriver > Object: a pure Object, no Transform, no Visibility.
        const std::vector<std::string> objectSection{"animation-mixer", "process", "physics-interpolation",
                                                   "auto-translate", "editor-description", "script", "object-metadata"};
        std::vector<std::string> expected{"animation-player"};
        expected.insert(expected.end(), objectSection.begin(), objectSection.end());
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, player), expected);
        expected.front() = "animation-tree";
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, tree), expected);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, AnimationObjects_APureObjectReparentsAndUndoes) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_animation_reparent.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity player =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationSequencer);
        editor.SelectEntity(editor.GetDocumentSceneRoot());
        const Scene::Entity door = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(door, Scene::kInvalidEntity);
        ASSERT_NE(player, Scene::kInvalidEntity);
        const Scene::Entity rootParent = entityManager.GetComponent<Scene::HierarchyComponent>(player).parent;

        editor.SelectEntity(player);
        ASSERT_TRUE(editor.ReparentSelectedEntity(door));
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(player).parent, door);
        EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(player));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(player).parent, rootParent);
        ASSERT_TRUE(editor.Redo());
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(player).parent, door);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ColorPickerSession_ManyLiveChangesAreOneUndoStep) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_color_session.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity light = entityManager.CreateEntity();
        AttachRoot(engine, light, Scene::TransformComponent{});
        Scene::LightComponent authored{};
        authored.color = Math::Color{1.0F, 1.0F, 1.0F};
        entityManager.AddComponent<Scene::LightComponent>(light, authored);
        const Scene::Entity other = entityManager.CreateEntity();
        AttachRoot(engine, other, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::LightComponent>(other, authored);
        editor.SelectEntity(light);

        const Core::TypeMetadataEntry* const entry =
            Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::LightComponent)));
        ASSERT_NE(entry, nullptr);
        const auto colorProperty = std::find_if(entry->properties.begin(), entry->properties.end(),
                                                [](const auto& candidate) { return candidate.name == "color"; });
        ASSERT_NE(colorProperty, entry->properties.end());
        const auto liveColor = [&](const Scene::Entity entity) {
            return entityManager.GetComponent<Scene::LightComponent>(entity).color;
        };

        // A drag across the picker: every step is visible at once, none of them is history.
        EXPECT_FALSE(editor.CanUndo());
        for (int step = 1; step <= 30; ++step) {
            const Math::Color value{1.0F, 1.0F - (static_cast<float>(step) / 60.0F), 0.2F};
            ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &value));
            EXPECT_FLOAT_EQ(liveColor(light).g, value.g);
        }
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_TRUE(editor.IsSceneDirty());

        // Closing the picker records the whole session as one step.
        ASSERT_TRUE(EditorAccess::CommitComponentPropertyPreview(editor));
        EXPECT_FLOAT_EQ(liveColor(light).g, 0.5F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_FLOAT_EQ(liveColor(light).r, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).g, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).b, 1.0F);
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.Redo());
        EXPECT_FLOAT_EQ(liveColor(light).g, 0.5F);
        ASSERT_TRUE(editor.Undo());

        // Cancel puts the colour back, with no history and the scene as clean as it was.
        const Math::Color red{1.0F, 0.0F, 0.0F};
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(EditorAccess::CancelComponentPropertyPreview(editor));
        EXPECT_FLOAT_EQ(liveColor(light).g, 1.0F);
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(EditorAccess::CommitComponentPropertyPreview(editor));

        // A session that ends where it began records nothing.
        const Math::Vector3 white{1.0F, 1.0F, 1.0F};
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &white));
        ASSERT_TRUE(EditorAccess::CommitComponentPropertyPreview(editor));
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.IsSceneDirty());

        // Undo in the middle of a session finishes it first, then takes it back.
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &red));
        ASSERT_TRUE(editor.Undo());
        EXPECT_FLOAT_EQ(liveColor(light).g, 1.0F);
        EXPECT_FALSE(editor.CanUndo());

        // Moving to another object while a session is open finishes it on the object it began on.
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &red));
        editor.SelectEntity(other);
        const Math::Vector3 blue{0.0F, 0.0F, 1.0F};
        ASSERT_TRUE(EditorAccess::PreviewSelectedComponentProperty(editor, *entry, *colorProperty, &blue));
        ASSERT_TRUE(EditorAccess::CommitComponentPropertyPreview(editor));
        EXPECT_FLOAT_EQ(liveColor(light).g, 0.0F);
        EXPECT_FLOAT_EQ(liveColor(other).b, 1.0F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_FLOAT_EQ(liveColor(other).r, 1.0F);
        EXPECT_FLOAT_EQ(liveColor(light).g, 0.0F);
        ASSERT_TRUE(editor.Undo());
        EXPECT_FLOAT_EQ(liveColor(light).g, 1.0F);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ColorPickerPreferences_SanitiseAndPersistAcrossSessionReload) {
    const Core::EngineConfig config = MakeEditorTestConfig();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_color_prefs.uvscene");
        editor.Init();
        EXPECT_TRUE(editor.GetColorPickerPreferences().advancedOpen);
        EXPECT_TRUE(editor.GetColorPickerPreferences().saved.empty());

        Editor::ColorPickerPreferences preferences;
        preferences.advancedOpen = false;
        preferences.saved = {Editor::EditorColor{1.0F, 0.5F, 0.0F, 1.0F},
                             Editor::EditorColor{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F},
                             Editor::EditorColor{2.0F, -1.0F, 0.25F, 0.5F}};
        for (int index = 0; index < 15; ++index) {
            preferences.recents.push_back(Editor::EditorColor{static_cast<float>(index) / 15.0F, 0.0F, 0.0F, 1.0F});
        }
        editor.SetColorPickerPreferences(preferences);
        // The NaN colour is dropped, the out-of-range one clamped, and the recents trimmed.
        ASSERT_EQ(editor.GetColorPickerPreferences().saved.size(), 2U);
        EXPECT_EQ(Editor::FormatColorHex(editor.GetColorPickerPreferences().saved[1], true), "#FF004080");
        EXPECT_EQ(editor.GetColorPickerPreferences().recents.size(), Editor::kMaxRecentColors);
        ASSERT_TRUE(EditorAccess::SaveSessionSettings(editor));
        Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
        EXPECT_EQ(settings.GetInt("editor.colorPicker.saved.count", -1), 2);
        EXPECT_EQ(settings.GetString("editor.colorPicker.saved.0", ""), "#FF8000FF");
        EXPECT_EQ(settings.GetString("editor.colorPicker.saved.1", ""), "#FF004080");
        editor.Shutdown();
    }
    {
        Editor reloaded(engine.GetServices(), "uve_editor_tests_color_prefs_reload.uvscene");
        reloaded.Init();
        const Editor::ColorPickerPreferences& preferences = reloaded.GetColorPickerPreferences();
        EXPECT_FALSE(preferences.advancedOpen);
        ASSERT_EQ(preferences.saved.size(), 2U);
        EXPECT_EQ(Editor::FormatColorHex(preferences.saved[0], true), "#FF8000FF");
        EXPECT_EQ(Editor::FormatColorHex(preferences.saved[1], true), "#FF004080");
        EXPECT_EQ(preferences.recents.size(), Editor::kMaxRecentColors);
        for (std::size_t index = 0U; index < Editor::kMaxRecentColors; ++index) {
            const Editor::EditorColor expected{static_cast<float>(index) / 15.0F, 0.0F, 0.0F, 1.0F};
            EXPECT_EQ(Editor::FormatColorHex(preferences.recents[index], true),
                      Editor::FormatColorHex(expected, true)) << index;
        }
        reloaded.Shutdown();
    }
    // A stored entry that is not a colour is skipped; the rest of the list still loads.
    engine.GetServices().GetConfigManager().SetString("editor.colorPicker.saved.0", "not a colour");
    {
        Editor corrupt(engine.GetServices(), "uve_editor_tests_color_prefs_corrupt.uvscene");
        corrupt.Init();
        ASSERT_EQ(corrupt.GetColorPickerPreferences().saved.size(), 1U);
        EXPECT_EQ(Editor::FormatColorHex(corrupt.GetColorPickerPreferences().saved[0], true), "#FF004080");
        corrupt.Shutdown();
    }
    // Invalid entries do not consume the saved-colour cap: the legacy loader inspected up to 64
    // raw entries before applying the 24 valid-colour limit.
    Config::IConfigManager& settings = engine.GetServices().GetConfigManager();
    settings.SetInt("editor.colorPicker.saved.count", 26);
    for (std::int64_t index = 0; index < 25; ++index) {
        settings.SetString("editor.colorPicker.saved." + std::to_string(index), "not a colour");
    }
    settings.SetString("editor.colorPicker.saved.25", "#123456");
    {
        Editor invalidPrefix(engine.GetServices(), "uve_editor_tests_color_prefs_invalid_prefix.uvscene");
        invalidPrefix.Init();
        ASSERT_EQ(invalidPrefix.GetColorPickerPreferences().saved.size(), 1U);
        EXPECT_EQ(Editor::FormatColorHex(invalidPrefix.GetColorPickerPreferences().saved[0], true),
                  "#123456FF");
        invalidPrefix.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorTest, InspectorTransformDrag_IsOneUndoStepAndCanBeCancelled) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_inspector_transform_drag.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity object = entityManager.CreateEntity();
        AttachRoot(engine, object, Scene::TransformComponent{});
        editor.SelectEntity(object);
        const auto live = [&] { return entityManager.GetComponent<Scene::TransformComponent>(object); };

        // No gesture in flight: nothing to preview into.
        Scene::TransformComponent moved = live();
        moved.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
        EXPECT_FALSE(editor.PreviewTransformGestureValue(moved));

        // Dragging Position.x from 0 to 3: every frame shows, one undo step results.
        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Translate));
        for (int frame = 1; frame <= 30; ++frame) {
            moved.localPosition.x = static_cast<float>(frame) * 0.1F;
            ASSERT_TRUE(editor.PreviewTransformGestureValue(moved));
            EXPECT_FLOAT_EQ(live().localPosition.x, moved.localPosition.x);
        }
        // A non-finite value is refused and the last good one stays.
        Scene::TransformComponent broken = moved;
        broken.localScale.y = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.PreviewTransformGestureValue(broken));
        EXPECT_FLOAT_EQ(live().localScale.y, 1.0F);
        EXPECT_FALSE(editor.CanUndo());
        ASSERT_TRUE(editor.CommitTransformGesture());
        ASSERT_TRUE(editor.CanUndo());
        ASSERT_TRUE(editor.Undo());
        EXPECT_FLOAT_EQ(live().localPosition.x, 0.0F);
        EXPECT_FALSE(editor.CanUndo());
        EXPECT_FALSE(editor.IsSceneDirty());

        // Cancelled: back where it started, no history.
        ASSERT_TRUE(editor.BeginTransformGesture(EditorToolSessionMode::Scale));
        Scene::TransformComponent scaled = live();
        scaled.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
        ASSERT_TRUE(editor.PreviewTransformGestureValue(scaled));
        ASSERT_TRUE(editor.CancelTransformGesture());
        EXPECT_FLOAT_EQ(live().localScale.x, 1.0F);
        EXPECT_FALSE(editor.CanUndo());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, InspectorMetadataDrag_IsOneUndoStepAndOnlyItsOwnEditIsCommitted) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_inspector_metadata_drag.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        editor.SelectEntity(root);
        ASSERT_TRUE(editor.AddSelectedObjectMetadata("speed", Core::Variant::MakeFloat(1.0)));
        const auto speed = [&] {
            return entityManager.GetComponent<Scene::ObjectMetadataComponent>(root).entries.front().value;
        };

        for (int frame = 1; frame <= 20; ++frame) {
            ASSERT_TRUE(editor.PreviewSelectedObjectMetadataValue(
                "speed", Core::Variant::MakeFloat(1.0 + static_cast<double>(frame))));
        }
        EXPECT_EQ(speed(), Core::Variant::MakeFloat(21.0));

        // Letting go of some other field does not end this drag.
        const Core::TypeMetadataEntry* const light =
            Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::LightComponent)));
        ASSERT_NE(light, nullptr);
        EXPECT_FALSE(EditorAccess::CommitComponentPropertyPreviewFor(editor, *light, light->properties.front()));

        ASSERT_TRUE(EditorAccess::CommitComponentPropertyPreview(editor));
        ASSERT_TRUE(editor.Undo()); // the whole drag
        EXPECT_EQ(speed(), Core::Variant::MakeFloat(1.0));

        // A one-shot edit arriving while a drag is still in flight records the drag first, so
        // undo walks back through both in the order they happened.
        ASSERT_TRUE(editor.PreviewSelectedObjectMetadataValue("speed", Core::Variant::MakeFloat(5.0)));
        ASSERT_TRUE(editor.SetSelectedObjectMetadataValue("speed", Core::Variant::MakeFloat(9.0)));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(speed(), Core::Variant::MakeFloat(5.0));
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(speed(), Core::Variant::MakeFloat(1.0));
        ASSERT_TRUE(editor.Undo()); // the add
        EXPECT_TRUE(entityManager.GetComponent<Scene::ObjectMetadataComponent>(root).entries.empty());
        EXPECT_FALSE(editor.CanUndo());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, InspectorPropertyEdit_RefusesAValueTheComponentRuleRejects) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_mesh_rule.uvscene");
        editor.Init();
        const Scene::Entity entity =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::MeshInstance3D);
        ASSERT_NE(entity, Scene::kInvalidEntity);
        const Core::TypeMetadataEntry* const entry =
            Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::MeshComponent)));
        ASSERT_NE(entry, nullptr);
        const auto property = [&](const std::string& name) {
            return *std::find_if(entry->properties.begin(), entry->properties.end(),
                                 [&](const auto& candidate) { return candidate.name == name; });
        };
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        // A material with no mesh has nothing to draw on: refused, and the component unchanged.
        const Asset::AssetGuid material{0x77U};
        EXPECT_FALSE(EditorAccess::SetSelectedComponentProperty(editor, *entry, property("materialGuid"),
                                                                          &material));
        EXPECT_EQ(entityManager.GetComponent<Scene::MeshComponent>(entity).materialGuid,
                  Asset::kInvalidAssetGuid);
        // A mesh alone is fine: it is drawn with the built-in lit shader.
        const Asset::AssetGuid mesh{0x55U};
        EXPECT_TRUE(EditorAccess::SetSelectedComponentProperty(editor, *entry, property("meshGuid"), &mesh));
        EXPECT_EQ(entityManager.GetComponent<Scene::MeshComponent>(entity).meshGuid, mesh);
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EditorTest, ModelSources_AreImportedAutomaticallyOutsideTheContentFolder) {
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
    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = root;
    config.derivedArtifactCacheRoot = derived / "Import";
    config.projectChangeWatchPollIntervalSeconds = 0.0;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_model_auto_import.uvscene");
        editor.Init();
        const std::filesystem::path imported = EditorAccess::GetImportedModelPath(editor, "Models/tri.obj");
        for (int frame = 0; frame < 8 && !std::filesystem::exists(imported); ++frame) {
            editor.Tick();
            engine.TickFrame();
        }
        // Converted without any action, into derived data - the content folder holds only sources.
        EXPECT_TRUE(std::filesystem::exists(imported));
        EXPECT_TRUE(imported.generic_string().ends_with(
            "uve_editor_tests_model_auto_import_derived/Imported/Models/tri.obj.uvmodel"))
            << imported;
        for (const auto& file : std::filesystem::recursive_directory_iterator(root)) {
            EXPECT_NE(file.path().extension(), ".uvmodel") << file.path();
        }
        // Mesh plus bones reads as a model; a plain mesh does not.
        EXPECT_TRUE(EditorAccess::IsRiggedModelSource(editor, "Models/rig.gltf"));
        EXPECT_FALSE(EditorAccess::IsRiggedModelSource(editor, "Models/tri.obj"));
        // Motion with no mesh is an animation: described, and never sent to the mesh importer.
        const EditorModelSourceInfo* const motion = EditorAccess::FindModelSourceInfo(editor, "Models/strafe.fbx");
        ASSERT_NE(motion, nullptr);
        EXPECT_TRUE(motion->animationOnly);
        EXPECT_FALSE(motion->rigged);
        // Its bones are still a skeleton a Skeleton3D can take.
        EXPECT_TRUE(motion->hasSkeleton);
        EXPECT_EQ(motion->summary, "1 bone, 1 animation, 1.00 s");
        Asset::ProjectFileEntry motionEntry;
        motionEntry.relativePath = "Models/strafe.fbx";
        motionEntry.kind = Asset::ProjectFileEntryKind::File;
        EXPECT_EQ(EditorAccess::GetAssetTableEntryTypeLabel(editor, motionEntry), "Animation");
        Asset::ProjectFileEntry rigEntry;
        rigEntry.relativePath = "Models/rig.gltf";
        rigEntry.kind = Asset::ProjectFileEntryKind::File;
        EXPECT_EQ(EditorAccess::GetAssetTableEntryTypeLabel(editor, rigEntry), "Skeletal Mesh");
        EXPECT_FALSE(EditorAccess::IsModelImportQueued(editor, "Models/strafe.fbx"));
        EXPECT_FALSE(std::filesystem::exists(EditorAccess::GetImportedModelPath(editor, "Models/strafe.fbx")));
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(derived);
}

TEST(EditorTest, Skeleton3D_StartsEmptyAndTakesItsBonesFromARiggedModel) {
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
    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = root;
    config.derivedArtifactCacheRoot = "uve_editor_tests_skeleton_derived/Import";
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_skeleton.uvscene");
        editor.Init();
        editor.Tick(); // first project refresh
        const Scene::Entity skeleton = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Skeleton3D);
        ASSERT_NE(skeleton, Scene::kInvalidEntity);
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        // A Object3D child, empty until pointed at a rig, and named for what it is.
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(skeleton).name, "Skeleton3D");
        EXPECT_TRUE(entityManager.GetComponent<Scene::Skeleton3DComponent>(skeleton).bones.empty());
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, skeleton),
                  (std::vector<std::string>{"skeleton-3d", "transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "object-metadata"}));

        // A model with no armature is refused and the object stays as it was.
        EXPECT_FALSE(EditorAccess::BindSelectedSkeletonSource(editor, "Characters/rock.gltf"));
        EXPECT_TRUE(entityManager.GetComponent<Scene::Skeleton3DComponent>(skeleton).bones.empty());

        ASSERT_TRUE(EditorAccess::BindSelectedSkeletonSource(editor, "Characters/hero.gltf"));
        const Scene::Skeleton3DComponent& bound =
            entityManager.GetComponent<Scene::Skeleton3DComponent>(skeleton);
        EXPECT_EQ(bound.skeletonAssetPath, "Characters/hero.gltf");
        ASSERT_EQ(bound.bones.size(), 2U);
        EXPECT_EQ(bound.bones[0].name, "Hips");
        EXPECT_EQ(bound.bones[1].parentIndex, 0);
        // One undo step takes the object back to empty.
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.GetComponent<Scene::Skeleton3DComponent>(skeleton).bones.empty());
        EXPECT_TRUE(entityManager.GetComponent<Scene::Skeleton3DComponent>(skeleton).skeletonAssetPath.empty());
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all("uve_editor_tests_skeleton_derived");
}

TEST(EditorTest, Object3DInspector_IsTransformVisibilityAndTheObjectSection) {
    Core::EngineCore engine(MakeEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_editor_tests_object3d_inspector.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity object = entityManager.CreateEntity();
        AttachRoot(engine, object, Scene::TransformComponent{});
        Scene::ApplyObject3DObjectDefinition(entityManager, object, Scene::Object3DObjectDefinition{});
        EXPECT_EQ(EditorAccess::GetEligibleInspectorDrawerIds(editor, object),
                  (std::vector<std::string>{"transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "object-metadata"}));
        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace

TEST(EditorTest, HierarchyDoubleClick_OpensTheAssetBehindARowAndLeavesPlainObjectsToTheSetting) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("hierarchy_double_click");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.Init();

        // A plain object is the scene's own: there is no file behind it, so the row reports that it
        // has nothing to open and the Double-Click preference keeps deciding for it.
        const Scene::Entity plain =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(plain, Scene::kInvalidEntity);
        EXPECT_FALSE(EditorAccess::OpenHierarchyRowTarget(editor, plain));

        // An entity asset placed into the level is an instance: its row opens that asset.
        const auto asset = editor.CreateContentCatalogueItem("empty", content);
        ASSERT_TRUE(asset.has_value());
        const Scene::Entity instance = editor.PlaceEntityAsset(*asset);
        ASSERT_NE(instance, Scene::kInvalidEntity);
        ASSERT_TRUE(engine.GetServices().GetEntityManager().HasComponent<Scene::PrefabInstanceComponent>(
            instance));
        EXPECT_TRUE(EditorAccess::OpenHierarchyRowTarget(editor, instance));
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        EXPECT_EQ(editor.GetEntityEditorAssetPath(), *asset);
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, HierarchyDoubleClick_TheViewportRowOpensTheLevelAndNeverDropsUnsavedWork) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("hierarchy_viewport_row");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity viewport = editor.GetDocumentViewport();
        ASSERT_NE(viewport, Scene::kInvalidEntity);

        // A saved, clean level: the row opens its .uvscene, which reads the file back.
        ASSERT_TRUE(editor.SaveScene());
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_TRUE(std::filesystem::is_regular_file(scratch / "main.uvscene"));
        EXPECT_TRUE(EditorAccess::OpenHierarchyRowTarget(editor, viewport));

        // Reading the file back rebuilds the document, so the handle taken before it is not the
        // one the document has now. Everything from here on addresses the row through the current
        // Viewport rather than the one captured before the reload.
        const Scene::Entity reloadedViewport = editor.GetDocumentViewport();
        ASSERT_NE(reloadedViewport, Scene::kInvalidEntity);

        // With unsaved work the click is still the row's (it never falls through to a rename), but
        // nothing is read back from disk: the object made a moment ago is still there.
        const Scene::Entity unsaved =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D);
        ASSERT_NE(unsaved, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.IsSceneDirty());
        EXPECT_TRUE(EditorAccess::OpenHierarchyRowTarget(editor, reloadedViewport));
        EXPECT_TRUE(entityManager.IsAlive(unsaved)) << "a double-click must not reload over edits";
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, MaterialThumbnailResolvesRelativeContentRootAndRendersRealPbr) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("pbr_thumb_relative_root");
    const std::filesystem::path contentAbs = scratch / "Content";
    std::filesystem::create_directories(contentAbs);

    std::error_code ec;
    const std::filesystem::path relRoot = std::filesystem::relative(contentAbs, std::filesystem::current_path(ec), ec);
    const std::filesystem::path configuredRoot = (!ec && !relRoot.empty()) ? relRoot : contentAbs;

    Core::EngineConfig config = MakeEditorTestConfig();
    config.headless = false;
    config.windowGlVersionMajor = 3;
    config.windowGlVersionMinor = 3;
    config.projectContentRoot = configuredRoot;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    if (!engine.Load() || !engine.GetServices().GetWindowManager().IsValid()) {
        engine.Shutdown();
        std::filesystem::remove_all(scratch);
        GTEST_SKIP() << "No display available for Editor PBR thumbnail verification - skipping";
    }

    Asset::MaterialAsset material{};
    material.albedoColor = Math::Color{0.20F, 0.75F, 0.35F};
    material.metallic = 0.25F;
    material.roughness = 0.30F;
    const std::filesystem::path matFile = configuredRoot / "M_Standard.uvmaterial";
    ASSERT_TRUE(Asset::SaveMaterialAsset(material, matFile));

    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        EditorAccess::RefreshProjectFileIndex(editor);

        // Both a content-relative path and a root-prefixed relative path must produce a real PBR sphere
        // (regression for the double-prefixed 'assets/assets/M_Standard.uvmaterial' bug).
        const std::uintptr_t texRelative = EditorAccess::GetMaterialThumbnail(editor, "M_Standard.uvmaterial");
        ASSERT_NE(texRelative, 0U);
        const std::uintptr_t texPrefixed = EditorAccess::GetMaterialThumbnail(editor, matFile);
        ASSERT_NE(texPrefixed, 0U);

        GLint width = 0;
        GLint height = 0;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texPrefixed));
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        ASSERT_GT(width, 0);
        ASSERT_GT(height, 0);

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0U);

        const std::size_t centerIdx = (static_cast<std::size_t>(height / 2) * static_cast<std::size_t>(width) +
                                       static_cast<std::size_t>(width / 2)) *
                                      4U;
        EXPECT_EQ(pixels[centerIdx + 3U], 255U);
        EXPECT_GT(static_cast<int>(pixels[centerIdx + 1U]), 40);
        EXPECT_EQ(pixels[3U], 0U);

        EditorAccess::InvalidateMaterialThumbnailCache(editor, matFile);
        const std::uintptr_t texAfterEdit = EditorAccess::GetMaterialThumbnail(editor, "M_Standard.uvmaterial");
        EXPECT_NE(texAfterEdit, 0U);

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, ToolWindowsSetFocusPendingOnReopen) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("tool_windows_reopen_focus");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();

        // Entity Class: opening sets focusPending; clearing (first focus) then reopening re-arms focusPending.
        editor.OpenEntityClassPicker(content);
        EXPECT_TRUE(editor.IsEntityClassPickerOpen());
        EXPECT_TRUE(EditorAccess::EntityClassFocusPending(editor));
        EditorAccess::ClearEntityClassFocusPending(editor);
        EXPECT_FALSE(EditorAccess::EntityClassFocusPending(editor));
        editor.OpenEntityClassPicker(content);
        EXPECT_TRUE(EditorAccess::EntityClassFocusPending(editor));

        // Preferences, Project Settings, Input Map, and Keyboard Shortcuts all re-arm focusPending on reopen.
        editor.OpenEditorPreferences();
        EXPECT_TRUE(EditorAccess::PreferencesFocusPending(editor));
        EditorAccess::PreferencesFocusPending(editor) = false;
        editor.OpenEditorPreferences();
        EXPECT_TRUE(EditorAccess::PreferencesFocusPending(editor));

        editor.OpenProjectSettings();
        EXPECT_TRUE(EditorAccess::ProjectSettingsFocusPending(editor));
        EditorAccess::ProjectSettingsFocusPending(editor) = false;
        editor.OpenProjectSettings();
        EXPECT_TRUE(EditorAccess::ProjectSettingsFocusPending(editor));

        editor.OpenInputMap();
        EXPECT_TRUE(EditorAccess::InputMapFocusPending(editor));
        EditorAccess::InputMapFocusPending(editor) = false;
        editor.OpenInputMap();
        EXPECT_TRUE(EditorAccess::InputMapFocusPending(editor));

        editor.OpenKeyboardShortcuts();
        EXPECT_TRUE(EditorAccess::ShortcutsFocusPending(editor));
        EditorAccess::ShortcutsFocusPending(editor) = false;
        editor.OpenKeyboardShortcuts();
        EXPECT_TRUE(EditorAccess::ShortcutsFocusPending(editor));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, OpenRetargetWindowForPathsStrictlyUsesMultiSelectionAndFolderRecursivelyDetectsAllAnimations) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("retarget_multiselect_folder");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content / "Mixamo" / "Combat");
    std::filesystem::create_directories(content / "Other");

    const auto writeClip = [](const std::filesystem::path& path, const char* id) {
        Asset::AnimationClipAsset clip;
        clip.clipId = id;
        clip.durationSeconds = 0.5;
        const Asset::AnimationAssetPose pose{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {{0.0, pose}, {0.5, pose}}}};
        clip.rest = {Asset::AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        EXPECT_TRUE(Asset::SaveAnimationClipAsset(clip, path));
    };
    writeClip(content / "Mixamo" / "Idle.uvanim", "Idle");
    writeClip(content / "Mixamo" / "Walk.uvanim", "Walk");
    writeClip(content / "Mixamo" / "Run.uvanim", "Run");
    writeClip(content / "Mixamo" / "Combat" / "Slash.uvanim", "Slash");
    writeClip(content / "Other" / "Ignored.uvanim", "Ignored");

    Core::EngineConfig config;
    config.mode = Core::EngineMode::Headless;
    config.contentRootPath = content;
    config.cacheRootPath = scratch / "cache";
    config.settingsFilePath = scratch / "project.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        editor.RefreshProjectFileIndex();

        // 1. Multi-select strictly passes ONLY the selected animations (never unselected siblings!).
        editor.OpenRetargetWindowForPaths({std::filesystem::path("Mixamo/Walk.uvanim"),
                                              std::filesystem::path("Mixamo/Combat/Slash.uvanim")});
        ASSERT_TRUE(editor.IsRetargetWindowOpen());
        EditorAccess::TickRetargetWindow(editor);
        EXPECT_TRUE(EditorAccess::RetargetWindowTarget(editor).empty());
        const std::vector<std::filesystem::path> pickedAnims = EditorAccess::RetargetWindowAnimations(editor);
        ASSERT_EQ(pickedAnims.size(), 2U);
        EXPECT_EQ(pickedAnims[0].filename().string(), "Slash.uvanim");
        EXPECT_EQ(pickedAnims[1].filename().string(), "Walk.uvanim");
        editor.CloseRetargetWindow();

        // 2. Folder right-click recursively discovers every animation under Mixamo (and excludes Other).
        editor.OpenRetargetWindowForFolder(std::filesystem::path("Mixamo"));
        ASSERT_TRUE(editor.IsRetargetWindowOpen());
        EditorAccess::TickRetargetWindow(editor);
        EXPECT_TRUE(EditorAccess::RetargetWindowTarget(editor).empty());
        const std::vector<std::filesystem::path> folderAnims = EditorAccess::RetargetWindowAnimations(editor);
        ASSERT_EQ(folderAnims.size(), 4U);
        for (const std::filesystem::path& p : folderAnims) {
            EXPECT_NE(p.filename().string(), "Ignored.uvanim");
        }
        editor.CloseRetargetWindow();

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, SetSelectedMeshSourceAutoAttachesAPoseUvskelSkeletonUnderPlayer3DWithUndoRedo) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("player3d_apose_skel");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content / "Characters");

    // Author a rigged .uvmodel plus a sibling .uvskel with a distinct A-pose rest position.
    Asset::MeshAsset mesh;
    mesh.vertices = {
        Asset::MeshVertex{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        Asset::MeshVertex{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        Asset::MeshVertex{{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds = Math::Aabb{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    mesh.joints = {
        Asset::MeshJoint{Asset::kInvalidJointParent, Math::Matrix4x4::Identity(), "Root"},
        Asset::MeshJoint{0U, Math::Matrix4x4::Identity(), "Hips"},
    };
    mesh.skinningInfluences = {
        Asset::MeshSkinningInfluence{{1U, 0U, 0U, 0U}, {1.0F, 0.0F, 0.0F, 0.0F}},
        Asset::MeshSkinningInfluence{{1U, 0U, 0U, 0U}, {1.0F, 0.0F, 0.0F, 0.0F}},
        Asset::MeshSkinningInfluence{{1U, 0U, 0U, 0U}, {1.0F, 0.0F, 0.0F, 0.0F}},
    };
    const std::filesystem::path modelPath = content / "Characters" / "Hero.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(mesh, modelPath));

    Asset::SkeletonAsset skel;
    skel.skeletonId = "Hero";
    skel.joints = {
        Asset::SkeletonJoint{"Root", -1, {0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
        Asset::SkeletonJoint{"Hips", 0, {0.0F, 0.95F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
    };
    const std::filesystem::path skelPath = content / "Characters" / "Hero.uvskel";
    ASSERT_TRUE(Asset::SaveSkeletonAsset(skel, skelPath));

    Core::EngineConfig config;
    config.mode = Core::EngineMode::Headless;
    config.contentRootPath = content;
    config.cacheRootPath = scratch / "cache";
    config.settingsFilePath = scratch / "project.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        editor.RefreshProjectFileIndex();

        Scene::IEntityManager& em = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sg = engine.GetServices().GetSceneGraph();

        // Build Player3D -> MeshInstance3D (no Skeleton3D yet) and assign Hero.uvmodel.
        const Scene::Entity player = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Player3D);
        ASSERT_NE(player, Scene::kInvalidEntity);
        const Scene::Entity meshEntity =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::MeshInstance3D, player);
        ASSERT_NE(meshEntity, Scene::kInvalidEntity);
        editor.SelectEntity(meshEntity);

        const Asset::AssetGuid guid = engine.GetServices().GetAssetDatabase().Register(modelPath);
        ASSERT_TRUE(editor.SetSelectedMeshSource(guid, std::nullopt));

        // A child Skeleton3D loaded from Hero.uvskel (with the A-pose Hips height 0.95) is auto-created!
        const std::vector<Scene::Entity> children = sg.GetChildren(em, meshEntity);
        ASSERT_EQ(children.size(), 1U);
        ASSERT_TRUE(em.HasComponent<Scene::Skeleton3DComponent>(children.front()));
        const auto& autoSkel = em.GetComponent<Scene::Skeleton3DComponent>(children.front());
        ASSERT_EQ(autoSkel.bones.size(), 2U);
        EXPECT_EQ(autoSkel.bones[1].name, "Hips");
        EXPECT_NEAR(autoSkel.bones[1].position.y, 0.95F, 1e-4F);
        EXPECT_EQ(autoSkel.skeletonAssetPath, "Characters/Hero.uvmodel");
        EXPECT_TRUE(em.HasComponent<Scene::SkinComponent>(meshEntity));

        // Undo removes the auto-created Skeleton3D and Skin; Redo brings both back.
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(sg.GetChildren(em, meshEntity).empty());
        EXPECT_FALSE(em.HasComponent<Scene::SkinComponent>(meshEntity));

        ASSERT_TRUE(editor.Redo());
        const std::vector<Scene::Entity> redoneChildren = sg.GetChildren(em, meshEntity);
        ASSERT_EQ(redoneChildren.size(), 1U);
        ASSERT_TRUE(em.HasComponent<Scene::Skeleton3DComponent>(redoneChildren.front()));
        EXPECT_EQ(em.GetComponent<Scene::Skeleton3DComponent>(redoneChildren.front()).bones.size(), 2U);
        EXPECT_TRUE(em.HasComponent<Scene::SkinComponent>(meshEntity));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch);
}

TEST(EditorTest, ExternalContentWatcher_PollNowDetectsExternalFilesAndIgnoresTempCopies) {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "uve_ext_watch_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    const std::filesystem::path content = scratch / "Content";
    ASSERT_TRUE(std::filesystem::create_directories(content / "Existing"));
    {
        std::ofstream f(content / "Existing" / "Seed.txt", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(f.is_open());
        f << "seed";
    }

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    config.projectChangeWatchPollIntervalSeconds = 3600.0;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        editor.Tick();

        // Quiet poll right after baseline finds no delta.
        EXPECT_FALSE(editor.PollExternalContentChangesNow());

        // Transient .uvcopy-tmp files must stay hidden from both index and watcher.
        {
            std::ofstream tmp(content / "InFlight.fbx.uvcopy-tmp", std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(tmp.is_open());
            tmp << "partial";
        }
        EXPECT_FALSE(editor.PollExternalContentChangesNow());

        // Simulate adding an external folder + file outside the Editor (OS/Android File Manager).
        ASSERT_TRUE(std::filesystem::create_directories(content / "ExternalPack" / "Locomotion"));
        {
            std::ofstream ext(content / "ExternalPack" / "Locomotion" / "Walk.txt", std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(ext.is_open());
            ext << "external-drop";
        }

        EXPECT_TRUE(editor.PollExternalContentChangesNow());
        const Asset::ProjectFileSnapshot refreshed =
            engine.GetServices().GetProjectFileIndex().GetSnapshot();
        const bool foundExternal = std::any_of(
            refreshed.entries.begin(), refreshed.entries.end(),
            [](const Asset::ProjectFileEntry& entry) {
                return entry.relativePath == std::filesystem::path{"ExternalPack/Locomotion/Walk.txt"};
            });
        const bool foundTemp = std::any_of(
            refreshed.entries.begin(), refreshed.entries.end(),
            [](const Asset::ProjectFileEntry& entry) {
                return entry.relativePath.extension() == ".uvcopy-tmp";
            });
        EXPECT_TRUE(foundExternal);
        EXPECT_FALSE(foundTemp);

        // Next poll with no further disk writes reports no delta.
        EXPECT_FALSE(editor.PollExternalContentChangesNow());

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch, ec);
}

TEST(EditorTest, ImportExternalFiles_RecursivelyCopiesFoldersPreservesSubfoldersSkipsHiddenAndSelfCopy) {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "uve_import_folder_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    const std::filesystem::path content = scratch / "Content";
    const std::filesystem::path externalRoot = scratch / "ExternalDownloads" / "MixamoAnims";
    ASSERT_TRUE(std::filesystem::create_directories(content / "Characters"));
    ASSERT_TRUE(std::filesystem::create_directories(externalRoot / "Locomotion"));
    ASSERT_TRUE(std::filesystem::create_directories(externalRoot / "Combat"));
    ASSERT_TRUE(std::filesystem::create_directories(externalRoot / ".hidden_meta"));

    const auto writeText = [](const std::filesystem::path& p, const std::string& body) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << body;
    };
    writeText(externalRoot / "Locomotion" / "Walk.txt", "walk-data");
    writeText(externalRoot / "Combat" / "Slash.txt", "slash-data");
    writeText(externalRoot / ".DS_Store", "macos-junk");
    writeText(externalRoot / ".hidden_meta" / "Secret.txt", "hidden");

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        editor.Tick();

        // Import entire folder preserving its subfolder layout inside Content/Characters.
        ASSERT_TRUE(editor.ImportExternalFiles({externalRoot}, content / "Characters"));
        EXPECT_TRUE(std::filesystem::is_regular_file(content / "Characters" / "MixamoAnims" / "Locomotion" / "Walk.txt"));
        EXPECT_TRUE(std::filesystem::is_regular_file(content / "Characters" / "MixamoAnims" / "Combat" / "Slash.txt"));
        EXPECT_FALSE(std::filesystem::exists(content / "Characters" / "MixamoAnims" / ".DS_Store"));
        EXPECT_FALSE(std::filesystem::exists(content / "Characters" / "MixamoAnims" / ".hidden_meta"));

        // Self-recursive folder copy (destination inside source) must be rejected cleanly.
        EXPECT_FALSE(editor.ImportExternalFiles({content}, content / "Characters" / "Nested"));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch, ec);
}

TEST(EditorTest, BackgroundImportCancel_ClearsPendingQueuesSafely) {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "uve_cancel_import_test";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    const std::filesystem::path content = scratch / "Content";
    ASSERT_TRUE(std::filesystem::create_directories(content));

    Core::EngineConfig config = MakeEditorTestConfig();
    config.projectContentRoot = content;
    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 16U, &engine);
        editor.Init();
        editor.DismissStartupLoading();
        editor.Tick();

        // Cancelling when idle or queued leaves loading state clean and non-busy.
        editor.CancelBackgroundImports();
        const Editor::EditorLoadingState state = editor.GetLoadingState();
        EXPECT_FALSE(state.startup);
        EXPECT_FALSE(state.busy);
        EXPECT_EQ(state.total, 0U);

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(scratch, ec);
}

} // namespace UVE::Editor::Tests
