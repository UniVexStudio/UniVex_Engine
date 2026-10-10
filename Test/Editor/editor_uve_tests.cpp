// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
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
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/object/scene_folder_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"

#include "Support/test_scratch_uve.h"

#include "editor_icon_set_uve.h"

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/objects/3d/health_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/marker_3d_uve.h"
#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"
#include "uve/objects/3d/nav_seeker_3d_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/spawn_point_3d_uve.h"
#include "uve/object/scene_object_registry_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor::Tests {

struct EditorUVEAccessUVE final {
    // The folder new level objects go to (the Outliner allows objects only inside folders).
    [[nodiscard]] static Scene::EntityUVE GetObjectFolderUVE(EditorUVE& editor) { return editor.ResolveObjectFolderUVE(); }
    [[nodiscard]] static Scene::EntityUVE GetViewportUVE(EditorUVE& editor) { return editor.GetDocumentViewportUVE(); }

    [[nodiscard]] static std::string GetOutlinerTypeTagUVE(const EditorUVE& editor,
                                                            const Scene::EntityUVE entity) {
        return editor.GetOutlinerTypeTagUVE(entity);
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> GetHierarchyChildrenInViewOrderUVE(
        const EditorUVE& editor, const Scene::EntityUVE parent) {
        return editor.GetHierarchyChildrenInViewOrderUVE(parent);
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> SortHierarchyRowsUVE(
        const EditorUVE& editor, std::vector<Scene::EntityUVE> entities) {
        return editor.SortHierarchyRowsUVE(std::move(entities));
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> GetDocumentAncestryUVE(const EditorUVE& editor,
                                                                                const Scene::EntityUVE entity) {
        return editor.GetDocumentAncestryUVE(entity);
    }

    [[nodiscard]] static std::vector<Scene::EntityUVE> GetEligibleReparentParentsUVE(EditorUVE& editor,
                                                                                        const Scene::EntityUVE entity) {
        return editor.GetEligibleReparentParentsUVE(entity);
    }

    [[nodiscard]] static bool RequestHierarchyReparentUVE(EditorUVE& editor, const Scene::EntityUVE entity,
                                                           const Scene::EntityUVE newParent) {
        return editor.RequestHierarchyReparentUVE(entity, newParent);
    }
    [[nodiscard]] static bool HasPendingHierarchyReparentUVE(const EditorUVE& editor) {
        return editor.m_pendingHierarchyReparent.has_value();
    }
    [[nodiscard]] static std::size_t GetPendingHierarchyReparentCountUVE(const EditorUVE& editor) {
        return editor.m_pendingHierarchyReparent.has_value()
                   ? editor.m_pendingHierarchyReparent->subtreeEntityCount
                   : 0U;
    }
    [[nodiscard]] static bool ConfirmHierarchyReparentUVE(EditorUVE& editor) {
        return editor.ConfirmHierarchyReparentUVE();
    }
    static void CancelHierarchyReparentUVE(EditorUVE& editor) { editor.CancelHierarchyReparentUVE(); }
    [[nodiscard]] static bool RequestHierarchyDeleteUVE(EditorUVE& editor) {
        return editor.RequestHierarchyDeleteUVE();
    }
    [[nodiscard]] static bool HasPendingHierarchyDeleteUVE(const EditorUVE& editor) {
        return editor.m_pendingHierarchyDelete.has_value();
    }
    [[nodiscard]] static std::size_t GetPendingHierarchyDeleteCountUVE(const EditorUVE& editor) {
        return editor.m_pendingHierarchyDelete.has_value() ? editor.m_pendingHierarchyDelete->subtreeEntityCount
                                                           : 0U;
    }
    [[nodiscard]] static bool ConfirmHierarchyDeleteUVE(EditorUVE& editor) {
        return editor.ConfirmHierarchyDeleteUVE();
    }
    static void CancelHierarchyDeleteUVE(EditorUVE& editor) { editor.CancelHierarchyDeleteUVE(); }

    static void SelectHierarchyRangeUVE(EditorUVE& editor, const Scene::EntityUVE entity,
                                        const std::vector<Scene::EntityUVE>& visibleOrder, const bool addToSelection) {
        editor.SelectHierarchyRangeUVE(entity, visibleOrder, addToSelection);
    }
    static void ApplyHierarchyBoxSelectionUVE(
        EditorUVE& editor, const std::vector<Scene::EntityUVE>& entities,
        const std::vector<std::array<float, 4U>>& bounds, const float startX, const float startY,
        const float endX, const float endY, const bool additive,
        const std::vector<Scene::EntityUVE>& selectionBefore, const Scene::EntityUVE activeBefore) {
        std::vector<EditorUVE::HierarchySelectionRowBoundsUVE> rowBounds;
        rowBounds.reserve(entities.size());
        for (std::size_t index = 0U; index < entities.size() && index < bounds.size(); ++index) {
            rowBounds.push_back(EditorUVE::HierarchySelectionRowBoundsUVE{
                entities[index], bounds[index][0U], bounds[index][1U], bounds[index][2U], bounds[index][3U]});
        }
        editor.ApplyHierarchyBoxSelectionUVE(rowBounds, startX, startY, endX, endY, additive,
                                             selectionBefore, activeBefore);
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

    [[nodiscard]] static bool IsRetargetPreviewActiveUVE(const EditorUVE& editor) {
        return editor.m_retargetPreview.has_value();
    }
    static void RebuildRetargetPreviewUVE(EditorUVE& editor, const RetargetPlanUVE& plan, const std::filesystem::path& model) {
        editor.RebuildRetargetPreviewUVE(plan, model);
    }
    [[nodiscard]] static Scene::EntityUVE GetRetargetSourceSkeletonUVE(const EditorUVE& editor) {
        return editor.m_retargetPreview.has_value() ? editor.m_retargetPreview->sourceSkeleton : Scene::kInvalidEntityUVE;
    }
    [[nodiscard]] static std::size_t GetRetargetSourceColourCountUVE(const EditorUVE& editor) {
        return editor.m_retargetPreview.has_value() ? editor.m_retargetPreview->sourceColours.size() : 0U;
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
    [[nodiscard]] static bool ResetSelectedComponentPropertyUVE(EditorUVE& editor,
                                                                const Core::TypeMetadataEntryUVE& entry,
                                                                const Core::TypeMetadataPropertyUVE& property) {
        return editor.ResetSelectedComponentPropertyUVE(entry, property);
    }
    [[nodiscard]] static bool IsComponentPropertyMixedUVE(EditorUVE& editor, const Core::TypeMetadataEntryUVE& entry,
                                                          const Core::TypeMetadataPropertyUVE& property) {
        return editor.IsComponentPropertyMixedUVE(entry, property);
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
    [[nodiscard]] static std::vector<std::string> GetInspectorGroupHeadersUVE(const EditorUVE& editor,
                                                                             const Scene::EntityUVE entity) {
        return editor.m_inspectorDrawerRegistry.GetEligibleGroupHeadersUVE(entity);
    }
    [[nodiscard]] static std::vector<std::string> GetEligibleInspectorDrawerIdsUVE(const EditorUVE& editor,
                                                                                  const Scene::EntityUVE entity) {
        return editor.m_inspectorDrawerRegistry.GetEligibleDrawerIdsUVE(entity);
    }
    [[nodiscard]] static std::size_t GetInspectorDrawerCountUVE(const EditorUVE& editor) {
        return editor.m_inspectorDrawerRegistry.GetDrawerCountUVE();
    }

    [[nodiscard]] static std::vector<std::string> GetEveryContentBrowserTypeLabelUVE() {
        using Type = EditorUVE::ContentBrowserItemTypeUVE;
        std::vector<std::string> labels;
        for (const Type type : {Type::Folder, Type::Scene, Type::Prefab, Type::Entity, Type::Bundle, Type::Mesh, Type::Model,
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
    [[nodiscard]] static bool CreateUVScriptForEntityUVE(EditorUVE& editor, const Scene::EntityUVE entity,
                                                          const std::string_view fileName = {}) {
        return editor.CreateUVScriptForEntityUVE(entity, fileName);
    }
    [[nodiscard]] static bool OpenUVScriptCreationDialogUVE(EditorUVE& editor, const Scene::EntityUVE entity) {
        return editor.OpenUVScriptCreationDialogUVE(entity);
    }
    [[nodiscard]] static bool ChooseUVScriptCreationTargetUVE(EditorUVE& editor, const Scene::EntityUVE entity) {
        return editor.ChooseUVScriptCreationTargetUVE(entity);
    }
    static void SetUVScriptCreationFileNameUVE(EditorUVE& editor, std::string fileName) {
        editor.SetUVScriptCreationFileNameUVE(std::move(fileName));
    }
    [[nodiscard]] static bool ConfirmUVScriptCreationDialogUVE(EditorUVE& editor) {
        return editor.ConfirmUVScriptCreationDialogUVE();
    }
    static void CancelUVScriptCreationDialogUVE(EditorUVE& editor) {
        editor.CancelUVScriptCreationDialogUVE();
    }
    [[nodiscard]] static bool AssignScriptToEntityUVE(EditorUVE& editor, const Scene::EntityUVE entity,
                                                       const std::string& path) {
        return editor.AssignScriptToEntityUVE(entity, path);
    }
    [[nodiscard]] static bool OpenHierarchyScriptAttachDialogUVE(EditorUVE& editor,
                                                                  const Scene::EntityUVE entity) {
        return editor.OpenHierarchyScriptAttachDialogUVE(entity);
    }
    static void SetHierarchyScriptAttachPathUVE(EditorUVE& editor, std::string path) {
        editor.SetHierarchyScriptAttachPathUVE(std::move(path));
    }
    [[nodiscard]] static bool ConfirmHierarchyScriptAttachUVE(EditorUVE& editor) {
        return editor.ConfirmHierarchyScriptAttachUVE();
    }
    static void CancelHierarchyScriptAttachUVE(EditorUVE& editor) {
        editor.CancelHierarchyScriptAttachUVE();
    }
    [[nodiscard]] static Scene::EntityUVE GetHierarchyScriptAttachTargetUVE(const EditorUVE& editor) {
        return editor.m_hierarchyScriptAttachTarget;
    }
    [[nodiscard]] static const std::string& GetHierarchyScriptAttachPathUVE(const EditorUVE& editor) {
        return editor.m_hierarchyScriptAttachPath;
    }
    [[nodiscard]] static Scene::EntityUVE GetUVScriptCreationTargetUVE(const EditorUVE& editor) {
        return editor.m_uvScriptCreationTarget;
    }
    [[nodiscard]] static const std::string& GetUVScriptCreationFileNameUVE(const EditorUVE& editor) {
        return editor.m_uvScriptCreationFileName;
    }
    [[nodiscard]] static const std::string& GetUVScriptCreationPathUVE(const EditorUVE& editor) {
        return editor.m_uvScriptCreationPath;
    }
    [[nodiscard]] static bool IsUVScriptCreationFileNameEditedUVE(const EditorUVE& editor) {
        return editor.m_uvScriptCreationFileNameEdited;
    }
    [[nodiscard]] static std::vector<Scene::EntityUVE> GetUVScriptTargetCandidatesUVE(const EditorUVE& editor) {
        return editor.GetUVScriptTargetCandidatesUVE();
    }
    static void BeginHierarchyRenameUVE(EditorUVE& editor, const Scene::EntityUVE entity) {
        editor.BeginHierarchyRenameUVE(entity);
    }
    static void CancelHierarchyRenameUVE(EditorUVE& editor) { editor.CancelHierarchyRenameUVE(); }
    [[nodiscard]] static bool SetHierarchyEntityNameUVE(EditorUVE& editor, const Scene::EntityUVE entity,
                                                         std::string name) {
        return editor.SetHierarchyEntityNameUVE(entity, std::move(name));
    }
    [[nodiscard]] static Scene::EntityUVE GetHierarchyRenameEntityUVE(const EditorUVE& editor) {
        return editor.m_hierarchyRenameEntity;
    }
    [[nodiscard]] static bool IsHierarchyRenameUsingDialogUVE(const EditorUVE& editor) {
        return editor.m_hierarchyRenameUsesDialogUVE;
    }
    [[nodiscard]] static bool IsHierarchyRenameDialogOpenRequestedUVE(const EditorUVE& editor) {
        return editor.m_hierarchyRenameDialogOpenRequested;
    }
    static void SetHierarchyFilterUVE(EditorUVE& editor, std::string filter) {
        editor.m_hierarchyFilter = std::move(filter);
        editor.InvalidateHierarchyFilterCacheUVE();
    }
    static void RebuildHierarchyFilterCacheUVE(EditorUVE& editor) { editor.RebuildHierarchyFilterCacheUVE(); }
    [[nodiscard]] static const std::vector<Scene::EntityUVE>& GetCachedHierarchyVisibleEntitiesUVE(
        const EditorUVE& editor) {
        return editor.m_cachedHierarchyVisibleEntities;
    }
    [[nodiscard]] static bool IsHierarchyEntityVisibleUVE(const EditorUVE& editor, const Scene::EntityUVE entity) {
        return editor.IsHierarchyEntityVisibleUVE(entity);
    }
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
    [[nodiscard]] static ContentShelvesUVE& GetContentShelvesUVE(EditorUVE& editor) { return editor.m_contentShelves; }
    [[nodiscard]] static std::filesystem::path GetSharedShelvesPathUVE(const EditorUVE& editor) {
        return editor.GetSharedShelvesPathUVE();
    }
    static bool SaveSharedShelvesUVE(EditorUVE& editor) { return editor.SaveSharedShelvesUVE(); }
    static void ReloadSharedShelvesIfChangedUVE(EditorUVE& editor) { editor.ReloadSharedShelvesIfChangedUVE(); }
    [[nodiscard]] static const std::string& GetContentStatusMessageUVE(const EditorUVE& editor) {
        return editor.m_contentStatusMessage;
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

    [[nodiscard]] static bool IsLibraryWorkspaceActiveUVE(const EditorUVE& editor) noexcept {
        return editor.m_activeWorkspace == EditorUVE::EditorWorkspaceUVE::Library;
    }
    static void ActivateGameWorkspaceUVE(EditorUVE& editor) noexcept {
        editor.m_activeWorkspace = EditorUVE::EditorWorkspaceUVE::Game;
    }
    [[nodiscard]] static bool IsScriptingWorkspaceActiveUVE(const EditorUVE& editor) noexcept {
        return editor.m_activeWorkspace == EditorUVE::EditorWorkspaceUVE::Scripting;
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

// Level objects live inside a folder; puts loose test entities into the level's object folder.
Scene::EntityUVE PutInObjectFolderUVE(Core::EngineCoreUVE& engine, EditorUVE& editor,
                                    std::initializer_list<Scene::EntityUVE> entities) {
    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    const Scene::EntityUVE folder = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
    for (const Scene::EntityUVE entity : entities) {
        services.GetSceneGraphUVE().SetParentUVE(services.GetEntityManagerUVE(), entity, folder);
    }
    return folder;
}

struct UnregisteredEditorLifecycleComponentUVE final {
    int value = 0;
};

TEST(EditorUVETest, InitUVE_StartsRunningWithEmptyDocumentRootsAndSupportsHeadlessLifecycle) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lifecycle.uvscene");
        editor.InitUVE();

        EXPECT_EQ(editor.GetStateUVE(), EditorStateUVE::Running);
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());

        editor.ShutdownUVE();
        EXPECT_EQ(editor.GetStateUVE(), EditorStateUVE::Shutdown);
    }

    engine.Shutdown();
}

TEST(EditorUVETest, OpenScriptGraphForEntity_NoScriptComponent_ReturnsFalse) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_open_script_graph_none.uvscene");
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

TEST(EditorUVETest, InitUVE_DoesNotCreateAutomaticPreviewLighting) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_no_preview_light.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_2d_canvas.uvscene");
        editor.InitUVE();
        const Editor2DCanvasStateUVE initial = editor.Get2DCanvasStateUVE();
        EXPECT_FLOAT_EQ(initial.zoom, 0.36F);
        EXPECT_FLOAT_EQ(initial.pan.x, 0.0F);
        EXPECT_FLOAT_EQ(initial.pan.y, 0.0F);
        EXPECT_TRUE(initial.gridVisible);
        EXPECT_TRUE(initial.safeAreaVisible);
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_layout.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_drawer_registration.uvscene");
        // 27, not the original 22: every one of the twenty-two ids below still registers, and
        // five sections were added - Visibility, which never had one, plus the four the common
        // Object section gained (Process, Thread Group, Auto Translate, Metadata). None of those
        // five needed a line of Inspector code: the drawers are generated from what each
        // component declares (RegisterMetadataInspectorDrawersUVE), so this count follows the
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
        // engine additions bring the metadata-driven drawer total to 57.
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorDrawerCountUVE(editor), 57U);
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "directional-light-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "animation-mixer"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "solid-body"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "animation-tree"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "bone-attachment-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "two-bone-ik-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "nav-mesh-volume-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "nav-seeker-3d"));
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
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "kinematic-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "spring-arm"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "spawn-point"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "ray-cast-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "projectile-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "hitbox-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "hurtbox-3d"));
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "lod-group-3d"));
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
        EXPECT_TRUE(EditorUVEAccessUVE::HasInspectorDrawerUVE(editor, "object-metadata"));
        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, WorldEnvironmentComponentUVE_AttachEditUndoRedoThroughEditorPath) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_world_environment.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(entity, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity));

        Scene::WorldEnvironment3DComponentUVE environment;
        environment.skyAssetPath = "environments/sunset.uvsky";
        environment.ambientColor = Math::Vector3UVE{0.15F, 0.25F, 0.40F};
        environment.ambientEnergy = 1.75F;
        environment.exposure = 1.25F;
        environment.fogColor = Math::Vector3UVE{0.30F, 0.35F, 0.45F};
        environment.fogDensity = 0.02F;
        environment.fogEnabled = true;
        environment.postProcessingEnabled = true;
        ASSERT_TRUE(Scene::IsWorldEnvironment3DObjectComponentValidUVE(environment));
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::WorldEnvironment, environment));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity).skyAssetPath,
                  environment.skyAssetPath);
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity).ambientEnergy,
                        environment.ambientEnergy);
        EXPECT_TRUE(editor.CanUndoUVE());

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity));
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity).fogEnabled,
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outliner_tags.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outliner_hierarchy_context.uvscene");
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
        const Scene::EntityUVE folder = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        for (const Scene::EntityUVE top : {rootA, parent, rootB}) {
            services.GetSceneGraphUVE().SetParentUVE(entityManager, top, folder);
        }

        EXPECT_EQ(EditorUVEAccessUVE::GetDocumentAncestryUVE(editor, selected),
                  (std::vector<Scene::EntityUVE>{editor.GetDocumentObjectUVE(),
                                                 EditorUVEAccessUVE::GetViewportUVE(editor),
                                                 folder, parent, selected}));
        // An object may only move inside a folder: the folder leads, then its objects in order. The
        // Object and the Viewport are not offered.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleReparentParentsUVE(editor, selected),
                  (std::vector<Scene::EntityUVE>{folder, rootA, parent, rootB}));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ContentBrowserWorkflowUVE_UsesPrimaryExtensionTagAndIndependentRegisteredFocus) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_tags.uvscene");
        Asset::ProjectFileEntryUVE registeredMesh;
        registeredMesh.relativePath = "Characters/Hero.UVMODEL";
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
    EXPECT_EQ(labelOf("Scripts/player.uvs"), "Script");
    EXPECT_EQ(labelOf("Audio/step.uvaudio"), "Audio");
    EXPECT_EQ(labelOf("Audio/Step.WAV"), "Audio");
    EXPECT_EQ(labelOf("Fonts/title.ttf"), "Font");
    EXPECT_EQ(labelOf("Fonts/body.otf"), "Font");
    EXPECT_EQ(labelOf("Content/Hero.uventity"), "Entity");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_navigation.uvscene");
        Asset::ProjectFileSnapshotUVE snapshot;
        snapshot.entries.push_back(
            Asset::ProjectFileEntryUVE{std::filesystem::path{"Scenes"}, Asset::ProjectFileEntryKindUVE::Directory, std::nullopt});
        snapshot.entries.push_back(Asset::ProjectFileEntryUVE{std::filesystem::path{"Scenes/City.uvscene"},
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_content_browser_auto_refresh.uvscene");
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
        std::ofstream corrupt(root / "corrupt.uvtex", std::ios::binary | std::ios::trunc);
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
        unsupported.format = Asset::TextureAssetFormatUVE::RGBA16Float;
        unsupported.pixels.resize(Asset::BytesPerPixelUVE(Asset::TextureAssetFormatUVE::RGBA16Float));
        ASSERT_TRUE(Asset::SaveTextureAssetUVE(unsupported, root / "unsupported.uvtex"));
    }

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_texture_thumbnail.uvscene");
        editor.InitUVE();
        engine.TickFrameUVE();
        editor.TickUVE();

        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "missing.uvtex"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "corrupt.uvtex"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "unsupported.uvtex"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailCacheSizeUVE(editor), 3U);
        EXPECT_EQ(EditorUVEAccessUVE::GetTextureThumbnailUVE(editor, "missing.uvtex"), 0U);
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
        std::ofstream corrupt(root / "corrupt.uvmodel", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(corrupt.is_open());
        corrupt << "not-a-uve-envelope";
    }
    {
        // A structurally valid mesh envelope with no geometry: LoadMeshAssetUVE succeeds, but
        // RenderThumbnailUVE has nothing to draw and must decline rather than issuing an empty
        // draw call.
        const Asset::MeshAssetUVE empty;
        ASSERT_TRUE(Asset::SaveMeshAssetUVE(empty, root / "empty.uvmodel"));
    }

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = root;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_mesh_thumbnail.uvscene");
        editor.InitUVE();
        engine.TickFrameUVE();
        editor.TickUVE();

        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "missing.uvmodel"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "corrupt.uvmodel"), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "empty.uvmodel"), 0U);
        // All three attempts are cached (as failures) rather than retried every call.
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailCacheSizeUVE(editor), 3U);
        EXPECT_EQ(EditorUVEAccessUVE::GetMeshThumbnailUVE(editor, "missing.uvmodel"), 0U);
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

        EditorUVE editor(services, "uve_editor_tests_session_settings.uvscene");
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

TEST(EditorUVETest, SessionSettingsUVE_ReadsValuesSavedUnderTheOldNodeKeys) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
    namespace Id = EditorSettingIdUVE;
    const auto placement = [](const EditorNewObjectPlacementUVE value) {
        return Config::SettingValueUVE{static_cast<std::int64_t>(value)};
    };

    // A settings file written before the object rename carries only the old editor.nodes.* keys.
    settings.SetBoolUVE("editor.nodes.addUnderSelection", false);
    settings.SetIntUVE("editor.nodes.placement", static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ViewFocus));
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_renamed_settings.uvscene");
        editor.InitUVE();
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE), Config::SettingValueUVE{false});
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kNewObjectPlacementUVE), placement(EditorNewObjectPlacementUVE::ViewFocus));
        // The registry migrates to the new ids and removes the old aliases on load.
        EXPECT_EQ(settings.GetBoolUVE("editor.objects.addUnderSelection", true), false);
        EXPECT_EQ(settings.GetIntUVE("editor.objects.placement", -1),
                  static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ViewFocus));
        EXPECT_FALSE(settings.HasKeyUVE("editor.nodes.addUnderSelection"));
        EXPECT_FALSE(settings.HasKeyUVE("editor.nodes.placement"));
        editor.ShutdownUVE();
    }

    // Next file: nothing stored under the new names, so the old ones decide again.
    ASSERT_TRUE(settings.RemoveKeyUVE("editor.objects.addUnderSelection"));
    ASSERT_TRUE(settings.RemoveKeyUVE("editor.objects.placement"));
    // An old value the setting's own rules refuse is not applied: a stale number never reaches the
    // editor, and neither does a value of the wrong type. Each setting keeps its default.
    settings.SetIntUVE("editor.nodes.placement", 99);
    settings.SetStringUVE("editor.nodes.addUnderSelection", "yes");
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_renamed_settings_stale.uvscene");
        editor.InitUVE();
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE), Config::SettingValueUVE{true});
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kNewObjectPlacementUVE), placement(EditorNewObjectPlacementUVE::ParentOrigin));
        editor.ShutdownUVE();
    }

    // A file that speaks both names keeps the new one: the rename is a fallback, never an override.
    settings.SetIntUVE("editor.nodes.placement", static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ViewFocus));
    settings.SetIntUVE(Id::kNewObjectPlacementUVE, static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ParentOrigin));
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_renamed_settings_both.uvscene");
        editor.InitUVE();
        EXPECT_EQ(editor.GetEditorSettingUVE(Id::kNewObjectPlacementUVE), placement(EditorNewObjectPlacementUVE::ParentOrigin));
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_setting_defaults.uvscene");
        const Config::SettingsRegistryUVE& registry = editor.GetSettingsRegistryUVE();
        // Bound settings, shelf schema, axis colours, rename aliases, one default-extras list per
        // library-creatable object kind, and two shortcuts per command.
        std::size_t creatableKinds = 0U;
        for (const Scene::Objects::SceneObjectDescriptorUVE& kind : Scene::Objects::GetSceneObjectDescriptorsUVE()) {
            creatableKinds += kind.libraryCreatable ? 1U : 0U;
        }
        ASSERT_EQ(registry.GetCountUVE(),
                  65U + 3U + std::size(kRenamedSettingIdsUVE) + 1U +
                      (2U * ContentShelvesUVE::kMaxShelvesUVE) + creatableKinds +
                      (2U * editor.GetEditorCommandsUVE().size()));
        for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
            // A renamed setting's old name is only a migration alias; the registry moves its value
            // to the replacement before normal settings are applied.
            if (descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) {
                continue;
            }
            // Axis palette defaults belong to the host and are intentionally unset until it seeds
            // all three colours together; these hidden descriptors are tested by the palette tests.
            if (descriptor->id == EditorSettingIdUVE::kViewportAxisColorXUVE ||
                descriptor->id == EditorSettingIdUVE::kViewportAxisColorYUVE ||
                descriptor->id == EditorSettingIdUVE::kViewportAxisColorZUVE) {
                continue;
            }
            const std::optional<Config::SettingValueUVE> value = editor.GetEditorSettingUVE(descriptor->id);
            if (!value.has_value()) {
                // Hidden schema-only session values (personal shelf names/items) have an owning
                // subsystem but are intentionally not individual Editor setting bindings.
                EXPECT_TRUE(descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE)) << descriptor->id;
                continue;
            }
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_setting_set.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_project_settings.uvscene");
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

TEST(EditorUVETest, NewObjectDefaultsUVE_ParentAndPlacementFollowThePreferences) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_new_object_defaults.uvscene");
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
        const Scene::EntityUVE parent = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        const Scene::EntityUVE root = parentOf(parent);
        const Scene::EntityUVE child = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        EXPECT_EQ(parentOf(child), parent);
        EXPECT_FLOAT_EQ(localPositionOf(child).x, 0.0F);

        // Not under the selection: straight under the Object.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE, false));
        editor.SelectEntityUVE(parent);
        const Scene::EntityUVE sibling = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        EXPECT_EQ(parentOf(sibling), root);

        // At the view focus: under the root, that point itself...
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectPlacementUVE,
                                               static_cast<std::int64_t>(EditorNewObjectPlacementUVE::ViewFocus)));
        const Scene::EntityUVE focused = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        EXPECT_FLOAT_EQ(localPositionOf(focused).x, 3.0F);
        EXPECT_FLOAT_EQ(localPositionOf(focused).z, -2.0F);

        // ...and under a moved parent, the same point taken into the parent's space.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE, true));
        Scene::TransformComponentUVE moved = entities.GetComponentUVE<Scene::TransformComponentUVE>(parent);
        moved.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        graph.SetLocalTransformUVE(entities, parent, moved);
        graph.UpdateUVE(entities);
        editor.SelectEntityUVE(parent);
        const Scene::EntityUVE nested = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
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

TEST(EditorUVETest, NewObjectDefaultsUVE_GroundPlanePlacementAimsThenFallsBackToFocus) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_ground_placement.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entities = engine.GetServicesUVE().GetEntityManagerUVE();
        const auto localPositionOf = [&entities](const Scene::EntityUVE entity) {
            return entities.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition;
        };
        namespace Id = EditorSettingIdUVE;

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectPlacementUVE,
                                               static_cast<std::int64_t>(EditorNewObjectPlacementUVE::GroundPlane)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE, false));

        // No aim yet: the view focus stands in, so the first object still lands in view.
        editor.SetViewportCameraFocusUVE(Math::Vector3UVE{3.0F, 0.0F, -2.0F});
        const Scene::EntityUVE fallback =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(fallback, Scene::kInvalidEntityUVE);
        EXPECT_FLOAT_EQ(localPositionOf(fallback).x, 3.0F);
        EXPECT_FLOAT_EQ(localPositionOf(fallback).z, -2.0F);

        // Aimed: the ground point wins.
        editor.SetViewportCursorGroundPointUVE(Math::Vector3UVE{5.0F, 0.0F, 7.0F});
        const Scene::EntityUVE aimed =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(aimed, Scene::kInvalidEntityUVE);
        EXPECT_FLOAT_EQ(localPositionOf(aimed).x, 5.0F);
        EXPECT_FLOAT_EQ(localPositionOf(aimed).y, 0.0F);
        EXPECT_FLOAT_EQ(localPositionOf(aimed).z, 7.0F);

        // A non-finite aim is ignored: the last good one still places.
        editor.SetViewportCursorGroundPointUVE(
            Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
        const Scene::EntityUVE kept =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(kept, Scene::kInvalidEntityUVE);
        EXPECT_FLOAT_EQ(localPositionOf(kept).x, 5.0F);
        EXPECT_FLOAT_EQ(localPositionOf(kept).z, 7.0F);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, PlayModePreferencesUVE_PauseSaveAndStayInTheTab) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_play_prefs.uvscene";
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
        ASSERT_NE(editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D), Scene::kInvalidEntityUVE);
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

TEST(EditorUVETest, PlayModePreferencesUVE_PauseOnErrorPausesPlayingOnTheFirstError) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_pause_on_error.uvscene", 100U, &engine);
        editor.InitUVE();
        namespace Id = EditorSettingIdUVE;

        // Off by default: errors never pause.
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        UVE_ERROR("pause-on-error test: ignored while the setting is off");
        editor.TickUVE();
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);

        // On: the first error pauses, on the frame it arrives.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kPlayPauseOnErrorUVE, true));
        UVE_ERROR("pause-on-error test: pauses while playing");
        editor.TickUVE();
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Paused);

        // Resume and stop behave as before.
        ASSERT_TRUE(editor.ResumePlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        ASSERT_TRUE(editor.StopPlayModeUVE());

        // Errors logged outside play never pause a later session: the baseline is taken at entry.
        UVE_ERROR("pause-on-error test: logged while editing");
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        editor.TickUVE();
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Playing);
        ASSERT_TRUE(editor.StopPlayModeUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ObjectTypesUVE_EveryObjectIsTypedAndKeepsItThroughDuplicateUndoAndSave) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = "uve_editor_tests_object_types.uvscene";
    std::filesystem::remove(scenePath);
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        EXPECT_EQ(editor.GetObjectTypeNameUVE(root), "Object");

        // Every object the Add Object list makes reads back as the type it was made as, including the
        // ones whose components alone could not say (Static3D is built like a Collider3D).
        for (const Scene::Objects::SceneObjectDescriptorUVE& descriptor : Scene::Objects::GetSceneObjectDescriptorsUVE()) {
            if (!descriptor.libraryCreatable) {
                continue;
            }
            editor.SelectEntityUVE(root);
            const Scene::EntityUVE object = editor.CreateDocumentSceneObjectUVE(descriptor.kind);
            ASSERT_NE(object, Scene::kInvalidEntityUVE) << descriptor.displayName;
            EXPECT_EQ(editor.GetObjectTypeNameUVE(object), descriptor.displayName);
        }

        // The legacy creation commands type their objects as the same kinds.
        editor.SelectEntityUVE(root);
        EXPECT_EQ(editor.GetObjectTypeNameUVE(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Cube)), "BoxMesh3D");

        // Duplicate, and undo then redo of a creation, carry the type with the object.
        editor.SelectEntityUVE(root);
        const Scene::EntityUVE body = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Static3D);
        ASSERT_NE(body, Scene::kInvalidEntityUVE);
        editor.SelectEntityUVE(body);
        const Scene::EntityUVE copy = editor.DuplicateSelectedEntityUVE();
        EXPECT_EQ(editor.GetObjectTypeNameUVE(copy), "Static3D");
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(editor.GetObjectTypeNameUVE(editor.GetSelectedEntityUVE()), "Static3D");

        ASSERT_TRUE(editor.SaveSceneUVE());
        ASSERT_TRUE(editor.LoadSceneUVE());
        std::size_t staticBodies = 0U;
        entityManager.ForEachUVE<Scene::NameComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NameComponentUVE& name) {
                if (name.name.starts_with("Static3D")) {
                    ++staticBodies;
                    EXPECT_EQ(editor.GetObjectTypeNameUVE(entity), "Static3D");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_sibling_order.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
        const Scene::EntityUVE root = EditorUVEAccessUVE::GetObjectFolderUVE(editor); // the level's only folder
        std::vector<Scene::EntityUVE> objects;
        for (int i = 0; i < 3; ++i) {
            editor.SelectEntityUVE(root);
            objects.push_back(editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D));
            ASSERT_NE(objects.back(), Scene::kInvalidEntityUVE);
        }
        const auto order = [&] { return sceneGraph.GetChildrenUVE(entityManager, root); };
        ASSERT_EQ(order(), objects);
        using Move = EditorSiblingMoveUVE;

        // At the ends, the moves toward that end are unavailable; the root never moves.
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(objects[0], Move::Up));
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(objects[2], Move::ToBottom));
        EXPECT_FALSE(editor.CanMoveDocumentEntityUVE(root, Move::Down));

        ASSERT_TRUE(editor.MoveDocumentEntityUVE(objects[2], Move::Up));
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.MoveDocumentEntityUVE(objects[0], Move::ToBottom));
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{objects[2], objects[1], objects[0]}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(order(), objects);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(order(), (std::vector<Scene::EntityUVE>{objects[0], objects[2], objects[1]}));
        ASSERT_TRUE(editor.UndoUVE());

        // A copy lands right below its source, and comes back there on redo.
        editor.SelectEntityUVE(objects[0]);
        const Scene::EntityUVE copy = editor.DuplicateSelectedEntityUVE();
        ASSERT_NE(copy, Scene::kInvalidEntityUVE);
        EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, copy), std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(sceneGraph.GetSiblingIndexUVE(entityManager, editor.GetSelectedEntityUVE()),
                  std::optional<std::size_t>{1U});
        ASSERT_TRUE(editor.UndoUVE());
        ASSERT_EQ(order(), objects);

        // A deleted object comes back in its place, not at the end.
        editor.SelectEntityUVE(objects[1]);
        ASSERT_TRUE(editor.DeleteSelectedEntityUVE());
        ASSERT_TRUE(editor.UndoUVE());
        const std::vector<Scene::EntityUVE> after = order();
        ASSERT_EQ(after.size(), 3U);
        EXPECT_EQ(after[0], objects[0]);
        EXPECT_EQ(after[2], objects[2]);
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
    const std::string_view scenePath = "uve_editor_tests_hierarchy_prefs.uvscene";
    const auto expectChosen = [](const HierarchyViewSettingsUVE& view) {
        EXPECT_FALSE(view.revealSelection);
        EXPECT_TRUE(view.colorCodeIcons);
        EXPECT_FALSE(view.showComponentBadges);
        EXPECT_FALSE(view.showTypeName);
        EXPECT_EQ(view.visibilityColumn, HierarchyVisibilityColumnUVE::OnHover);
        EXPECT_EQ(view.doubleClick, HierarchyDoubleClickUVE::FocusInViewport);
        EXPECT_EQ(view.renameMode, HierarchyRenameModeUVE::Dialog);
        EXPECT_EQ(view.filterMode, HierarchyFilterModeUVE::NameTypeAndComponents);
        EXPECT_EQ(view.sortMode, HierarchySortModeUVE::ByType);
        EXPECT_TRUE(view.filterCaseSensitive);
        EXPECT_FALSE(view.filterKeepAncestors);
        EXPECT_EQ(view.treeLines, HierarchyTreeLinesUVE::ToEachChild);
        EXPECT_FLOAT_EQ(view.rowHeight, 30.0F);
        EXPECT_FLOAT_EQ(view.indentWidth, 30.0F);
        EXPECT_TRUE(view.showIcons);
        EXPECT_TRUE(view.dragToReparent);
        EXPECT_FALSE(view.confirmLargeSubtreeReparent);
        EXPECT_FALSE(view.confirmDeleteSubtree);
    };
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        EXPECT_FLOAT_EQ(editor.GetHierarchyViewSettingsUVE().rowHeight, kMinimumHierarchyRowHeightUVE);
        EXPECT_EQ(editor.GetHierarchyViewSettingsUVE().renameMode, HierarchyRenameModeUVE::Inline);
        EXPECT_EQ(editor.GetHierarchyViewSettingsUVE().filterMode, HierarchyFilterModeUVE::NameOnly);
        EXPECT_EQ(editor.GetHierarchyViewSettingsUVE().sortMode, HierarchySortModeUVE::SceneOrder);
        EXPECT_FALSE(editor.GetHierarchyViewSettingsUVE().filterCaseSensitive);
        EXPECT_TRUE(editor.GetHierarchyViewSettingsUVE().filterKeepAncestors);
        EXPECT_FALSE(editor.GetHierarchyViewSettingsUVE().colorCodeIcons);
        EXPECT_TRUE(editor.GetHierarchyViewSettingsUVE().showComponentBadges);
        EXPECT_TRUE(editor.GetHierarchyViewSettingsUVE().confirmLargeSubtreeReparent);
        EXPECT_TRUE(editor.GetHierarchyViewSettingsUVE().confirmDeleteSubtree);
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyRevealSelectionUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyColorCodeIconsUVE, true));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyShowComponentBadgesUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyShowTypeNameUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyVisibilityColumnUVE,
                                               static_cast<std::int64_t>(HierarchyVisibilityColumnUVE::OnHover)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyDoubleClickUVE,
                                               static_cast<std::int64_t>(HierarchyDoubleClickUVE::FocusInViewport)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyRenameModeUVE,
                                               static_cast<std::int64_t>(HierarchyRenameModeUVE::Dialog)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(
            Id::kHierarchyFilterModeUVE, static_cast<std::int64_t>(HierarchyFilterModeUVE::NameTypeAndComponents)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchySortModeUVE,
                                               static_cast<std::int64_t>(HierarchySortModeUVE::ByType)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, true));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmLargeSubtreeReparentUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmDeleteSubtreeUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyTreeLinesUVE,
                                               static_cast<std::int64_t>(HierarchyTreeLinesUVE::ToEachChild)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyRowHeightUVE, 30.0));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 30.0));
        expectChosen(editor.GetHierarchyViewSettingsUVE());

        // An indent or row height outside its drawable bounds, an action that does not exist, or
        // a value of the wrong type changes nothing.
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 2.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyIndentWidthUVE, 400.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyRowHeightUVE, 8.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyRowHeightUVE, 64.0));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyRowHeightUVE, std::string{"large"}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyDoubleClickUVE, std::int64_t{7}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyRenameModeUVE, std::int64_t{2}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyFilterModeUVE, std::int64_t{2}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchySortModeUVE, std::int64_t{3}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmLargeSubtreeReparentUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmDeleteSubtreeUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyShowIconsUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyColorCodeIconsUVE, std::int64_t{1}));
        EXPECT_FALSE(editor.SetEditorSettingUVE(Id::kHierarchyShowComponentBadgesUVE, std::int64_t{1}));
        expectChosen(editor.GetHierarchyViewSettingsUVE());
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        EXPECT_DOUBLE_EQ(
            engine.GetServicesUVE().GetConfigManagerUVE().GetDoubleUVE("editor.hierarchy.rowHeight", -1.0), 30.0);
        EXPECT_EQ(engine.GetServicesUVE().GetConfigManagerUVE().GetIntUVE(Id::kHierarchySortModeUVE, -1),
                  static_cast<std::int64_t>(HierarchySortModeUVE::ByType));
        EXPECT_TRUE(engine.GetServicesUVE().GetConfigManagerUVE().GetBoolUVE(Id::kHierarchyColorCodeIconsUVE, false));
        EXPECT_FALSE(engine.GetServicesUVE().GetConfigManagerUVE().GetBoolUVE(
            Id::kHierarchyShowComponentBadgesUVE, true));
        EXPECT_FALSE(engine.GetServicesUVE().GetConfigManagerUVE().GetBoolUVE(
            Id::kHierarchyConfirmLargeSubtreeReparentUVE, true));
        EXPECT_FALSE(engine.GetServicesUVE().GetConfigManagerUVE().GetBoolUVE(
            Id::kHierarchyConfirmDeleteSubtreeUVE, true));
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

TEST(EditorUVETest, HierarchySortModeUVE_SortsDisplayAndFlatFilterWithoutChangingSceneOrder) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_sort.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        namespace Id = EditorSettingIdUVE;
        using Kind = Scene::Objects::SceneObjectKindUVE;

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kNewObjectsUnderSelectionUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchySortModeUVE,
                                               static_cast<std::int64_t>(HierarchySortModeUVE::SceneOrder)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, false));
        const Scene::EntityUVE parent = editor.CreateDocumentSceneObjectUVE(Kind::Folder);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name = "Sort Test Container";

        const auto createNamedChild = [&editor, &entityManager, &sceneGraph, parent](
                                          const Kind kind, const std::string_view name) {
            const Scene::EntityUVE child = editor.CreateDocumentSceneObjectUVE(kind);
            if (child == Scene::kInvalidEntityUVE) {
                return child;
            }
            entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name = std::string{name};
            sceneGraph.SetParentUVE(entityManager, child, parent);
            return child;
        };
        const Scene::EntityUVE zuluCamera = createNamedChild(Kind::Camera3D, "Sortable Zulu");
        const Scene::EntityUVE alphaStatic = createNamedChild(Kind::Static3D, "Sortable Alpha");
        const Scene::EntityUVE charlieCamera = createNamedChild(Kind::Camera3D, "Sortable Charlie");
        const Scene::EntityUVE bravoBox = createNamedChild(Kind::BoxMesh3D, "Sortable Bravo");
        ASSERT_NE(zuluCamera, Scene::kInvalidEntityUVE);
        ASSERT_NE(alphaStatic, Scene::kInvalidEntityUVE);
        ASSERT_NE(charlieCamera, Scene::kInvalidEntityUVE);
        ASSERT_NE(bravoBox, Scene::kInvalidEntityUVE);
        const std::vector<Scene::EntityUVE> authoredOrder{zuluCamera, alphaStatic, charlieCamera, bravoBox};
        ASSERT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), authoredOrder);

        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "Sortable");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_EQ(EditorUVEAccessUVE::GetCachedHierarchyVisibleEntitiesUVE(editor), authoredOrder);

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchySortModeUVE,
                                               static_cast<std::int64_t>(HierarchySortModeUVE::Alphabetical)));
        const std::vector<Scene::EntityUVE> alphabetical{alphaStatic, bravoBox, charlieCamera, zuluCamera};
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyChildrenInViewOrderUVE(editor, parent), alphabetical);
        // Changing the setting invalidates the flattened filter cache, which follows the same order.
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_EQ(EditorUVEAccessUVE::GetCachedHierarchyVisibleEntitiesUVE(editor), alphabetical);

        // The Object remains a structural anchor even when its label would otherwise sort after a
        // loose document root.
        const Scene::EntityUVE looseRoot = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, looseRoot, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(looseRoot, Scene::NameComponentUVE{"A Loose Root"});
        const Scene::EntityUVE rootObject = editor.GetDocumentObjectUVE();
        const std::vector<Scene::EntityUVE> documentRoots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(documentRoots.size(), 2U);
        const std::vector<Scene::EntityUVE> sortedRoots =
            EditorUVEAccessUVE::SortHierarchyRowsUVE(editor, documentRoots);
        ASSERT_EQ(sortedRoots.size(), 2U);
        EXPECT_EQ(sortedRoots[0U], rootObject);
        EXPECT_EQ(sortedRoots[1U], looseRoot);

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchySortModeUVE,
                                               static_cast<std::int64_t>(HierarchySortModeUVE::ByType)));
        const std::vector<Scene::EntityUVE> byType{bravoBox, charlieCamera, zuluCamera, alphaStatic};
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyChildrenInViewOrderUVE(editor, parent), byType);
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_EQ(EditorUVEAccessUVE::GetCachedHierarchyVisibleEntitiesUVE(editor), byType);

        // Sorting is presentation-only: saving, sibling moves, undo, and runtime traversal still
        // see the authored order from the scene graph.
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, parent), authoredOrder);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, HierarchyScriptDialogUVE_DefaultsToClickedRowAllowsRetargetAndFilenameOverrideUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    std::string createdPath;
    std::string parentScriptPath;
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_create_script.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE parent =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        const Scene::EntityUVE child =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name = "HierarchyScriptParentUVE";
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name = "HierarchyScriptChildUVE";
        engine.GetServicesUVE().GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchySelectChildrenUVE, true));
        editor.SelectEntityUVE(parent);
        const std::vector<Scene::EntityUVE> selectionBefore = editor.GetSelectedEntitiesUVE();
        const Scene::EntityUVE activeBefore = editor.GetSelectedEntityUVE();
        ASSERT_NE(std::find(selectionBefore.begin(), selectionBefore.end(), child), selectionBefore.end());
        const bool canUndoBeforeDialog = editor.CanUndoUVE();

        const std::vector<Scene::EntityUVE> targets = EditorUVEAccessUVE::GetUVScriptTargetCandidatesUVE(editor);
        EXPECT_NE(std::find(targets.begin(), targets.end(), parent), targets.end());
        EXPECT_NE(std::find(targets.begin(), targets.end(), child), targets.end());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath.empty());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath.empty());

        ASSERT_TRUE(EditorUVEAccessUVE::OpenUVScriptCreationDialogUVE(editor, child));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationTargetUVE(editor), child);
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationFileNameUVE(editor), "HierarchyScriptChildUVE");
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationPathUVE(editor).rfind(
                      "scripts/HierarchyScriptChildUVE", 0U), 0U);
        EditorUVEAccessUVE::CancelUVScriptCreationDialogUVE(editor);
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath.empty());
        EXPECT_EQ(editor.CanUndoUVE(), canUndoBeforeDialog);

        // The picker may change the default target; the suggested file name follows until manually edited.
        ASSERT_TRUE(EditorUVEAccessUVE::OpenUVScriptCreationDialogUVE(editor, child));
        ASSERT_TRUE(EditorUVEAccessUVE::ChooseUVScriptCreationTargetUVE(editor, parent));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationTargetUVE(editor), parent);
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationFileNameUVE(editor), "HierarchyScriptParentUVE");
        EXPECT_FALSE(EditorUVEAccessUVE::IsUVScriptCreationFileNameEditedUVE(editor));
        EditorUVEAccessUVE::SetUVScriptCreationFileNameUVE(editor, "player_controller");
        EXPECT_TRUE(EditorUVEAccessUVE::IsUVScriptCreationFileNameEditedUVE(editor));
        ASSERT_TRUE(EditorUVEAccessUVE::ChooseUVScriptCreationTargetUVE(editor, child));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationFileNameUVE(editor), "player_controller");
        ASSERT_TRUE(EditorUVEAccessUVE::ChooseUVScriptCreationTargetUVE(editor, parent));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationPathUVE(editor).rfind(
                      "scripts/player_controller", 0U), 0U);
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmUVScriptCreationDialogUVE(editor));
        parentScriptPath = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath;
        ASSERT_FALSE(parentScriptPath.empty());
        EXPECT_EQ(parentScriptPath.rfind("scripts/player_controller", 0U), 0U);
        EXPECT_TRUE(parentScriptPath.ends_with(".uvs"));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath.empty());
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->entity, parent);
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->text.rfind("entity HierarchyScriptParentUVE : Object3D\n", 0U), 0U);
        EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);

        // A script created from this row defaults to that row's current object name.
        ASSERT_TRUE(EditorUVEAccessUVE::OpenUVScriptCreationDialogUVE(editor, child));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationTargetUVE(editor), child);
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationFileNameUVE(editor), "HierarchyScriptChildUVE");
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmUVScriptCreationDialogUVE(editor));
        createdPath = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath;
        ASSERT_FALSE(createdPath.empty());
        EXPECT_EQ(createdPath.rfind("scripts/HierarchyScriptChildUVE", 0U), 0U);
        EXPECT_TRUE(createdPath.ends_with(".uvs"));
        EXPECT_NE(parentScriptPath, createdPath);
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->entity, child);
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->text.rfind("entity HierarchyScriptChildUVE : Object3D\n", 0U), 0U);
        EXPECT_FALSE(EditorUVEAccessUVE::CreateUVScriptForEntityUVE(editor, child));
        EXPECT_FALSE(EditorUVEAccessUVE::CreateUVScriptForEntityUVE(editor, parent));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);

        editor.CloseOpenUVScriptUVE();
        ASSERT_TRUE(editor.UndoUVE()); // Undo the child's default-named script without disturbing the parent.
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath.empty());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath, parentScriptPath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.UndoUVE()); // Undo the manually named parent script independently.
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath.empty());
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath, parentScriptPath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath, createdPath);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(parent).scriptAssetPath, parentScriptPath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::error_code error;
    std::filesystem::remove(createdPath, error);
    std::filesystem::remove(parentScriptPath, error);
}

TEST(EditorUVETest, HierarchyScriptDialogIncludesObjectsWithoutScriptComponents) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    std::string scriptPath;
    {
        EditorUVE editor(engine.GetServicesUVE(),
                         "uve_editor_tests_hierarchy_script_without_component.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE canvas =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Canvas);
        ASSERT_NE(canvas, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(canvas));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(canvas));
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(canvas).name = "HierarchyScriptCanvasUVE";
        const Scene::EntityUVE selectedObject =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(selectedObject, Scene::kInvalidEntityUVE);

        const std::vector<Scene::EntityUVE> candidates =
            EditorUVEAccessUVE::GetUVScriptTargetCandidatesUVE(editor);
        EXPECT_NE(std::find(candidates.begin(), candidates.end(), canvas), candidates.end());
        EXPECT_NE(std::find(candidates.begin(), candidates.end(), selectedObject), candidates.end());
        const Scene::EntityUVE folder = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        EXPECT_NE(std::find(candidates.begin(), candidates.end(), folder), candidates.end());
        EXPECT_FALSE(EditorUVEAccessUVE::OpenUVScriptCreationDialogUVE(editor, folder));
        editor.SelectEntityUVE(selectedObject);
        const std::vector<Scene::EntityUVE> selectionBefore = editor.GetSelectedEntitiesUVE();
        const Scene::EntityUVE activeBefore = editor.GetSelectedEntityUVE();

        ASSERT_TRUE(EditorUVEAccessUVE::OpenUVScriptCreationDialogUVE(editor, canvas));
        EXPECT_EQ(EditorUVEAccessUVE::GetUVScriptCreationFileNameUVE(editor), "HierarchyScriptCanvasUVE");
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmUVScriptCreationDialogUVE(editor));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(canvas));
        scriptPath = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(canvas).scriptAssetPath;
        EXPECT_EQ(scriptPath.rfind("scripts/HierarchyScriptCanvasUVE", 0U), 0U);
        EXPECT_TRUE(scriptPath.ends_with(".uvs"));
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->entity, canvas);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);

        editor.CloseOpenUVScriptUVE();
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(canvas));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(canvas));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(canvas).scriptAssetPath, scriptPath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::error_code error;
    std::filesystem::remove(scriptPath, error);
}

TEST(EditorUVETest, HierarchyScriptActionsUVE_AttachOpenAndDetachExactTargetsUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    std::string sourcePath;
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_script_actions.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE source =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Canvas);
        const Scene::EntityUVE target =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Canvas);
        const Scene::EntityUVE selectionAnchor =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(source, Scene::kInvalidEntityUVE);
        ASSERT_NE(target, Scene::kInvalidEntityUVE);
        ASSERT_NE(selectionAnchor, Scene::kInvalidEntityUVE);
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(source));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(target));

        ASSERT_TRUE(EditorUVEAccessUVE::CreateUVScriptForEntityUVE(editor, source, "hierarchy_attach_source"));
        sourcePath = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(source).scriptAssetPath;
        ASSERT_FALSE(sourcePath.empty());
        EXPECT_TRUE(editor.DescribeScriptAssetProblemUVE(sourcePath).empty());
        editor.CloseOpenUVScriptUVE();
        editor.SelectEntityUVE(selectionAnchor);
        const std::vector<Scene::EntityUVE> selectionBefore = editor.GetSelectedEntitiesUVE();
        const Scene::EntityUVE activeBefore = editor.GetSelectedEntityUVE();
        const bool canUndoBeforeDialog = editor.CanUndoUVE();

        const std::vector<std::string> knownScripts = editor.GetKnownScriptAssetPathsUVE();
        EXPECT_NE(std::find(knownScripts.begin(), knownScripts.end(), sourcePath), knownScripts.end());
        ASSERT_TRUE(EditorUVEAccessUVE::OpenHierarchyScriptAttachDialogUVE(editor, target));
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyScriptAttachTargetUVE(editor), target);
        EditorUVEAccessUVE::CancelHierarchyScriptAttachUVE(editor);
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(target));
        EXPECT_EQ(editor.CanUndoUVE(), canUndoBeforeDialog);

        ASSERT_TRUE(EditorUVEAccessUVE::OpenHierarchyScriptAttachDialogUVE(editor, target));
        EditorUVEAccessUVE::SetHierarchyScriptAttachPathUVE(editor, sourcePath);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyScriptAttachPathUVE(editor), sourcePath);
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmHierarchyScriptAttachUVE(editor));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(target));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath, sourcePath);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(source).scriptAssetPath, sourcePath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        EXPECT_FALSE(EditorUVEAccessUVE::OpenHierarchyScriptAttachDialogUVE(editor, target));
        EXPECT_FALSE(EditorUVEAccessUVE::AssignScriptToEntityUVE(editor, target, sourcePath));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath, sourcePath);

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(target));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(target));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath, sourcePath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);

        ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(target));
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->entity, target);
        EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        editor.CloseOpenUVScriptUVE();

        ASSERT_TRUE(EditorUVEAccessUVE::AssignScriptToEntityUVE(editor, target, {}));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath.empty());
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath, sourcePath);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath.empty());
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), selectionBefore);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), activeBefore);

        const bool canUndoBeforeInvalidAttach = editor.CanUndoUVE();
        ASSERT_TRUE(EditorUVEAccessUVE::OpenHierarchyScriptAttachDialogUVE(editor, target));
        EditorUVEAccessUVE::SetHierarchyScriptAttachPathUVE(editor, "scripts/missing-hierarchy-script.uvs");
        EXPECT_FALSE(EditorUVEAccessUVE::ConfirmHierarchyScriptAttachUVE(editor));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(target).scriptAssetPath.empty());
        EditorUVEAccessUVE::CancelHierarchyScriptAttachUVE(editor);
        EXPECT_EQ(editor.CanUndoUVE(), canUndoBeforeInvalidAttach);
        EXPECT_TRUE(editor.DescribeScriptAssetProblemUVE(sourcePath).empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::error_code error;
    std::filesystem::remove(sourcePath, error);
}

TEST(EditorUVETest, HierarchyRenameModeUVE_RoutesTheSharedRenameRequestAndCancellation) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rename_mode.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(entity, Scene::kInvalidEntityUVE);
        editor.SelectEntityUVE(entity);
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyRenameModeUVE,
                                               static_cast<std::int64_t>(HierarchyRenameModeUVE::Inline)));

        EditorUVEAccessUVE::BeginHierarchyRenameUVE(editor, entity);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyRenameEntityUVE(editor), entity);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyRenameUsingDialogUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyRenameDialogOpenRequestedUVE(editor));
        EditorUVEAccessUVE::CancelHierarchyRenameUVE(editor);

        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyRenameModeUVE,
                                               static_cast<std::int64_t>(HierarchyRenameModeUVE::Dialog)));
        EditorUVEAccessUVE::BeginHierarchyRenameUVE(editor, entity);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyRenameEntityUVE(editor), entity);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyRenameUsingDialogUVE(editor));
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyRenameDialogOpenRequestedUVE(editor));
        EditorUVEAccessUVE::CancelHierarchyRenameUVE(editor);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyRenameEntityUVE(editor), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyRenameUsingDialogUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyRenameDialogOpenRequestedUVE(editor));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyRenameUVE_ActiveRowRemainsRenameableInChildrenFollowSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rename_children_selection.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        engine.GetServicesUVE().GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(parent, Scene::NameComponentUVE{"Parent"});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(child, Scene::NameComponentUVE{"Child"});
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchySelectChildrenUVE, true));
        editor.SelectEntityUVE(parent);
        ASSERT_FALSE(editor.HasSingleDocumentSelectionUVE());
        const std::vector<Scene::EntityUVE> expectedSelection{parent, child};
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);

        // F2 and the active-row rename target the parent, not every selected descendant.
        EditorUVEAccessUVE::BeginHierarchyRenameUVE(editor, parent);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyRenameEntityUVE(editor), parent);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Parent Revised"));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name, "Parent Revised");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name, "Child");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name, "Parent");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name, "Parent Revised");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);

        // The row menu can also rename a non-active member without replacing the group selection.
        EditorUVEAccessUVE::CancelHierarchyRenameUVE(editor);
        EditorUVEAccessUVE::BeginHierarchyRenameUVE(editor, child);
        EXPECT_EQ(EditorUVEAccessUVE::GetHierarchyRenameEntityUVE(editor), child);
        ASSERT_TRUE(EditorUVEAccessUVE::SetHierarchyEntityNameUVE(editor, child, "Child Revised"));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(parent).name, "Parent Revised");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name, "Child Revised");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name, "Child");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name, "Child Revised");
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedSelection);

        EditorUVEAccessUVE::CancelHierarchyRenameUVE(editor);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyFilterUVE_UsesModeCaseComponentAndAncestorPreferences) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_filter.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const auto createNamed = [&editor, &entityManager](const Scene::Objects::SceneObjectKindUVE kind,
                                                           const std::string& name) {
            const Scene::EntityUVE entity = editor.CreateDocumentSceneObjectUVE(kind);
            if (entity != Scene::kInvalidEntityUVE) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name = name;
            }
            return entity;
        };
        const Scene::EntityUVE group =
            createNamed(Scene::Objects::SceneObjectKindUVE::Object3D, "Group");
        const Scene::EntityUVE child =
            createNamed(Scene::Objects::SceneObjectKindUVE::Object3D, "LeafMatch");
        const Scene::EntityUVE scripted =
            createNamed(Scene::Objects::SceneObjectKindUVE::Object3D, "Runner");
        const Scene::EntityUVE typed =
            createNamed(Scene::Objects::SceneObjectKindUVE::BoxMesh3D, "Geometry");
        const Scene::EntityUVE audioSource =
            createNamed(Scene::Objects::SceneObjectKindUVE::AudioSource3D, "Speaker");
        ASSERT_NE(group, Scene::kInvalidEntityUVE);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        ASSERT_NE(scripted, Scene::kInvalidEntityUVE);
        ASSERT_NE(typed, Scene::kInvalidEntityUVE);
        ASSERT_NE(audioSource, Scene::kInvalidEntityUVE);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, group);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(scripted));
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(audioSource));

        namespace Id = EditorSettingIdUVE;
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterModeUVE,
                                               static_cast<std::int64_t>(HierarchyFilterModeUVE::NameOnly)));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "Script");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));

        ASSERT_TRUE(editor.SetEditorSettingUVE(
            Id::kHierarchyFilterModeUVE, static_cast<std::int64_t>(HierarchyFilterModeUVE::NameTypeAndComponents)));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "BoxMesh3D");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, typed));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterModeUVE,
                                               static_cast<std::int64_t>(HierarchyFilterModeUVE::NameOnly)));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, typed));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "type:BoxMesh3D");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, typed));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "component:Audio Source");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "component:audio source");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));

        ASSERT_TRUE(editor.SetEditorSettingUVE(
            Id::kHierarchyFilterModeUVE, static_cast<std::int64_t>(HierarchyFilterModeUVE::NameTypeAndComponents)));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "script");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, true));
        // Matched against the row's name rather than a component's: every object carries an "Editor
        // Description" component whose label contains a lowercase "script", so a lowercase "script"
        // legitimately matches all of them once case counts.
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "runner");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "Runner");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "component:audio source");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "component:Audio Source");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, false));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        editor.SelectEntityUVE(audioSource);
        ASSERT_TRUE(editor.RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AudioSource));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        ASSERT_TRUE(editor.UndoUVE());
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        ASSERT_TRUE(editor.RedoUVE());
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));
        ASSERT_TRUE(editor.UndoUVE());
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, audioSource));

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterModeUVE,
                                               static_cast<std::int64_t>(HierarchyFilterModeUVE::NameOnly)));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, false));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "runner");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, true));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "Runner");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, scripted));

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterCaseSensitiveUVE, false));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, true));
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "LeafMatch");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, child));
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, group));
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyFilterKeepAncestorsUVE, false));
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, child));
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, group));
        const std::vector<Scene::EntityUVE> documentRoots = editor.GetDocumentRootsUVE();
        ASSERT_FALSE(documentRoots.empty());
        EditorUVEAccessUVE::SetHierarchyFilterUVE(editor, "root");
        EditorUVEAccessUVE::RebuildHierarchyFilterCacheUVE(editor);
        for (const Scene::EntityUVE documentRoot : documentRoots) {
            EXPECT_TRUE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, documentRoot));
        }
        EXPECT_FALSE(EditorUVEAccessUVE::IsHierarchyEntityVisibleUVE(editor, group));

        editor.ShutdownUVE();
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_commands.uvscene");
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

        // Undo is unavailable with nothing done; after creating an object it runs.
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
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_commands_reload.uvscene");
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
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_commands_corrupt.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_game.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_workspace_game_file.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_viewport_views.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_folds.uvscene");
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
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_inspector_folds_reload.uvscene");
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

TEST(EditorUVETest, InspectorFoldsUVE_MigratesLegacyObjectEntriesToTypedStringList) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
    settings.SetIntUVE("editor.inspector.folds.count", 2);
    settings.SetStringUVE("editor.inspector.folds.0.key", "section:transform");
    settings.SetBoolUVE("editor.inspector.folds.0.open", false);
    settings.SetStringUVE("editor.inspector.folds.1.key", "group:light/Shadow");
    settings.SetBoolUVE("editor.inspector.folds.1.open", true);
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_folds_migration.uvscene");
        editor.InitUVE();
        EXPECT_FALSE(editor.IsInspectorFoldOpenUVE("section:transform", true));
        EXPECT_TRUE(editor.IsInspectorFoldOpenUVE("group:light/Shadow", false));
        EXPECT_EQ(settings.GetStringUVE("editor.inspector.folds.0", ""), "0:section:transform");
        EXPECT_EQ(settings.GetStringUVE("editor.inspector.folds.1", ""), "1:group:light/Shadow");
        EXPECT_FALSE(settings.HasKeyUVE("editor.inspector.folds.0.key"));
        EXPECT_FALSE(settings.HasKeyUVE("editor.inspector.folds.0.open"));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs.uvscene");
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
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs_reload.uvscene");
        reloaded.InitUVE();
        EXPECT_FALSE(reloaded.IsViewportGridVisibleUVE());
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridOpacityUVE(), 0.35F);
        reloaded.ShutdownUVE();
    }

    // A corrupt stored opacity falls back to its default alone; the stored visibility survives it.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.grid.opacity", 7.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_grid_prefs_corrupt.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_grid_cell.uvscene");
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
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_grid_cell_reload.uvscene");
        reloaded.InitUVE();
        EXPECT_FLOAT_EQ(reloaded.GetViewportGridCellSizeUVE(), 0.25F);
        reloaded.ShutdownUVE();
    }

    // A corrupt stored size falls back to the 1 m default.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.grid.cellSize", -3.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_grid_cell_corrupt.uvscene");
        corrupt.InitUVE();
        EXPECT_FLOAT_EQ(corrupt.GetViewportGridCellSizeUVE(), 1.0F);
        corrupt.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportSelectionOutlineUVE_RefusesBadColoursAndPersistsAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    using Color = EditorUVE::ViewportAxisColorUVE;
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs.uvscene");
        editor.InitUVE();
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisibleUVE());

        // A bad channel is refused whole: nothing changes. NaN fails the range check by design.
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(false, Color{1.5F, 0.0F, 0.0F}));
        EXPECT_FALSE(editor.SetViewportSelectionOutlineUVE(
            false, Color{1.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}));
        EXPECT_TRUE(editor.IsViewportSelectionOutlineVisibleUVE());

        ASSERT_TRUE(editor.SetViewportSelectionOutlineUVE(false, Color{0.2F, 0.6F, 1.0F}));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_reload.uvscene");
        reloaded.InitUVE();
        EXPECT_FALSE(reloaded.IsViewportSelectionOutlineVisibleUVE());
        EXPECT_FLOAT_EQ(reloaded.GetViewportSelectionOutlineColorUVE().g, 0.6F);
        reloaded.ShutdownUVE();
    }
    // The width is not a preference any more - the host draws it at the renderer's own 1 px floor -
    // so a settings file written before its removal still loads: the key has no descriptor left, so
    // it is inert rather than something to validate, clamp or fall back from.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.selectionOutline.thickness", 40.0);
    {
        EditorUVE stale(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_stale_key.uvscene");
        stale.InitUVE();
        EXPECT_FALSE(stale.IsViewportSelectionOutlineVisibleUVE());
        EXPECT_FLOAT_EQ(stale.GetViewportSelectionOutlineColorUVE().b, 1.0F);
        stale.ShutdownUVE();
    }
    // One bad colour channel sends the whole colour back to its default, never a mixed colour.
    engine.GetServicesUVE().GetConfigManagerUVE().SetDoubleUVE("editor.viewport.selectionOutline.b", 3.0);
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_outline_prefs_corrupt_color.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_axis_colors.uvscene");
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
        Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
        EXPECT_FALSE(settings.HasKeyUVE("editor.viewport.axisColors.set"));
        EXPECT_DOUBLE_EQ(settings.GetDoubleUVE("editor.viewport.axisColors.x.r", 0.0), chosenX.r);
        EXPECT_DOUBLE_EQ(settings.GetDoubleUVE("editor.viewport.axisColors.y.g", 0.0), chosenY.g);
        EXPECT_DOUBLE_EQ(settings.GetDoubleUVE("editor.viewport.axisColors.z.b", 0.0), chosenZ.b);
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_axis_colors_reload.uvscene");
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

        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_axis_colors_corrupt.uvscene");
        corrupt.InitUVE();
        EXPECT_FALSE(corrupt.AreViewportAxisColorsSetUVE())
            << "an out-of-range persisted channel must fall back to unset, not be clamped in";
        corrupt.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ViewportAxisColorsUVE_InvalidOrMissingPaletteDoesNotOverwriteHostDefaults) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    using AxisColorUVE = EditorUVE::ViewportAxisColorUVE;
    const AxisColorUVE hostX{0.12F, 0.34F, 0.56F};
    const AxisColorUVE hostY{0.23F, 0.45F, 0.67F};
    const AxisColorUVE hostZ{0.31F, 0.53F, 0.75F};
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_axis_colors_host_defaults.uvscene");
        editor.InitUVE();
        ASSERT_TRUE(editor.SetViewportAxisColorsUVE(hostX, hostY, hostZ)); // the host seeds its defaults

        // With no saved palette, loading session settings must not clear the host's palette.
        EditorUVEAccessUVE::LoadSessionSettingsUVE(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSetUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(0).r, hostX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(1).g, hostY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(2).b, hostZ.b);

        // The legacy explicit-unset marker and a corrupt, incomplete descriptor palette are also
        // not permission to replace host defaults with the editor's placeholder values.
        Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
        settings.SetBoolUVE("editor.viewport.axisColors.set", false);
        EditorUVEAccessUVE::LoadSessionSettingsUVE(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSetUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(0).r, hostX.r);

        settings.SetBoolUVE("editor.viewport.axisColors.set", true);
        settings.SetDoubleUVE("editor.viewport.axisColors.x.r", 1.5);
        EditorUVEAccessUVE::LoadSessionSettingsUVE(editor);
        EXPECT_TRUE(editor.AreViewportAxisColorsSetUVE());
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(0).r, hostX.r);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(1).g, hostY.g);
        EXPECT_FLOAT_EQ(editor.GetViewportAxisColorUVE(2).b, hostZ.b);
        editor.ShutdownUVE();
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_favorites.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_favorites_reload.uvscene");
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

TEST(EditorUVETest, FavoriteProjectsUVE_PersistenceIsBoundedAndKeepsPathOrder) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    constexpr std::size_t kExpectedFavoriteLimit = 128U;
    std::vector<std::filesystem::path> favorites;
    favorites.reserve(kExpectedFavoriteLimit + 2U);
    for (std::size_t index = 0U; index < kExpectedFavoriteLimit + 2U; ++index) {
        favorites.emplace_back("favorite-" + std::to_string(index));
    }
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_favorites_bound.uvscene");
        editor.InitUVE();
        for (const std::filesystem::path& favorite : favorites) {
            EditorUVEAccessUVE::ToggleProjectPathFavoriteUVE(editor, favorite);
        }
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
        EXPECT_EQ(settings.GetIntUVE("editor.favorites.count", -1),
                  static_cast<std::int64_t>(kExpectedFavoriteLimit));
        EXPECT_EQ(settings.GetStringUVE("editor.favorites.0", ""), favorites.front().generic_string());
        EXPECT_EQ(settings.GetStringUVE(
                      "editor.favorites." + std::to_string(kExpectedFavoriteLimit - 1U), ""),
                  favorites[kExpectedFavoriteLimit - 1U].generic_string());
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_favorites_bound_reload.uvscene");
        reloaded.InitUVE();
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(reloaded, favorites.front()));
        EXPECT_TRUE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(
            reloaded, favorites[kExpectedFavoriteLimit - 1U]));
        EXPECT_FALSE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(
            reloaded, favorites[kExpectedFavoriteLimit]));
        EXPECT_FALSE(EditorUVEAccessUVE::IsProjectPathFavoritedUVE(reloaded, favorites.back()));
        reloaded.ShutdownUVE();
    }

    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ContentShelvesPersistAcrossSessionReload) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_shelves.uvscene");
        editor.InitUVE();
        ContentShelvesUVE& shelves = EditorUVEAccessUVE::GetContentShelvesUVE(editor);
        ASSERT_EQ(shelves.CreateUVE("Props"), "Props");
        ASSERT_EQ(shelves.CreateUVE("Empty"), "Empty");
        ASSERT_TRUE(shelves.AddItemUVE("Props", "Sea/rock.uvmodel"));
        ASSERT_TRUE(shelves.AddItemUVE("Props", "Sea/Models"));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));
        editor.ShutdownUVE();
    }
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_shelves_reload.uvscene");
        editor.InitUVE();
        const ContentShelvesUVE& shelves = EditorUVEAccessUVE::GetContentShelvesUVE(editor);
        ASSERT_EQ(shelves.GetAllUVE().size(), 2U);
        EXPECT_EQ(shelves.GetAllUVE()[0].name, "Props");
        EXPECT_EQ(shelves.GetAllUVE()[0].items,
                  (std::vector<std::filesystem::path>{"Sea/rock.uvmodel", "Sea/Models"}));
        EXPECT_EQ(shelves.GetAllUVE()[1].name, "Empty");
        EXPECT_TRUE(shelves.GetAllUVE()[1].items.empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, TeamShelvesLiveInTheProjectAndFollowChangesOnDisk) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    std::filesystem::path shelvesPath;
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_team_shelves.uvscene");
        editor.InitUVE();
        shelvesPath = EditorUVEAccessUVE::GetSharedShelvesPathUVE(editor);
        std::filesystem::remove(shelvesPath);
        EXPECT_EQ(shelvesPath.filename(), "project.uvshelves");
        ContentShelvesUVE& shelves = EditorUVEAccessUVE::GetContentShelvesUVE(editor);
        // No team shelf: no file is made.
        ASSERT_EQ(shelves.CreateUVE("Mine"), "Mine");
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSharedShelvesUVE(editor));
        EXPECT_FALSE(std::filesystem::exists(shelvesPath));

        ASSERT_EQ(shelves.CreateUVE("Props", true), "Props");
        ASSERT_TRUE(shelves.AddItemUVE("Props", "Sea/rock.uvmodel"));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSharedShelvesUVE(editor));
        ASSERT_TRUE(std::filesystem::exists(shelvesPath));
        ASSERT_TRUE(EditorUVEAccessUVE::SaveSessionSettingsUVE(editor));

        // A teammate's change arrives on disk (a pull): the editor picks it up.
        {
            std::ofstream file(shelvesPath, std::ios::trunc);
            file << R"({"version":1,"shelves":[{"name":"Props","items":["Sea/rock.uvmodel","Sea/Wave.uvtex"]}]})";
        }
        std::filesystem::last_write_time(shelvesPath,
                                         std::filesystem::last_write_time(shelvesPath) + std::chrono::seconds{2});
        EditorUVEAccessUVE::ReloadSharedShelvesIfChangedUVE(editor);
        EXPECT_TRUE(shelves.ContainsUVE("Props", "Sea/Wave.uvtex"));
        EXPECT_NE(shelves.FindUVE("Mine"), nullptr) << "a reload leaves personal shelves alone";
        editor.ShutdownUVE();
    }
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_team_shelves_reload.uvscene");
        editor.InitUVE();
        const ContentShelvesUVE& shelves = EditorUVEAccessUVE::GetContentShelvesUVE(editor);
        ASSERT_NE(shelves.FindUVE("Props"), nullptr);
        EXPECT_TRUE(shelves.FindUVE("Props")->shared);
        EXPECT_EQ(shelves.FindUVE("Props")->items.size(), 2U);
        ASSERT_NE(shelves.FindUVE("Mine"), nullptr);
        EXPECT_FALSE(shelves.FindUVE("Mine")->shared);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(shelvesPath);
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, SaveAllWritesOnlyWhatChanged) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "uve_editor_tests_save_all.uvscene";
    std::filesystem::remove(scenePath);
    {
        EditorUVE editor(engine.GetServicesUVE(), scenePath);
        editor.InitUVE();
        EXPECT_EQ(editor.SaveAllUVE(), 0U);
        EXPECT_EQ(EditorUVEAccessUVE::GetContentStatusMessageUVE(editor), "Nothing to save.");

        const Scene::EntityUVE root = engine.GetServicesUVE().GetEntityManagerUVE().CreateEntityUVE();
        AttachRootUVE(engine, root, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(root);
        Scene::TransformComponentUVE moved{};
        moved.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        ASSERT_TRUE(editor.SetSelectedLocalTransformUVE(moved));
        ASSERT_TRUE(editor.IsSceneDirtyUVE());

        EXPECT_EQ(editor.SaveAllUVE(), 1U);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(std::filesystem::exists(scenePath));
        EXPECT_EQ(EditorUVEAccessUVE::GetContentStatusMessageUVE(editor), "Saved 1 file.");
        EXPECT_EQ(editor.SaveAllUVE(), 0U);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(scenePath);
}

TEST(EditorUVETest, SelectionAndInspectorTransformEdit_ValidateLifetimeAndFiniteValues) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_selection.uvscene");
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

TEST(EditorUVETest, HierarchyRangeSelectionUVE_UsesVisibleOrderAndKeepsItsAnchor) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_range_selection.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        const Scene::EntityUVE locked = entityManager.CreateEntityUVE();
        const Scene::EntityUVE fourth = entityManager.CreateEntityUVE();
        const Scene::EntityUVE fifth = entityManager.CreateEntityUVE();
        for (const Scene::EntityUVE entity : {first, second, locked, fourth, fifth}) {
            AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
        }
        const std::vector<Scene::EntityUVE> visibleOrder{first, second, locked, fourth, fifth};
        ASSERT_TRUE(editor.SetEntityLockedUVE(locked, true));
        editor.SelectEntityUVE(second);

        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, fourth, visibleOrder, false);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{second, fourth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), fourth);
        editor.TickUVE(); // pruning must not replace the Shift anchor with the active end of the range
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, first, visibleOrder, false);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{first, second}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);

        editor.SelectEntityUVE(second);
        editor.ToggleEntitySelectionUVE(fourth); // Ctrl-click moves the anchor to this row
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, fifth, visibleOrder, true);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{second, fourth, fifth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), fifth);
        editor.TickUVE();
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, first, visibleOrder, false);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{first, second, fourth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);

        // If filtering or a collapsed branch hides the old anchor, Shift falls back to the clicked
        // row and establishes a new anchor in the currently visible order.
        editor.SelectEntityUVE(second);
        const std::vector<Scene::EntityUVE> subsetOrder{fourth, fifth};
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, fifth, subsetOrder, false);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{fifth});
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, fourth, subsetOrder, false);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{fourth, fifth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), fourth);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyRubberBandSelectionUVE_SelectsIntersectingRowsAndSupportsAdditiveMode) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_rubber_band.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        const Scene::EntityUVE locked = entityManager.CreateEntityUVE();
        const Scene::EntityUVE fourth = entityManager.CreateEntityUVE();
        const Scene::EntityUVE fifth = entityManager.CreateEntityUVE();
        const std::vector<Scene::EntityUVE> rows{first, second, locked, fourth, fifth};
        for (const Scene::EntityUVE entity : rows) {
            AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
        }
        const std::vector<std::array<float, 4U>> rowBounds{
            {0.0F, 0.0F, 100.0F, 16.0F}, {0.0F, 16.0F, 100.0F, 32.0F},
            {0.0F, 32.0F, 100.0F, 48.0F}, {0.0F, 48.0F, 100.0F, 64.0F},
            {0.0F, 64.0F, 100.0F, 80.0F}};
        ASSERT_TRUE(editor.SetEntityLockedUVE(locked, true));

        // This box clips the second, locked third, and fourth rows; the locked row is skipped.
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, rows, rowBounds,
                                                          20.0F, 24.0F, 80.0F, 56.0F,
                                                          false, {}, Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{second, fourth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), fourth);

        editor.SelectEntityUVE(first);
        const std::vector<Scene::EntityUVE> selectionBefore = editor.GetSelectedEntitiesUVE();
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, rows, rowBounds,
                                                          80.0F, 56.0F, 20.0F, 24.0F,
                                                          true, selectionBefore, first);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{first, second, fourth}));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), fourth);

        // A zero-area box selects no rows: replace clears, while additive mode preserves the base.
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, rows, rowBounds,
                                                          20.0F, 80.0F, 80.0F, 80.0F,
                                                          false, {}, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        editor.SelectEntityUVE(first);
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, rows, rowBounds,
                                                          20.0F, 90.0F, 80.0F, 90.0F,
                                                          true, editor.GetSelectedEntitiesUVE(), first);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{first});
        EXPECT_EQ(editor.GetSelectedEntityUVE(), first);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchySelectChildrenUVE_ExpandsClickToggleRangeAndBoxSelections) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_select_children.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        const Scene::EntityUVE grandchild = entityManager.CreateEntityUVE();
        const Scene::EntityUVE lockedChild = entityManager.CreateEntityUVE();
        const Scene::EntityUVE childBelowLocked = entityManager.CreateEntityUVE();
        const Scene::EntityUVE otherParent = entityManager.CreateEntityUVE();
        const Scene::EntityUVE otherChild = entityManager.CreateEntityUVE();
        const Scene::EntityUVE thirdParent = entityManager.CreateEntityUVE();
        for (const Scene::EntityUVE entity : {parent, child, grandchild, lockedChild, childBelowLocked,
                                               otherParent, otherChild, thirdParent}) {
            AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
        }
        sceneGraph.SetParentUVE(entityManager, child, parent);
        sceneGraph.SetParentUVE(entityManager, lockedChild, parent);
        sceneGraph.SetParentUVE(entityManager, grandchild, child);
        sceneGraph.SetParentUVE(entityManager, childBelowLocked, lockedChild);
        sceneGraph.SetParentUVE(entityManager, otherChild, otherParent);
        ASSERT_TRUE(editor.SetEntityLockedUVE(lockedChild, true));

        const std::vector<Scene::EntityUVE> expectedParentGroup{parent, child, grandchild, childBelowLocked};
        const std::vector<Scene::EntityUVE> expectedOtherGroup{otherParent, otherChild};
        std::vector<Scene::EntityUVE> expectedCombined = expectedOtherGroup;
        expectedCombined.insert(expectedCombined.end(), expectedParentGroup.begin(), expectedParentGroup.end());
        const std::vector<Scene::EntityUVE> visibleOrder{parent, otherParent, thirdParent};
        EXPECT_FALSE(editor.GetHierarchyViewSettingsUVE().selectChildren);
        editor.SelectEntityUVE(parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), std::vector<Scene::EntityUVE>{parent});

        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchySelectChildrenUVE, true));
        EXPECT_TRUE(editor.GetHierarchyViewSettingsUVE().selectChildren);
        EXPECT_EQ(editor.GetEditorSettingUVE(EditorSettingIdUVE::kHierarchySelectChildrenUVE),
                  Config::SettingValueUVE{true});
        editor.SelectEntityUVE(parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedParentGroup);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);

        // Ctrl/Cmd-clicking a parent toggles that parent and its unlocked subtree as one group.
        editor.ClearSelectionUVE();
        editor.ToggleEntitySelectionUVE(parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedParentGroup);
        editor.ToggleEntitySelectionUVE(parent);
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        editor.ToggleEntitySelectionUVE(otherParent);
        editor.ToggleEntitySelectionUVE(parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedCombined);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), parent);
        editor.ToggleEntitySelectionUVE(parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedOtherGroup);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), otherChild);

        // Shift range and rubber-band expand visible row hits to collapsed or filtered descendants.
        editor.SelectEntityUVE(parent);
        EditorUVEAccessUVE::SelectHierarchyRangeUVE(editor, otherParent, visibleOrder, false);
        std::vector<Scene::EntityUVE> expectedRange = expectedParentGroup;
        expectedRange.insert(expectedRange.end(), expectedOtherGroup.begin(), expectedOtherGroup.end());
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedRange);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), otherParent);

        const std::vector<std::array<float, 4U>> rowBounds{
            {0.0F, 0.0F, 100.0F, 16.0F}, {0.0F, 16.0F, 100.0F, 32.0F},
            {0.0F, 32.0F, 100.0F, 48.0F}};
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, visibleOrder, rowBounds,
                                                          20.0F, 2.0F, 80.0F, 14.0F,
                                                          false, {}, Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedParentGroup);
        const std::vector<Scene::EntityUVE> selectionBefore = editor.GetSelectedEntitiesUVE();
        EditorUVEAccessUVE::ApplyHierarchyBoxSelectionUVE(editor, visibleOrder, rowBounds,
                                                          20.0F, 18.0F, 80.0F, 30.0F,
                                                          true, selectionBefore, parent);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), expectedRange);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), otherParent);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, MultiSelectionUVE_TickPrunesStaleEntitiesAndPromotesLastLiveSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_selection_stale.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_entities.uvscene");
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
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(empty).name, "Object3D");
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

        // One-root document: every created archetype lives under the Object (chained by
        // creation-under-selection), and the document's single root is the Object itself.
        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 1U);
        EXPECT_EQ(roots.front(), editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_primitive_appearance.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_invalid.uvscene");
        EXPECT_EQ(editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty), Scene::kInvalidEntityUVE);

        editor.InitUVE();
        EXPECT_EQ(editor.CreateDocumentEntityUVE(static_cast<EditorEntityKindUVE>(999)),
                  Scene::kInvalidEntityUVE);
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_create_names.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rename.uvscene");
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

TEST(EditorUVETest, RenameCollisionUVE_UsesTheNextFreeDocumentWideNumericSuffix) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rename_collision.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        const Scene::EntityUVE occupiedName = entityManager.CreateEntityUVE();
        const Scene::EntityUVE occupiedSuffix = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, source, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, occupiedName, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, occupiedSuffix, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(source, Scene::NameComponentUVE{"Source"});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(occupiedName, Scene::NameComponentUVE{"Lamp"});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(occupiedSuffix, Scene::NameComponentUVE{"Lamp 2"});
        editor.SelectEntityUVE(source);

        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("Lamp"));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(source).name, "Lamp 3");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(occupiedName).name, "Lamp");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(occupiedSuffix).name, "Lamp 2");
        EXPECT_EQ(editor.GetSelectedEntityUVE(), source);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(source).name, "Source");
        EXPECT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(source).name, "Lamp 3");

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ObjectDiagnosticsUVE_ClassifySetupProblemsBySeverity) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object_diagnostics.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        using Severity = HierarchyDiagnosticSeverityUVE;

        // A plain, well-formed object has nothing to report and no script.
        const Scene::EntityUVE clean = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, clean, Scene::TransformComponentUVE{});
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(clean).empty());
        EXPECT_FALSE(editor.GetObjectScriptPathUVE(clean).has_value());

        // A mesh object with no assigned mesh is a warning, while a missing asset is an error.
        const Scene::EntityUVE noMesh = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, noMesh, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(noMesh, Scene::MeshComponentUVE{});
        const std::vector<HierarchyDiagnosticUVE> noMeshDiagnostics = editor.GetObjectDiagnosticsUVE(noMesh);
        ASSERT_EQ(noMeshDiagnostics.size(), 1U);
        EXPECT_EQ(noMeshDiagnostics.front().severity, Severity::Warning);
        EXPECT_NE(noMeshDiagnostics.front().message.find("No mesh"), std::string::npos);

        const Scene::EntityUVE lostMesh = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lostMesh, Scene::TransformComponentUVE{});
        Scene::MeshComponentUVE lost{};
        lost.meshGuid = Asset::AssetGuidUVE{0x12345678ULL};
        lost.materialGuid = Asset::AssetGuidUVE{0x9abcdef0ULL};
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(lostMesh, lost);
        const std::vector<HierarchyDiagnosticUVE> missingAssetDiagnostics = editor.GetObjectDiagnosticsUVE(lostMesh);
        ASSERT_EQ(missingAssetDiagnostics.size(), 2U) << "missing mesh and missing material";
        EXPECT_EQ(missingAssetDiagnostics[0U].severity, Severity::Error);
        EXPECT_EQ(missingAssetDiagnostics[1U].severity, Severity::Error);

        // A valid script path has no diagnostic; a path outside the project is an error.
        const Scene::EntityUVE scripted = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, scripted, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ScriptComponentUVE>(scripted, Scene::ScriptComponentUVE{"scripts/player.uvs"});
        ASSERT_TRUE(editor.GetObjectScriptPathUVE(scripted).has_value());
        EXPECT_EQ(*editor.GetObjectScriptPathUVE(scripted), "scripts/player.uvs");
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(scripted).empty());
        entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(scripted).scriptAssetPath = "../outside.uvs";
        const std::vector<HierarchyDiagnosticUVE> scriptDiagnostics = editor.GetObjectDiagnosticsUVE(scripted);
        ASSERT_EQ(scriptDiagnostics.size(), 1U);
        EXPECT_EQ(scriptDiagnostics.front().severity, Severity::Error);

        // Non-finite transforms are errors; a Skeleton3D without a source model remains a warning.
        const Scene::EntityUVE invalidTransform = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, invalidTransform, Scene::TransformComponentUVE{});
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(invalidTransform).localPosition.x =
            std::numeric_limits<float>::infinity();
        const std::vector<HierarchyDiagnosticUVE> transformDiagnostics = editor.GetObjectDiagnosticsUVE(invalidTransform);
        ASSERT_EQ(transformDiagnostics.size(), 1U);
        EXPECT_EQ(transformDiagnostics.front().severity, Severity::Error);

        const Scene::EntityUVE invalidCamera = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, invalidCamera, Scene::TransformComponentUVE{});
        Scene::CameraComponentUVE camera{};
        camera.farPlane = camera.nearPlane;
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(invalidCamera, camera);
        const std::vector<HierarchyDiagnosticUVE> cameraDiagnostics = editor.GetObjectDiagnosticsUVE(invalidCamera);
        ASSERT_EQ(cameraDiagnostics.size(), 1U);
        EXPECT_EQ(cameraDiagnostics.front().severity, Severity::Error);
        entityManager.GetComponentUVE<Scene::CameraComponentUVE>(invalidCamera) = Scene::CameraComponentUVE{};
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(invalidCamera).empty());

        const Scene::EntityUVE invalidLight = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, invalidLight, Scene::TransformComponentUVE{});
        Scene::LightComponentUVE light{};
        light.intensity = -1.0F;
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(invalidLight, light);
        const std::vector<HierarchyDiagnosticUVE> lightDiagnostics = editor.GetObjectDiagnosticsUVE(invalidLight);
        ASSERT_EQ(lightDiagnostics.size(), 1U);
        EXPECT_EQ(lightDiagnostics.front().severity, Severity::Error);
        entityManager.GetComponentUVE<Scene::LightComponentUVE>(invalidLight) = Scene::LightComponentUVE{};
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(invalidLight).empty());

        const Scene::EntityUVE invalidAudioSource = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, invalidAudioSource, Scene::TransformComponentUVE{});
        Scene::AudioSourceComponentUVE audioSource{};
        audioSource.volume = -1.0F;
        entityManager.AddComponentUVE<Scene::AudioSourceComponentUVE>(invalidAudioSource, audioSource);
        const std::vector<HierarchyDiagnosticUVE> audioDiagnostics =
            editor.GetObjectDiagnosticsUVE(invalidAudioSource);
        ASSERT_EQ(audioDiagnostics.size(), 1U);
        EXPECT_EQ(audioDiagnostics.front().severity, Severity::Error);
        entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(invalidAudioSource) =
            Scene::AudioSourceComponentUVE{};
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(invalidAudioSource).empty());

        const Scene::EntityUVE skeleton = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, skeleton, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton, Scene::Skeleton3DComponentUVE{});
        const std::vector<HierarchyDiagnosticUVE> skeletonDiagnostics = editor.GetObjectDiagnosticsUVE(skeleton);
        ASSERT_EQ(skeletonDiagnostics.size(), 1U);
        EXPECT_EQ(skeletonDiagnostics.front().severity, Severity::Warning);

        // Not a document entity: nothing to say.
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(Scene::kInvalidEntityUVE).empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ObjectDiagnosticsUVE_CoverBuiltInAndMetadataValidatedNodeTypes) {
    const Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object_diagnostics_metadata.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        using Severity = HierarchyDiagnosticSeverityUVE;

        const auto createRoot = [&]() {
            const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
            if (entity != Scene::kInvalidEntityUVE) {
                AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
            }
            return entity;
        };
        const auto addComponent = [&]<typename Component>(Component component) {
            const Scene::EntityUVE entity = createRoot();
            if (entity != Scene::kInvalidEntityUVE) {
                entityManager.AddComponentUVE<Component>(entity, std::move(component));
            }
            return entity;
        };
        const auto expectOneError = [&editor](const Scene::EntityUVE entity,
                                              const std::string_view messagePart) {
            const std::vector<HierarchyDiagnosticUVE> diagnostics = editor.GetObjectDiagnosticsUVE(entity);
            EXPECT_EQ(diagnostics.size(), 1U) << messagePart;
            if (diagnostics.size() != 1U) {
                return;
            }
            EXPECT_EQ(diagnostics.front().severity, Severity::Error);
            EXPECT_NE(diagnostics.front().message.find(std::string{messagePart}), std::string::npos);
        };

        // Direct checks cover important built-ins that have no whole-instance rule in component metadata.
        Scene::PrimitiveMeshComponentUVE primitive{};
        primitive.baseColor.x = std::numeric_limits<float>::quiet_NaN();
        const Scene::EntityUVE primitiveEntity = addComponent(primitive);
        ASSERT_NE(primitiveEntity, Scene::kInvalidEntityUVE);
        expectOneError(primitiveEntity, "primitive mesh");

        Scene::ColliderComponentUVE collider{};
        collider.collisionLayer = 0U;
        const Scene::EntityUVE colliderEntity = addComponent(collider);
        ASSERT_NE(colliderEntity, Scene::kInvalidEntityUVE);
        expectOneError(colliderEntity, "collider");

        Scene::Rigid3DComponentUVE rigidBody{};
        rigidBody.drag = -1.0F;
        const Scene::EntityUVE rigidBodyEntity = addComponent(rigidBody);
        ASSERT_NE(rigidBodyEntity, Scene::kInvalidEntityUVE);
        expectOneError(rigidBodyEntity, "rigid-body");

        Scene::WorldEnvironment3DComponentUVE environment{};
        environment.exposure = 0.0F;
        const Scene::EntityUVE environmentEntity = addComponent(environment);
        ASSERT_NE(environmentEntity, Scene::kInvalidEntityUVE);
        expectOneError(environmentEntity, "world environment");

        const Scene::EntityUVE validLodEntity = addComponent(Scene::LodGroup3DComponentUVE{});
        ASSERT_NE(validLodEntity, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(validLodEntity).empty());

        Scene::LodGroup3DComponentUVE lod{};
        Asset::AssetGuidUVE missingLodMeshGuid{0xfedcba9876543210ULL};
        const Asset::IAssetDatabaseUVE& assetDatabase = engine.GetServicesUVE().GetAssetDatabaseUVE();
        while (assetDatabase.HasGuidUVE(missingLodMeshGuid)) {
            ++missingLodMeshGuid.value;
        }
        lod.lodMeshGuids[0U] = missingLodMeshGuid;
        const Scene::EntityUVE lodEntity = addComponent(lod);
        ASSERT_NE(lodEntity, Scene::kInvalidEntityUVE);
        const std::vector<HierarchyDiagnosticUVE> lodDiagnostics = editor.GetObjectDiagnosticsUVE(lodEntity);
        ASSERT_EQ(lodDiagnostics.size(), 1U);
        EXPECT_EQ(lodDiagnostics.front().severity, Severity::Error);
        EXPECT_NE(lodDiagnostics.front().message.find("LOD level 1"), std::string::npos);

        // Specialized object components reuse their registered validators and display names.
        Scene::AreaComponentUVE area{};
        area.halfExtents.x = 0.0F;
        const Scene::EntityUVE areaEntity = addComponent(area);
        ASSERT_NE(areaEntity, Scene::kInvalidEntityUVE);
        expectOneError(areaEntity, "Area3D");

        Scene::CharacterControllerComponentUVE character{};
        character.maxSlides = 0U;
        const Scene::EntityUVE characterEntity = addComponent(character);
        ASSERT_NE(characterEntity, Scene::kInvalidEntityUVE);
        expectOneError(characterEntity, "Character3D");

        Scene::RayCast3DComponentUVE rayCast{};
        rayCast.length = 0.0F;
        const Scene::EntityUVE rayCastEntity = addComponent(rayCast);
        ASSERT_NE(rayCastEntity, Scene::kInvalidEntityUVE);
        expectOneError(rayCastEntity, "RayCast3D");

        Scene::NavMeshVolume3DComponentUVE navMesh{};
        navMesh.boundsHalfExtents.y = 0.0F;
        const Scene::EntityUVE navMeshEntity = addComponent(navMesh);
        ASSERT_NE(navMeshEntity, Scene::kInvalidEntityUVE);
        expectOneError(navMeshEntity, "NavMeshVolume3D");

        Scene::NavSeeker3DComponentUVE navSeeker{};
        navSeeker.radius = 0.0F;
        const Scene::EntityUVE navSeekerEntity = addComponent(navSeeker);
        ASSERT_NE(navSeekerEntity, Scene::kInvalidEntityUVE);
        expectOneError(navSeekerEntity, "NavSeeker3D");

        Scene::SpawnPoint3DComponentUVE spawnPoint{};
        spawnPoint.spawnTag.clear();
        const Scene::EntityUVE spawnPointEntity = addComponent(spawnPoint);
        ASSERT_NE(spawnPointEntity, Scene::kInvalidEntityUVE);
        expectOneError(spawnPointEntity, "SpawnPoint3D");

        Scene::HealthComponentUVE health{};
        health.maxHealth = 0.0F;
        const Scene::EntityUVE healthEntity = addComponent(health);
        ASSERT_NE(healthEntity, Scene::kInvalidEntityUVE);
        expectOneError(healthEntity, "Health");

        const Scene::EntityUVE validAreaEntity = addComponent(Scene::AreaComponentUVE{});
        ASSERT_NE(validAreaEntity, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(validAreaEntity).empty());

        // A row with both severities uses the error glyph while keeping both messages in its tooltip.
        const Scene::EntityUVE mixedEntity = createRoot();
        ASSERT_NE(mixedEntity, Scene::kInvalidEntityUVE);
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(mixedEntity, Scene::MeshComponentUVE{});
        entityManager.AddComponentUVE<Scene::AreaComponentUVE>(mixedEntity, area);
        const std::vector<HierarchyDiagnosticUVE> mixedDiagnostics = editor.GetObjectDiagnosticsUVE(mixedEntity);
        ASSERT_EQ(mixedDiagnostics.size(), 2U);
        EXPECT_TRUE(HasHierarchyErrorDiagnosticsUVE(mixedDiagnostics));
        EXPECT_EQ(std::count_if(mixedDiagnostics.begin(), mixedDiagnostics.end(), [](const auto& diagnostic) {
                      return diagnostic.severity == Severity::Error;
                  }),
                  1);
        EXPECT_EQ(std::count_if(mixedDiagnostics.begin(), mixedDiagnostics.end(), [](const auto& diagnostic) {
                      return diagnostic.severity == Severity::Warning;
                  }),
                  1);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, ObjectDiagnosticsUVE_ReportPathReferencesThatNoLongerResolve) {
    // A project whose content root holds a rigged model, two materials, a sky texture, a level and
    // a baked navigation mesh, plus a script the project itself holds: every path-valued reference
    // the badge judges starts out with a real file behind it, so a path that is not one of them can
    // be called broken with confidence.
    const std::filesystem::path contentRoot = "uve_editor_tests_path_diagnostics_content";
    const std::filesystem::path scriptPath = "scripts/hero.uvs";
    std::filesystem::remove_all(contentRoot);
    std::filesystem::create_directories(contentRoot / "Characters");
    std::filesystem::create_directories(contentRoot / "Materials");
    std::filesystem::create_directories(contentRoot / "Textures");
    std::filesystem::create_directories(contentRoot / "Levels");
    std::filesystem::create_directories(contentRoot / "Navigation");
    std::filesystem::create_directories(scriptPath.parent_path());
    {
        std::ofstream rig(contentRoot / "Characters" / "hero.gltf", std::ios::binary | std::ios::trunc);
        rig << R"({"asset":{"version":"2.0"},"nodes":[{}],"skins":[{"joints":[0]}]})";
        std::ofstream material(contentRoot / "Materials" / "dirt.uvmat", std::ios::binary | std::ios::trunc);
        material << "{}";
        std::ofstream overlay(contentRoot / "Materials" / "damage_flash.uvmat", std::ios::binary | std::ios::trunc);
        overlay << "{}";
        std::ofstream sky(contentRoot / "Textures" / "dusk.uvtex", std::ios::binary | std::ios::trunc);
        sky << "{}";
        std::ofstream level(contentRoot / "Levels" / "courtyard.uvscene", std::ios::binary | std::ios::trunc);
        level << "{}";
        std::ofstream navMesh(contentRoot / "Navigation" / "yard.uvnav", std::ios::binary | std::ios::trunc);
        navMesh << "{}";
        std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
        script << "export let speed = 1.0\n";
    }
    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    std::filesystem::remove(config.settingsFilePath);
    config.projectContentRootUVE = contentRoot;
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_path_diagnostics.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        using Severity = HierarchyDiagnosticSeverityUVE;

        const auto createRoot = [&]() {
            const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
            if (entity != Scene::kInvalidEntityUVE) {
                AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
            }
            return entity;
        };
        const auto addComponent = [&]<typename Component>(Component component) {
            const Scene::EntityUVE entity = createRoot();
            if (entity != Scene::kInvalidEntityUVE) {
                entityManager.AddComponentUVE<Component>(entity, std::move(component));
            }
            return entity;
        };
        const auto expectClean = [&editor](const Scene::EntityUVE entity, const std::string_view what) {
            EXPECT_TRUE(editor.GetObjectDiagnosticsUVE(entity).empty()) << what;
        };
        const auto expectOneError = [&editor](const Scene::EntityUVE entity, const std::string_view messagePart) {
            const std::vector<HierarchyDiagnosticUVE> diagnostics = editor.GetObjectDiagnosticsUVE(entity);
            EXPECT_EQ(diagnostics.size(), 1U) << messagePart;
            if (diagnostics.size() != 1U) {
                return;
            }
            EXPECT_EQ(diagnostics.front().severity, Severity::Error);
            EXPECT_NE(diagnostics.front().message.find(std::string{messagePart}), std::string::npos);
        };

        Scene::ScriptComponentUVE script{};
        script.scriptAssetPath = scriptPath.generic_string();
        const Scene::EntityUVE scripted = addComponent(script);
        Scene::Skeleton3DComponentUVE skeleton{};
        skeleton.skeletonAssetPath = "Characters/hero.gltf";
        const Scene::EntityUVE rigged = addComponent(skeleton);
        Scene::Decal3DComponentUVE decal{};
        decal.materialAssetPath = "Materials/dirt.uvmat";
        const Scene::EntityUVE decalled = addComponent(decal);
        Scene::FogVolume3DComponentUVE fog{};
        fog.materialAssetPath = "Materials/dirt.uvmat";
        const Scene::EntityUVE fogged = addComponent(fog);
        Scene::WorldEnvironment3DComponentUVE environment{};
        environment.skyAssetPath = "Textures/dusk.uvtex";
        const Scene::EntityUVE sky = addComponent(environment);
        Scene::LevelStreamer3DComponentUVE streamer{};
        streamer.levelPath = "Levels/courtyard.uvscene";
        const Scene::EntityUVE streaming = addComponent(streamer);
        Scene::NavMeshVolume3DComponentUVE navigation{};
        navigation.navigationMeshAssetPath = "Navigation/yard.uvnav";
        const Scene::EntityUVE navigating = addComponent(navigation);
        Scene::SurfaceInstanceComponentUVE surface{};
        surface.materialOverridePath = "Materials/dirt.uvmat";
        surface.materialOverlayPath = "Materials/damage_flash.uvmat";
        const Scene::EntityUVE drawable = addComponent(surface);

        // Before the editor has refreshed its content tree it has not looked at the project yet,
        // so no path reference is judged at all - a badge is never a guess about a project the
        // editor has not read. The script is clean here for the stronger reason that its file is
        // really there, which is what the checks below prove it still is.
        expectClean(scripted, "the script file exists");
        expectClean(rigged, "unjudged until the editor has refreshed its content tree");
        expectClean(decalled, "unjudged until the editor has refreshed its content tree");
        expectClean(fogged, "unjudged until the editor has refreshed its content tree");
        expectClean(sky, "unjudged until the editor has refreshed its content tree");
        expectClean(streaming, "unjudged until the editor has refreshed its content tree");
        expectClean(navigating, "unjudged until the editor has refreshed its content tree");
        expectClean(drawable, "unjudged until the editor has refreshed its content tree");

        editor.TickUVE(); // the first content-index refresh: the content tree is current from here
        expectClean(scripted, "the script file exists");
        expectClean(rigged, "the content tree lists the model source");
        expectClean(decalled, "the content tree lists the decal material");
        expectClean(fogged, "the content tree lists the fog material");
        expectClean(sky, "the content tree lists the sky texture");
        expectClean(streaming, "the content tree lists the level");
        expectClean(navigating, "the content tree lists the baked navigation mesh");
        expectClean(drawable, "the content tree lists both materials");

        // The same references, pointed at files that are not there: one error each, naming what is
        // gone. The script is resolved by the mount and by a real path, so it is judged either way.
        entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(scripted).scriptAssetPath = "scripts/gone.uvs";
        expectOneError(scripted, "attached script");
        entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(rigged).skeletonAssetPath =
            "Characters/gone.gltf";
        expectOneError(rigged, "source model");
        entityManager.GetComponentUVE<Scene::Decal3DComponentUVE>(decalled).materialAssetPath =
            "Materials/gone.uvmat";
        expectOneError(decalled, "decal material");
        entityManager.GetComponentUVE<Scene::FogVolume3DComponentUVE>(fogged).materialAssetPath =
            "Materials/gone.uvmat";
        expectOneError(fogged, "fog material");
        entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(sky).skyAssetPath =
            "Textures/gone.uvtex";
        expectOneError(sky, "sky texture");
        entityManager.GetComponentUVE<Scene::LevelStreamer3DComponentUVE>(streaming).levelPath =
            "Levels/gone.uvscene";
        expectOneError(streaming, "level file");
        entityManager.GetComponentUVE<Scene::NavMeshVolume3DComponentUVE>(navigating).navigationMeshAssetPath =
            "Navigation/gone.uvnav";
        expectOneError(navigating, "navigation mesh");
        entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(drawable).materialOverridePath =
            "Materials/gone.uvmat";
        expectOneError(drawable, "material override");
        entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(drawable).materialOverridePath =
            "Materials/dirt.uvmat";
        entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(drawable).materialOverlayPath =
            "Materials/gone.uvmat";
        expectOneError(drawable, "material overlay");

        // A path that comes back is a reference that stops being reported.
        entityManager.GetComponentUVE<Scene::LevelStreamer3DComponentUVE>(streaming).levelPath =
            "Levels/courtyard.uvscene";
        expectClean(streaming, "the level is listed again");
        entityManager.GetComponentUVE<Scene::NavMeshVolume3DComponentUVE>(navigating).navigationMeshAssetPath =
            "Navigation/yard.uvnav";
        expectClean(navigating, "the navigation mesh is listed again");
        entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(drawable).materialOverlayPath =
            "Materials/damage_flash.uvmat";
        expectClean(drawable, "both materials are listed again");

        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
    std::filesystem::remove(scriptPath);
    std::filesystem::remove_all(contentRoot);
}

TEST(EditorUVETest, InspectorClipboardUVE_CopyPasteResetComponentsAndTransformUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_clipboard.uvscene");
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

        // An object without the component cannot be copied from.
        editor.ClearSelectionUVE();
        EXPECT_FALSE(editor.CopySelectedComponentUVE(*primitive));
        EXPECT_FALSE(editor.CopySelectedTransformUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorPropertyClipboardUVE_CopyPastePropertyUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_property_clipboard.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
        const Core::TypeMetadataEntryUVE* const primitive =
            registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::PrimitiveMeshComponentUVE)));
        ASSERT_NE(primitive, nullptr);
        const auto findProperty = [primitive](const char* const name) {
            for (const Core::TypeMetadataPropertyUVE& property : primitive->properties) {
                if (property.name == name) {
                    return &property;
                }
            }
            return static_cast<const Core::TypeMetadataPropertyUVE*>(nullptr);
        };
        const Core::TypeMetadataPropertyUVE* const kind = findProperty("kind");
        const Core::TypeMetadataPropertyUVE* const baseColor = findProperty("baseColor");
        ASSERT_NE(kind, nullptr);
        ASSERT_NE(baseColor, nullptr);

        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        const Scene::EntityUVE target = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, source, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
            source, Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::UVSphere,
                                                     Math::Vector3UVE{0.9F, 0.1F, 0.2F}});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(target, Scene::PrimitiveMeshComponentUVE{});

        // Nothing copied yet: nothing to paste.
        editor.SelectEntityUVE(target);
        EXPECT_FALSE(editor.CanPasteSelectedComponentPropertyUVE(*primitive, *kind));
        EXPECT_FALSE(editor.PasteSelectedComponentPropertyUVE(*primitive, *kind));

        // One property travels on its own: the shape copies without the color.
        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.CopySelectedComponentPropertyUVE(*primitive, *kind));
        editor.SelectEntityUVE(target);
        ASSERT_TRUE(editor.PasteSelectedComponentPropertyUVE(*primitive, *kind));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).kind,
                  Scene::PrimitiveMeshKindUVE::UVSphere);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).baseColor,
                  (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).kind,
                  Scene::PrimitiveMeshKindUVE::Cube);

        // A clipboard of one property never pastes into another.
        EXPECT_FALSE(editor.CanPasteSelectedComponentPropertyUVE(*primitive, *baseColor));
        EXPECT_FALSE(editor.PasteSelectedComponentPropertyUVE(*primitive, *baseColor));

        // The color follows the same path, as one undoable step.
        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.CopySelectedComponentPropertyUVE(*primitive, *baseColor));
        editor.SelectEntityUVE(target);
        ASSERT_TRUE(editor.PasteSelectedComponentPropertyUVE(*primitive, *baseColor));
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).baseColor.x, 0.9F);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(target).baseColor.x, 1.0F);

        // An object without the component cannot be copied from.
        editor.ClearSelectionUVE();
        EXPECT_FALSE(editor.CopySelectedComponentPropertyUVE(*primitive, *kind));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorPropertyClipboardUVE_CopyPasteTransformPartsUndoably) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_property_clipboard_transform.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE source = entityManager.CreateEntityUVE();
        const Scene::EntityUVE target = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE placed{};
        placed.localPosition = Math::Vector3UVE{3.0F, 1.0F, -2.0F};
        placed.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};
        // Authored, not derived: a whole extra turn past 360, which no quaternion can hold.
        placed.localEulerRadians =
            Math::Vector3UVE{0.0F, 370.0F * std::numbers::pi_v<float> / 180.0F, 0.0F};
        placed.eulerOrder = Math::EulerOrderUVE::YXZ;
        placed.rotationEditMode = Scene::RotationEditModeUVE::Quaternion;
        placed.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
        AttachRootUVE(engine, source, placed);
        AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(source).localEulerRadians,
                  placed.localEulerRadians);

        // Nothing copied yet: nothing to paste.
        editor.SelectEntityUVE(target);
        EXPECT_FALSE(editor.CanPasteSelectedTransformPartUVE(TransformClipboardPartUVE::Position));
        EXPECT_FALSE(editor.PasteSelectedTransformPartUVE(TransformClipboardPartUVE::Position));

        // One part travels on its own: position copies without rotation or scale.
        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.CopySelectedTransformPartUVE(TransformClipboardPartUVE::Position));
        // A clipboard of one part never pastes into another.
        EXPECT_FALSE(editor.CanPasteSelectedTransformPartUVE(TransformClipboardPartUVE::Scale));
        editor.SelectEntityUVE(target);
        ASSERT_TRUE(editor.PasteSelectedTransformPartUVE(TransformClipboardPartUVE::Position));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localPosition,
                  placed.localPosition);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localScale,
                  (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localPosition,
                  Math::Vector3UVE{});

        // Rotation carries its Euler authoring state, so a pasted 370 stays 370.
        editor.SelectEntityUVE(source);
        ASSERT_TRUE(editor.CopySelectedTransformPartUVE(TransformClipboardPartUVE::Rotation));
        editor.SelectEntityUVE(target);
        ASSERT_TRUE(editor.PasteSelectedTransformPartUVE(TransformClipboardPartUVE::Rotation));
        const auto& rotated = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target);
        EXPECT_EQ(rotated.localRotation, placed.localRotation);
        EXPECT_EQ(rotated.localEulerRadians, placed.localEulerRadians);
        EXPECT_EQ(rotated.eulerOrder, placed.eulerOrder);
        EXPECT_EQ(rotated.rotationEditMode, placed.rotationEditMode);
        EXPECT_EQ(rotated.localPosition, Math::Vector3UVE{});
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localRotation,
                  Math::QuaternionUVE{});
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(target).localEulerRadians,
                  Math::Vector3UVE{});

        // An object without the component cannot be copied from.
        editor.ClearSelectionUVE();
        EXPECT_FALSE(editor.CopySelectedTransformPartUVE(TransformClipboardPartUVE::Position));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorPropertyClipboardUVE_CopyPropertyPaths) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_property_clipboard_paths.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Core::TypeMetadataRegistryUVE& registry = Scene::GetSceneComponentMetadataRegistryUVE();
        const Core::TypeMetadataEntryUVE* const primitive =
            registry.FindTypeByIndexUVE(std::type_index(typeid(Scene::PrimitiveMeshComponentUVE)));
        ASSERT_NE(primitive, nullptr);
        const Core::TypeMetadataPropertyUVE* kind = nullptr;
        for (const Core::TypeMetadataPropertyUVE& property : primitive->properties) {
            if (property.name == "kind") {
                kind = &property;
            }
        }
        ASSERT_NE(kind, nullptr);

        const Scene::EntityUVE level = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, level, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(level, Scene::NameComponentUVE{"Level"});
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, lamp, level);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(lamp, Scene::PrimitiveMeshComponentUVE{});

        editor.SelectEntityUVE(lamp);
        EXPECT_EQ(editor.GetSelectedComponentPropertyPathUVE(*primitive, *kind),
                  "Level/Lamp/component.primitive_mesh/kind");
        ASSERT_TRUE(editor.CopySelectedComponentPropertyPathUVE(*primitive, *kind));
        EXPECT_EQ(editor.GetClipboardTextUVE(), "Level/Lamp/component.primitive_mesh/kind");
        EXPECT_EQ(editor.GetSelectedTransformPartPathUVE(TransformClipboardPartUVE::Position),
                  "Level/Lamp/Transform/localPosition");
        ASSERT_TRUE(editor.CopySelectedTransformPartPathUVE(TransformClipboardPartUVE::Rotation));
        EXPECT_EQ(editor.GetClipboardTextUVE(), "Level/Lamp/Transform/localRotation");

        // No selection: no path.
        editor.ClearSelectionUVE();
        EXPECT_TRUE(editor.GetSelectedComponentPropertyPathUVE(*primitive, *kind).empty());
        EXPECT_FALSE(editor.CopySelectedComponentPropertyPathUVE(*primitive, *kind));
        EXPECT_TRUE(editor.GetSelectedTransformPartPathUVE(TransformClipboardPartUVE::Position).empty());
        EXPECT_FALSE(editor.CopySelectedTransformPartPathUVE(TransformClipboardPartUVE::Position));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, HierarchyEntityLockUVE_PreventsSelectionWithoutDirtyingTheScene) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_lock.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE locked = entityManager.CreateEntityUVE();
        const Scene::EntityUVE selectable = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, locked, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, selectable, Scene::TransformComponentUVE{});

        editor.SelectEntityUVE(locked);
        editor.ToggleEntitySelectionUVE(selectable);
        ASSERT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{locked, selectable}));
        ASSERT_TRUE(editor.SetEntityLockedUVE(locked, true));
        EXPECT_TRUE(editor.IsEntityLockedUVE(locked));
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{selectable}));

        // Selecting or toggling a locked row leaves the current selection alone.
        editor.SelectEntityUVE(locked);
        editor.ToggleEntitySelectionUVE(locked);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), selectable);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{selectable}));
        EXPECT_FALSE(editor.SetEntityLockedUVE(locked, true));
        EXPECT_FALSE(editor.SetEntityLockedUVE(Scene::kInvalidEntityUVE, true));
        EXPECT_FALSE(editor.IsEntityLockedUVE(Scene::kInvalidEntityUVE));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        ASSERT_TRUE(editor.SetEntityLockedUVE(locked, false));
        EXPECT_FALSE(editor.IsEntityLockedUVE(locked));
        editor.SelectEntityUVE(locked);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{locked}));

        // Locking the active row removes it immediately from the selection.
        ASSERT_TRUE(editor.SetEntityLockedUVE(locked, true));
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, LockSelectionUVE_LocksTheGroupAndUnlockAllReleasesIt) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lock_selection.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, first, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, second, Scene::TransformComponentUVE{});

        EXPECT_FALSE(editor.HasLockedEntitiesUVE());
        EXPECT_EQ(editor.GetLockedEntityCountUVE(), 0U);
        EXPECT_FALSE(editor.CanLockSelectionUVE()); // nothing selected yet
        EXPECT_FALSE(editor.LockSelectionUVE());
        EXPECT_FALSE(editor.UnlockAllEntitiesUVE()); // and nothing locked

        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        ASSERT_TRUE(editor.CanLockSelectionUVE());
        ASSERT_TRUE(editor.LockSelectionUVE());
        EXPECT_TRUE(editor.IsEntityLockedUVE(first));
        EXPECT_TRUE(editor.IsEntityLockedUVE(second));
        // As with the single-row setter, the locked rows leave the selection as they go.
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        EXPECT_EQ(editor.GetLockedEntityCountUVE(), 2U);
        EXPECT_FALSE(editor.CanLockSelectionUVE()); // nothing selected is left to lock
        EXPECT_FALSE(editor.LockSelectionUVE());

        // A locked row is not selectable, and the single-row toggle still releases it.
        editor.SelectEntityUVE(first);
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        ASSERT_TRUE(editor.SetEntityLockedUVE(first, false));
        editor.SelectEntityUVE(first);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{first}));

        // Unlock All takes the rest in one action, and the row becomes selectable again.
        EXPECT_EQ(editor.GetLockedEntityCountUVE(), 1U);
        ASSERT_TRUE(editor.UnlockAllEntitiesUVE());
        EXPECT_FALSE(editor.HasLockedEntitiesUVE());
        EXPECT_EQ(editor.GetLockedEntityCountUVE(), 0U);
        EXPECT_FALSE(editor.UnlockAllEntitiesUVE());
        editor.SelectEntityUVE(second);
        EXPECT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{second}));

        // Session UI state throughout: the scene is not dirtied and no history entry appears.
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, LockCommandsFollowTheSelectionAndTheSessionLocks) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lock_commands.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE object = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, object, Scene::TransformComponentUVE{});

        // Lock is unavailable with an empty selection, and so is Unlock All with no locks.
        EXPECT_FALSE(editor.RunEditorCommandUVE("edit.lock"));
        EXPECT_FALSE(editor.RunEditorCommandUVE("edit.unlockAll"));

        editor.SelectEntityUVE(object);
        ASSERT_EQ(editor.GetSelectedEntitiesUVE(), (std::vector<Scene::EntityUVE>{object}));
        ASSERT_TRUE(editor.RunEditorCommandUVE("edit.lock"));
        EXPECT_TRUE(editor.IsEntityLockedUVE(object));
        EXPECT_TRUE(editor.GetSelectedEntitiesUVE().empty());
        // Nothing selected to lock again, but the session lock makes Unlock All available.
        EXPECT_FALSE(editor.RunEditorCommandUVE("edit.lock"));
        ASSERT_TRUE(editor.RunEditorCommandUVE("edit.unlockAll"));
        EXPECT_FALSE(editor.HasLockedEntitiesUVE());
        EXPECT_FALSE(editor.RunEditorCommandUVE("edit.unlockAll"));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        // The commands are declared like the rest: labelled, categorized, and without a shortcut.
        for (const EditorCommandUVE& command : editor.GetEditorCommandsUVE()) {
            if (command.id != "edit.lock" && command.id != "edit.unlockAll") {
                continue;
            }
            EXPECT_FALSE(command.label.empty()) << command.id;
            EXPECT_EQ(command.category, "Edit") << command.id;
            EXPECT_TRUE(command.shortcuts[0].IsEmptyUVE()) << command.id;
        }
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, FrameSelectionUVE_FitsTheSelectionsWorldBounds) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_frame_selection.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE first = entityManager.CreateEntityUVE();
        const Scene::EntityUVE second = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, first, Scene::TransformComponentUVE{});
        AttachRootUVE(engine, second, Scene::TransformComponentUVE{});
        for (const Scene::EntityUVE entity : {first, second}) {
            entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
                entity, Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, {1.0F, 1.0F, 1.0F}});
        }
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(second).localPosition =
            Math::Vector3UVE{4.0F, 0.0F, 0.0F};
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        // Nothing selected: nothing to frame, and no request is queued.
        Math::Vector3UVE center{};
        float radius = 0.0F;
        EXPECT_FALSE(editor.CanFrameSelectionInViewportUVE());
        EXPECT_FALSE(editor.TryGetSelectionFrameUVE(center, radius));
        EXPECT_FALSE(editor.RequestViewportFrameSelectionUVE());
        EXPECT_EQ(editor.GetViewportFrameRequestSerialUVE(), 0U);

        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        ASSERT_TRUE(editor.CanFrameSelectionInViewportUVE());
        ASSERT_TRUE(editor.TryGetSelectionFrameUVE(center, radius));
        // Two unit cubes: half-extent 0.5 each, centred on x = 0 and x = 4, so -0.5..4.5.
        EXPECT_NEAR(center.x, 2.0F, 1.0e-4F);
        EXPECT_NEAR(center.y, 0.0F, 1.0e-4F);
        EXPECT_NEAR(center.z, 0.0F, 1.0e-4F);
        // Half-diagonal of -0.5..4.5 on x and +/-0.5 on y/z: sqrt(6.75).
        EXPECT_NEAR(radius, std::sqrt(6.75F), 1.0e-4F);

        ASSERT_TRUE(editor.RequestViewportFrameSelectionUVE());
        EXPECT_EQ(editor.GetViewportFrameRequestSerialUVE(), 1U);
        EXPECT_NEAR(editor.GetViewportFrameCenterUVE().x, 2.0F, 1.0e-4F);
        EXPECT_NEAR(editor.GetViewportFrameRadiusUVE(), std::sqrt(6.75F), 1.0e-4F);
        // Asking again re-frames: the serial is what the host watches, not the payload.
        ASSERT_TRUE(editor.RequestViewportFrameSelectionUVE());
        EXPECT_EQ(editor.GetViewportFrameRequestSerialUVE(), 2U);

        // A bare position - a Marker3D, a light, a plain object - frames that one point.
        const Scene::EntityUVE marker = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE markerTransform{};
        markerTransform.localPosition = Math::Vector3UVE{0.0F, 3.0F, -2.0F};
        AttachRootUVE(engine, marker, markerTransform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        editor.SelectEntityUVE(marker);
        ASSERT_TRUE(editor.TryGetSelectionFrameUVE(center, radius));
        EXPECT_NEAR(center.y, 3.0F, 1.0e-4F);
        EXPECT_NEAR(center.z, -2.0F, 1.0e-4F);
        EXPECT_FLOAT_EQ(radius, 0.0F);

        // The Object root is a pure Object with no transform of its own: skipped, not framed at the
        // origin, so a selection of only the root has nothing to frame.
        editor.SelectEntityUVE(editor.GetDocumentObjectUVE());
        EXPECT_FALSE(editor.CanFrameSelectionInViewportUVE());
        EXPECT_FALSE(editor.RequestViewportFrameSelectionUVE());
        EXPECT_EQ(editor.GetViewportFrameRequestSerialUVE(), 2U);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AlignViewToNodeUVE_LooksAlongTheObjectsOwnForwardAxis) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_align_view.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE object = entityManager.CreateEntityUVE();
        Math::QuaternionUVE quarterTurn{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.5707964F, quarterTurn));
        Scene::TransformComponentUVE transform{};
        transform.localPosition = Math::Vector3UVE{2.0F, 1.0F, -3.0F};
        transform.localRotation = quarterTurn;
        AttachRootUVE(engine, object, transform);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        // The Object root has no rotation to look along, and neither does an invalid handle.
        EXPECT_FALSE(editor.CanAlignViewToEntityUVE(editor.GetDocumentObjectUVE()));
        EXPECT_FALSE(editor.CanAlignViewToEntityUVE(Scene::kInvalidEntityUVE));
        ASSERT_TRUE(editor.CanAlignViewToEntityUVE(object));
        ASSERT_TRUE(editor.RequestViewportAlignViewToEntityUVE(object));
        EXPECT_EQ(editor.GetViewportAlignViewRequestSerialUVE(), 1U);

        const EditorViewportBookmarkUVE bookmark = editor.GetViewportAlignViewBookmarkUVE();
        // The pose looks AT the object, standing off at the marker focus distance.
        EXPECT_NEAR(bookmark.target.x, 2.0F, 1.0e-5F);
        EXPECT_NEAR(bookmark.target.y, 1.0F, 1.0e-5F);
        EXPECT_NEAR(bookmark.target.z, -3.0F, 1.0e-5F);
        EXPECT_NEAR(bookmark.distance, kEditorMarkerFocusDistanceUVE, 1.0e-5F);

        // Looking along the object's own forward: rebuilding a rotation from the bookmark's angles
        // must turn the engine's -Z onto the object's forward axis (a quarter turn about Y sends it
        // to -X).
        Math::QuaternionUVE look{};
        ASSERT_TRUE(TryComposeOrbitLookRotationUVE(bookmark.yawRadians, bookmark.pitchRadians, look));
        const Math::QuaternionUVE worldRotation =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(object).worldRotation;
        const Math::Vector3UVE forward = Math::RotateVectorUVE(worldRotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        const Math::Vector3UVE lookedAt = Math::RotateVectorUVE(look, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        EXPECT_NEAR(forward.x, lookedAt.x, 1.0e-5F);
        EXPECT_NEAR(forward.y, lookedAt.y, 1.0e-5F);
        EXPECT_NEAR(forward.z, lookedAt.z, 1.0e-5F);
        EXPECT_NEAR(forward.x, -1.0F, 1.0e-5F);
        EXPECT_NEAR(forward.z, 0.0F, 1.0e-5F);

        // Aligning the view moves nothing in the document.
        const Scene::TransformComponentUVE unchanged =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(object);
        EXPECT_EQ(unchanged.localPosition, transform.localPosition);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        // A verbatim-second request still re-applies, like every other serial-driven request.
        ASSERT_TRUE(editor.RequestViewportAlignViewToEntityUVE(object));
        EXPECT_EQ(editor.GetViewportAlignViewRequestSerialUVE(), 2U);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AlignSelectedEntityToViewUVE_TurnsTheObjectToTheCameraAngles) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_align_object.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Math::QuaternionUVE parentTurn{};
        ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.7853982F, parentTurn));
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localRotation = parentTurn;
        AttachRootUVE(engine, parent, parentTransform);
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        // Without the camera's angles there is nothing to align to, and with nothing selected there
        // is nothing to turn.
        EXPECT_FALSE(editor.HasViewportCameraAnglesUVE());
        EXPECT_FALSE(editor.CanAlignSelectedEntityToViewUVE());
        EXPECT_FALSE(editor.AlignSelectedEntityToViewUVE());
        editor.SelectEntityUVE(child);
        EXPECT_FALSE(editor.CanAlignSelectedEntityToViewUVE());
        editor.SetViewportCameraAnglesUVE(0.0F, 0.0F);
        EXPECT_TRUE(editor.HasViewportCameraAnglesUVE());
        ASSERT_TRUE(editor.CanAlignSelectedEntityToViewUVE());

        // yaw 0, pitch 0 is the eye sitting on +X and looking toward -X.
        ASSERT_TRUE(editor.AlignSelectedEntityToViewUVE());
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const Math::QuaternionUVE aligned =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldRotation;
        const Math::Vector3UVE alignedForward =
            Math::RotateVectorUVE(aligned, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        EXPECT_NEAR(alignedForward.x, -1.0F, 1.0e-5F);
        EXPECT_NEAR(alignedForward.y, 0.0F, 1.0e-5F);
        EXPECT_NEAR(alignedForward.z, 0.0F, 1.0e-5F);
        // The child's own local rotation is what changed; its position and the parent are untouched.
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localPosition,
                  childTransform.localPosition);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);
        // ... and the parent's 45 degree turn (a quarter turn would equal the view rotation at yaw 0 and
        // make the align a no-op) means the local value is not simply the view rotation.
        EXPECT_GT(std::abs(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child)
                               .localRotation.w -
                           1.0F),
                  1.0e-3F);
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.CanUndoUVE());

        // Aligning to the angles it already has changes nothing.
        EXPECT_FALSE(editor.AlignSelectedEntityToViewUVE());

        // Undo puts the identity rotation back.
        ASSERT_TRUE(editor.UndoUVE());
        const Math::QuaternionUVE restored =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(child).localRotation;
        EXPECT_NEAR(restored.w, 1.0F, 1.0e-5F);
        EXPECT_NEAR(restored.x, 0.0F, 1.0e-5F);
        EXPECT_NEAR(restored.y, 0.0F, 1.0e-5F);
        EXPECT_NEAR(restored.z, 0.0F, 1.0e-5F);

        // Non-finite angles are refused outright, leaving the last usable pair in place.
        editor.SetViewportCameraAnglesUVE(std::numeric_limits<float>::quiet_NaN(), 0.0F);
        EXPECT_TRUE(editor.HasViewportCameraAnglesUVE());
        // The undo above put the object back at identity, so aligning now turns it again - and it
        // turns to the retained (0, 0) pair, not to anything derived from the refused NaN.
        EXPECT_TRUE(editor.AlignSelectedEntityToViewUVE());
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const Math::Vector3UVE retainedForward = Math::RotateVectorUVE(
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldRotation,
            Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        EXPECT_NEAR(retainedForward.x, -1.0F, 1.0e-5F);
        EXPECT_NEAR(retainedForward.y, 0.0F, 1.0e-5F);
        EXPECT_NEAR(retainedForward.z, 0.0F, 1.0e-5F);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SetEntityVisibleUVE_TogglesAnyRowUndoablyWithoutTouchingSelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_visibility_toggle.uvscene");
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

        // An object without a Visibility component has no eye to toggle.
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(other));
        EXPECT_FALSE(editor.SetEntityVisibleUVE(other, false));
        EXPECT_FALSE(editor.SetEntityVisibleUVE(Scene::kInvalidEntityUVE, false));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ViewportFocusRequest_OnlyForObjectsWithAPlaceInTheScene) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_viewport_focus.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE placed = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, placed, Scene::TransformComponentUVE{});
        // An entity with no transform is like the Object root or a plain Object: nothing to look at.
        const Scene::EntityUVE unplaced = entityManager.CreateEntityUVE();

        const std::uint32_t serialBefore = editor.GetViewportFocusRequestSerialUVE();
        EXPECT_TRUE(editor.CanFocusEntityInViewportUVE(placed));
        ASSERT_TRUE(editor.RequestViewportFocusUVE(placed));
        EXPECT_EQ(editor.GetViewportFocusEntityUVE(), placed);
        EXPECT_EQ(editor.GetViewportFocusRequestSerialUVE(), serialBefore + 1U);

        // Asking again is a new request, so the host focuses again even on the same object.
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_hierarchy_branch.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const auto makeObject = [&](const Scene::EntityUVE parent) {
            const Scene::EntityUVE object = entityManager.CreateEntityUVE();
            AttachRootUVE(engine, object, Scene::TransformComponentUVE{});
            if (parent != Scene::kInvalidEntityUVE) {
                services.GetSceneGraphUVE().SetParentUVE(entityManager, object, parent);
            }
            return object;
        };
        // branch -> middle -> leaf, branch -> sibling leaf; and a separate tree beside it.
        const Scene::EntityUVE branch = makeObject(Scene::kInvalidEntityUVE);
        const Scene::EntityUVE middle = makeObject(branch);
        const Scene::EntityUVE leaf = makeObject(middle);
        const Scene::EntityUVE siblingLeaf = makeObject(branch);
        const Scene::EntityUVE otherRoot = makeObject(Scene::kInvalidEntityUVE);
        const Scene::EntityUVE otherChild = makeObject(otherRoot);

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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_transform.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_name.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_create.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE created = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::CollisionBox);
        ASSERT_TRUE(entityManager.IsAliveUVE(created));
        ASSERT_TRUE(editor.CanUndoUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(created));
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_capacity.uvscene", 1U);
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_history_stale.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_root.uvscene");
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
        ASSERT_EQ(roots.size(), 3U); // the Object + the raw source + its duplicate
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_child.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_subtree.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_root.uvscene");
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

TEST(EditorUVETest, CopyAndPasteSelectedEntityUVE_InsertsWhereANewObjectWouldGo) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_entity_clipboard.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

        const Scene::EntityUVE folder = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, folder, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::FolderComponentUVE>(folder);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(folder, Scene::NameComponentUVE{"Props"});
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, lamp, folder);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});

        EXPECT_FALSE(editor.HasEntityClipboardUVE());
        EXPECT_TRUE(editor.GetEntityClipboardLabelUVE().empty());

        editor.SelectEntityUVE(lamp);
        ASSERT_TRUE(editor.CopySelectedEntityUVE());
        EXPECT_TRUE(editor.HasEntityClipboardUVE());
        EXPECT_EQ(editor.GetEntityClipboardLabelUVE(), "1 object");
        // A copy changes nothing in the document.
        EXPECT_TRUE(entityManager.IsAliveUVE(lamp));

        // Paste goes where a new object would: into the selected folder.
        editor.SelectEntityUVE(folder);
        const Scene::EntityUVE pasted = editor.PasteEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(pasted));
        EXPECT_NE(pasted, lamp);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), pasted);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(pasted).name, "Lamp 2");
        const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, folder);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_NE(std::find(children.begin(), children.end(), pasted), children.end());

        // One Undo removes the inserted root; Redo puts it back under the same parent, same name.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(pasted));
        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE restored = editor.GetSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(restored));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(restored).name, "Lamp 2");
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, folder).size(), 2U);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, CutSelectedEntityUVE_RemovesItAndUndoBringsItBack) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_entity_cut.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

        const Scene::EntityUVE folder = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, folder, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::FolderComponentUVE>(folder);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(folder, Scene::NameComponentUVE{"Props"});
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, lamp, folder);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});

        editor.SelectEntityUVE(lamp);
        ASSERT_TRUE(editor.CutSelectedEntityUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(lamp));
        EXPECT_TRUE(editor.HasEntityClipboardUVE());

        // The cut freed the name, so a Paste into the same folder takes it back.
        editor.SelectEntityUVE(folder);
        const Scene::EntityUVE pasted = editor.PasteEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(pasted));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(pasted).name, "Lamp");

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(pasted));
        ASSERT_TRUE(editor.UndoUVE());
        // Undoing the cut restores the object in place, with a fresh handle.
        const Scene::EntityUVE restored = editor.GetSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(restored));
        EXPECT_NE(restored, lamp);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(restored).name, "Lamp");
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(restored).parent, folder);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EntityClipboardTextUVE_CopiesNodePathAndIdentifier) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_clipboard_text.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();

        const Scene::EntityUVE level = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, level, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(level, Scene::NameComponentUVE{"Level"});
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, lamp, level);
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});

        EXPECT_TRUE(editor.GetClipboardTextUVE().empty());
        ASSERT_TRUE(editor.CopyEntityNodePathUVE(lamp));
        EXPECT_EQ(editor.GetClipboardTextUVE(), "Level/Lamp");
        ASSERT_TRUE(editor.CopyEntityNodePathUVE(level));
        EXPECT_EQ(editor.GetClipboardTextUVE(), "Level");

        ASSERT_TRUE(editor.CopyEntityIdentifierUVE(lamp));
        EXPECT_EQ(editor.GetClipboardTextUVE(),
                  std::to_string(lamp.index) + ":" + std::to_string(lamp.generation));
        EXPECT_FALSE(editor.CopyEntityNodePathUVE(Scene::kInvalidEntityUVE));
        EXPECT_FALSE(editor.CopyEntityIdentifierUVE(Scene::kInvalidEntityUVE));

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EntityClipboardUVE_IsDroppedWhenTheDocumentIsReplaced) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_clipboard_lifetime.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});
        editor.SelectEntityUVE(lamp);

        ASSERT_TRUE(editor.CopySelectedEntityUVE());
        ASSERT_TRUE(editor.HasEntityClipboardUVE());
        ASSERT_TRUE(editor.SaveSceneUVE());
        ASSERT_TRUE(editor.LoadSceneUVE());
        EXPECT_FALSE(editor.HasEntityClipboardUVE());
        EXPECT_EQ(editor.PasteEntityUVE(), Scene::kInvalidEntityUVE);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, DuplicateNameSuffixSettingUVE_DrivesTheNumberPattern) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_name_suffix.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE lamp = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, lamp, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(lamp, Scene::NameComponentUVE{"Lamp"});

        // The shipped pattern reproduces the historical "Lamp 2".
        editor.SelectEntityUVE(lamp);
        const Scene::EntityUVE first = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(first));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(first).name, "Lamp 2");

        // Where the number goes is the author's to choose.
        ASSERT_TRUE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyDuplicateNameSuffixUVE,
                                               std::string{"_%n"}));
        editor.SelectEntityUVE(lamp);
        const Scene::EntityUVE second = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(second));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(second).name, "Lamp_2");

        // A pattern that cannot number anything is refused, and the stored one keeps working.
        EXPECT_FALSE(editor.SetEditorSettingUVE(EditorSettingIdUVE::kHierarchyDuplicateNameSuffixUVE,
                                                std::string{"-copy"}));
        editor.SelectEntityUVE(lamp);
        const Scene::EntityUVE third = editor.DuplicateSelectedEntityUVE();
        ASSERT_TRUE(entityManager.IsAliveUVE(third));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(third).name, "Lamp_3");

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_DuplicateUndoRedoUsesFreshHandlesAndRestoresDirtySelection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_history.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_history.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_stale_parent.uvscene");
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
        // The ever-present Object is the only thing left in the document.
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_duplicate_redo_invalidation.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_lifecycle_safety.uvscene");
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

TEST(EditorUVETest, LargeHierarchyReparentUVE_ConfirmationIsThresholdedCancelableAndConfigurable) {
    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.settingsFilePath = "uve_editor_tests_large_reparent_confirmation_settings.json";
    std::filesystem::remove(config.settingsFilePath);
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_large_reparent_confirmation.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
        const Scene::EntityUVE folder = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        ASSERT_NE(folder, Scene::kInvalidEntityUVE);

        const auto createSubtree = [&](const std::size_t entityCount) {
            std::vector<Scene::EntityUVE> subtree;
            subtree.reserve(entityCount);
            for (std::size_t index = 0U; index < entityCount; ++index) {
                const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
                AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
                sceneGraph.SetParentUVE(entityManager, entity, subtree.empty() ? folder : subtree.back());
                subtree.push_back(entity);
            }
            return subtree;
        };
        const auto createTarget = [&] {
            const Scene::EntityUVE target = entityManager.CreateEntityUVE();
            AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
            sceneGraph.SetParentUVE(entityManager, target, folder);
            return target;
        };

        const std::vector<Scene::EntityUVE> belowThreshold =
            createSubtree(kHierarchyLargeSubtreeReparentThresholdUVE - 1U);
        const Scene::EntityUVE belowThresholdTarget = createTarget();
        const std::vector<Scene::EntityUVE> largeSubtree =
            createSubtree(kHierarchyLargeSubtreeReparentThresholdUVE);
        const Scene::EntityUVE largeTarget = createTarget();
        editor.SelectEntityUVE(belowThreshold.front());

        // The threshold counts the dragged root: 63 entities move immediately, while 64 are held
        // for a decision. The small move still takes the existing single Undo transaction.
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyReparentUVE(
            editor, belowThreshold.front(), belowThresholdTarget));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(belowThreshold.front()).parent,
                  belowThresholdTarget);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(belowThreshold.front()).parent, folder);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        editor.SelectEntityUVE(largeSubtree.front());
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyReparentUVE(editor, largeSubtree.front(), largeTarget));
        ASSERT_TRUE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_EQ(EditorUVEAccessUVE::GetPendingHierarchyReparentCountUVE(editor),
                  kHierarchyLargeSubtreeReparentThresholdUVE);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, folder);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        EditorUVEAccessUVE::CancelHierarchyReparentUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::ConfirmHierarchyReparentUVE(editor));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, folder);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyReparentUVE(editor, largeSubtree.front(), largeTarget));
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmHierarchyReparentUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, largeTarget);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree[1U]).parent,
                  largeSubtree.front());
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, folder);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());

        namespace Id = EditorSettingIdUVE;
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmLargeSubtreeReparentUVE, false));
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyReparentUVE(editor, largeSubtree.front(), largeTarget));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, largeTarget);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, folder);

        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyDragToReparentUVE, false));
        EXPECT_FALSE(EditorUVEAccessUVE::RequestHierarchyReparentUVE(editor, largeSubtree.front(), largeTarget));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyReparentUVE(editor));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(largeSubtree.front()).parent, folder);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, DeleteSubtreeConfirmUVE_BranchAsksLeafDeletesImmediately) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_delete_confirm.uvscene");
        editor.InitUVE();
        namespace Id = EditorSettingIdUVE;
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
        const Scene::EntityUVE folder = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        ASSERT_NE(folder, Scene::kInvalidEntityUVE);
        const auto createChild = [&](const Scene::EntityUVE parent) {
            const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
            AttachRootUVE(engine, entity, Scene::TransformComponentUVE{});
            sceneGraph.SetParentUVE(entityManager, entity, parent);
            return entity;
        };
        Scene::EntityUVE branch = createChild(folder);
        Scene::EntityUVE leaf = createChild(branch);
        Scene::EntityUVE lone = createChild(folder);

        // Undo restores a deleted subtree under fresh handles (see EditorHistoryUVE_DeleteUndoRedo...
        // WithFreshHandles), selecting the restored root. Re-resolve branch and leaf from it.
        const auto reResolveBranch = [&] {
            branch = editor.GetSelectedEntityUVE();
            ASSERT_TRUE(entityManager.IsAliveUVE(branch));
            const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, branch);
            ASSERT_EQ(children.size(), 1U);
            leaf = children.front();
            EXPECT_TRUE(entityManager.IsAliveUVE(leaf));
        };
        // A branch stops to ask: the request succeeds but nothing dies until the confirm lands.
        editor.SelectEntityUVE(branch);
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyDeleteUVE(editor));
        EXPECT_TRUE(EditorUVEAccessUVE::HasPendingHierarchyDeleteUVE(editor));
        EXPECT_EQ(EditorUVEAccessUVE::GetPendingHierarchyDeleteCountUVE(editor), 2U);
        EXPECT_TRUE(entityManager.IsAliveUVE(branch));
        EXPECT_TRUE(entityManager.IsAliveUVE(leaf));
        ASSERT_TRUE(EditorUVEAccessUVE::ConfirmHierarchyDeleteUVE(editor));
        EXPECT_FALSE(entityManager.IsAliveUVE(branch));
        EXPECT_FALSE(entityManager.IsAliveUVE(leaf));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyDeleteUVE(editor));
        // One undo step brings the whole branch back.
        ASSERT_TRUE(editor.UndoUVE());
        reResolveBranch();

        // Cancel leaves the document untouched and disarms the confirm.
        editor.SelectEntityUVE(branch);
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyDeleteUVE(editor));
        EditorUVEAccessUVE::CancelHierarchyDeleteUVE(editor);
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyDeleteUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::ConfirmHierarchyDeleteUVE(editor));
        EXPECT_TRUE(entityManager.IsAliveUVE(branch));

        // With the preference off a branch deletes immediately, arming nothing.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmDeleteSubtreeUVE, false));
        editor.SelectEntityUVE(branch);
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyDeleteUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyDeleteUVE(editor));
        EXPECT_FALSE(entityManager.IsAliveUVE(branch));
        ASSERT_TRUE(editor.UndoUVE());
        reResolveBranch();

        // A lone object never asks, even with the preference on.
        ASSERT_TRUE(editor.SetEditorSettingUVE(Id::kHierarchyConfirmDeleteSubtreeUVE, true));
        editor.SelectEntityUVE(lone);
        ASSERT_TRUE(EditorUVEAccessUVE::RequestHierarchyDeleteUVE(editor));
        EXPECT_FALSE(EditorUVEAccessUVE::HasPendingHierarchyDeleteUVE(editor));
        EXPECT_FALSE(entityManager.IsAliveUVE(lone));
        ASSERT_TRUE(editor.UndoUVE());
        lone = editor.GetSelectedEntityUVE();
        EXPECT_TRUE(entityManager.IsAliveUVE(lone));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_RootMovesBelowTargetAndPreservesLocalTransform) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_root.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE target = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, target, Scene::TransformComponentUVE{});
        const Scene::EntityUVE moved = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE localTransform{};
        localTransform.localPosition = Math::Vector3UVE{2.0F, -3.0F, 7.0F};
        AttachRootUVE(engine, moved, localTransform);
        const Scene::EntityUVE folder = PutInObjectFolderUVE(engine, editor, {target, moved});
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
        const std::vector<Scene::EntityUVE> folderChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, folder);
        EXPECT_EQ(folderChildren, std::vector<Scene::EntityUVE>{target}); // the target stays in the folder

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_ChildCanReturnToRootWithoutDetachingDescendants) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_root_detach.uvscene");
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
        const Scene::EntityUVE folder = PutInObjectFolderUVE(engine, editor, {parent});
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_TRUE(services.GetSceneGraphUVE().GetChildrenUVE(entityManager, parent).empty());
        // "Return to root" in a level means the top of the object's folder.
        const std::vector<Scene::EntityUVE> objectChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, folder);
        EXPECT_NE(std::find(objectChildren.begin(), objectChildren.end(), child), objectChildren.end());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_history.uvscene");
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
        static_cast<void>(PutInObjectFolderUVE(engine, editor, {oldParent, newParent}));
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

TEST(EditorUVETest, ObjectUVE_ChildrenOfTheTransformlessRootStayFullyEditable) {
    // The Object is a pure Object: in the hierarchy, with no transform. Everything that used to
    // read the parent's world transform must treat it as identity, the way the scene graph does -
    // otherwise every direct child of the root would refuse to move, rotate or be reparented.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object_children.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        // In a level, objects sit in a folder - a transformless pure Object, like the Object.
        const Scene::EntityUVE rootObject = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        ASSERT_NE(rootObject, Scene::kInvalidEntityUVE);
        ASSERT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(rootObject));

        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE parentTransform{};
        parentTransform.localPosition = Math::Vector3UVE{0.0F, 10.0F, 0.0F};
        AttachRootUVE(engine, parent, parentTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, parent, rootObject);
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE childTransform{};
        childTransform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
        AttachRootUVE(engine, child, childTransform);
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, rootObject);
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
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(rootObject));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, rootObject);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_NEAR(entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(child).worldPosition.y,
                    worldBefore.y, 1.0e-5F);

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, rootObject);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, EditorHistoryUVE_RedoOfMoveToDocumentRootLandsUnderTheObject) {
    // "Move to document root" is spelled as no parent, and means the Object. History has to
    // record the parent the entity actually got, or redo would set no parent at all and leave a
    // stray beside the root - a second top-level object in a one-root document.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_to_root_redo.uvscene");
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        // In a level, "the top" for an object is its folder.
        const Scene::EntityUVE rootObject = EditorUVEAccessUVE::GetObjectFolderUVE(editor);
        const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, parent, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, parent, rootObject);
        const Scene::EntityUVE child = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, child, Scene::TransformComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, child, parent);
        editor.SelectEntityUVE(child);

        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, rootObject);
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, parent);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(child).parent, rootObject);
        EXPECT_EQ(editor.GetDocumentRootsUVE(), std::vector<Scene::EntityUVE>{editor.GetDocumentObjectUVE()});

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, ReparentSelectedEntityUVE_RejectsCyclesNoOpNonDocumentStaleAndNonRunningStates) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_safety.uvscene");
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
        // kInvalidEntityUVE as the new parent now means "move under the Object" (see
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_stale_parent.uvscene");
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
        static_cast<void>(PutInObjectFolderUVE(engine, editor, {oldParent, newParent}));
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

TEST(EditorUVETest, ReparentToggleUVE_DefaultsToKeepLocalAndRoundTrips) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_reparent_toggle.uvscene");
        editor.InitUVE();
        // The row menu's "Keep Global Transform" checkbox reads and writes this: locals are kept
        // until the user opts in, every session.
        EXPECT_EQ(editor.GetReparentTransformModeUVE(), EditorReparentTransformModeUVE::KeepLocal);
        ASSERT_TRUE(editor.SetReparentTransformModeUVE(EditorReparentTransformModeUVE::KeepWorld));
        EXPECT_EQ(editor.GetReparentTransformModeUVE(), EditorReparentTransformModeUVE::KeepWorld);
        ASSERT_TRUE(editor.SetReparentTransformModeUVE(EditorReparentTransformModeUVE::KeepLocal));
        EXPECT_EQ(editor.GetReparentTransformModeUVE(), EditorReparentTransformModeUVE::KeepLocal);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, KeepWorldReparentUVE_PreservesCompatibleWorldTrsAndHistory) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_keep_world_reparent.uvscene");
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
        static_cast<void>(PutInObjectFolderUVE(engine, editor, {parent, moved}));
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_keep_world_reject.uvscene");
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
        static_cast<void>(PutInObjectFolderUVE(engine, editor, {shearParent, moved}));
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
        static_cast<void>(PutInObjectFolderUVE(engine, editor, {tinyParent}));
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(tinyParent));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SaveThenLoadScene_RoundTripsDocumentRootsWithoutSerializingEditorCamera) {
    const std::filesystem::path scenePath = "uve_editor_tests_round_trip.uvscene";
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
        // The single root is the Object; the load wrapped the pre-root save's top-level
        // entities beneath it, and the Outliner layout then moved the authored root (with its
        // saved transform and its own child) into the level's object folder.
        EXPECT_EQ(loadedRoots.front(), editor.GetDocumentObjectUVE());
        const std::vector<Scene::EntityUVE> loadedObjectChildren = services.GetSceneGraphUVE().GetChildrenUVE(
            services.GetEntityManagerUVE(), EditorUVEAccessUVE::GetObjectFolderUVE(editor));
        ASSERT_EQ(loadedObjectChildren.size(), 1U);
        const Scene::TransformComponentUVE& loadedTransform =
            services.GetEntityManagerUVE().GetComponentUVE<Scene::TransformComponentUVE>(
                loadedObjectChildren.front());
        EXPECT_EQ(loadedTransform.localPosition, rootTransform.localPosition);
        EXPECT_EQ(services.GetSceneGraphUVE()
                      .GetChildrenUVE(services.GetEntityManagerUVE(), loadedObjectChildren.front())
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gizmo.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_root.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_parented.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_rotate_safety.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scale.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_scale_safety.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_uniform_scale_offset.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_uniform_scale_floor.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection_bounds_identity.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_selection_bounds_parented.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_snapping_settings.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_snapping_commands.uvscene");
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
    const std::filesystem::path missingScenePath = "uve_editor_tests_missing.uvscene";
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
        ASSERT_EQ(roots.size(), 2U); // the Object + the surviving authored root
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_restore.uvscene", 100U, &engine);
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
        ASSERT_EQ(restoredRoots.size(), 2U); // the Object + the restored authored root
        const Scene::EntityUVE restoredAuthored =
            restoredRoots.front() == editor.GetDocumentObjectUVE() ? restoredRoots.back()
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
    // (e.g. the editor Viewport's own hidden free-look-camera proxy) are Objects just like real
    // document content (AttachTransformUVE always creates a root), but must never be swept up by
    // Play-mode's destroy/recreate snapshot cycle in StopPlayModeUVE() - before this fix, they were,
    // which left a cached EntityUVE elsewhere pointing at a destroyed entity and crashed the next
    // time it was dereferenced. See GetDocumentRootsUVE()'s own comment for the fix.
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_internal_entity.uvscene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE documentRoot = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, documentRoot, Scene::TransformComponentUVE{});

        const Scene::EntityUVE internalEntity = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, internalEntity, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::EditorInternalEntityComponentUVE>(internalEntity);

        const std::vector<Scene::EntityUVE> roots = editor.GetDocumentRootsUVE();
        ASSERT_EQ(roots.size(), 2U); // the Object + the authored document root
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
        ASSERT_EQ(rootsAfterStop.size(), 2U); // the Object + the restored authored root
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn.uvscene", 100U, &engine);
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
        Scene::SpawnPoint3DComponentUVE spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn, spawnPoint);

        // Compose reads the spawn object's WORLD pose, so the sweep must have run since attaching.
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
        EXPECT_FALSE(entityManager.GetComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn).enabled);

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
        entityManager.ForEachUVE<Scene::SpawnPoint3DComponentUVE>(
            [&restoredSpawn](const Scene::EntityUVE entity, Scene::SpawnPoint3DComponentUVE&) {
                restoredSpawn = entity;
            });
        ASSERT_NE(restoredSpawn, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(
            entityManager.GetComponentUVE<Scene::SpawnPoint3DComponentUVE>(restoredSpawn).enabled);

        editor.ShutdownUVE();
    }

    engine.Shutdown();
}

TEST(EditorUVETest, PlayModeSandbox_SpawnPointWithOffsetAndParentPlacesRespectingBothKinds) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_offset.uvscene", 100U,
                         &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

        // Spawn point object at (4, 0, 0) whose authored offset raises the player by (0, 0.5, 0);
        // one-shot false, so the point stays live through the session.
        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE spawnTransform{};
        spawnTransform.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
        AttachRootUVE(engine, spawn, spawnTransform);
        Scene::SpawnPoint3DComponentUVE spawnPoint{};
        spawnPoint.localPosition = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
        entityManager.AddComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn, spawnPoint);

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
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn).enabled);

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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_noop_a.uvscene", 100U,
                         &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // A spawn point with no player to absorb it: play still enters, and the point keeps its
        // one-shot loaded - resolution is defined as "nothing to do", never a failure.
        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, spawn, Scene::TransformComponentUVE{});
        Scene::SpawnPoint3DComponentUVE spawnPoint{};
        spawnPoint.oneShot = true;
        entityManager.AddComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn, spawnPoint);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn).enabled);
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_spawn_noop_b.uvscene", 100U,
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

TEST(EditorUVETest, PlayModeSandbox_InstantiatesDefaultPlayerAndUsesItsCamera) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectoryUVE("play_default_player");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    config.projectContentRootUVE = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";

    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), scratch / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        const auto created = editor.CreateContentCatalogueItemUVE("player", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.SetDefaultPlayerEntityUVE(created->filename()));

        const Scene::EntityUVE spawn = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE spawnTransform{};
        spawnTransform.localPosition = Math::Vector3UVE{5.0F, 1.0F, -2.0F};
        AttachRootUVE(engine, spawn, spawnTransform);
        entityManager.AddComponentUVE<Scene::SpawnPoint3DComponentUVE>(spawn, Scene::SpawnPoint3DComponentUVE{});
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        EXPECT_EQ(Scene::ResolvePossessedPlayerUVE(entityManager), Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.EnterPlayModeUVE());

        const Scene::EntityUVE player = Scene::ResolvePossessedPlayerUVE(entityManager);
        ASSERT_NE(player, Scene::kInvalidEntityUVE);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);
        const Scene::WorldTransformComponentUVE& played =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(player);
        EXPECT_NEAR(played.worldPosition.x, 5.0F, 1.0e-4F);
        EXPECT_NEAR(played.worldPosition.y, 1.0F, 1.0e-4F);
        EXPECT_NEAR(played.worldPosition.z, -2.0F, 1.0e-4F);

        const Scene::EntityUVE camera = Scene::FindPlayerCameraUVE(entityManager, player);
        ASSERT_NE(camera, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::CameraComponentUVE>(camera).current);
        const std::optional<Scene::EntityUVE> current = Scene::FindCurrentCameraEntityUVE(entityManager);
        ASSERT_TRUE(current.has_value());
        EXPECT_EQ(*current, camera);

        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_EQ(Scene::ResolvePossessedPlayerUVE(entityManager), Scene::kInvalidEntityUVE);

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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_bookmarks.uvscene", 100U, &engine);
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
    // Godot's inert annotation: the marker's authored offset+rotation compose under the object's
    // world pose, the eye lands exactly ON the marker looking along its composed -Z, and the
    // orbit inverse then hands back a target/yaw/pitch the viewport camera can hold verbatim -
    // measured here by running the camera's own forward formula back to the eye (round trip).
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_marker_focus.uvscene", 100U, &engine);
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();

        // Marker object at (3,1,-2), yawed 90 degrees about Y (-Z faces -X), local offset (0,2,4).
        const Scene::EntityUVE markerObject = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE markerTransform{};
        markerTransform.localPosition = Math::Vector3UVE{3.0F, 1.0F, -2.0F};
        markerTransform.localRotation = Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F};
        AttachRootUVE(engine, markerObject, markerTransform);
        Scene::Marker3DComponentUVE marker{};
        marker.markerName = "Boss view";
        marker.localPosition = Math::Vector3UVE{0.0F, 2.0F, 4.0F};
        entityManager.AddComponentUVE<Scene::Marker3DComponentUVE>(markerObject, marker);
        services.GetSceneGraphUVE().UpdateUVE(entityManager);

        const std::optional<Editor::EditorViewportBookmarkUVE> bookmark =
            editor.ComposeMarker3DFocusBookmarkUVE(markerObject);
        ASSERT_TRUE(bookmark.has_value());
        // Composed eye: rotation maps (0,2,4) to (4,2,0) about Y with sin90, added to the object.
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
        entityManager.GetComponentUVE<Scene::Marker3DComponentUVE>(markerObject).enabled = false;
        EXPECT_FALSE(editor.ComposeMarker3DFocusBookmarkUVE(markerObject).has_value());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_multi_selection.uvscene", 100U, &engine);
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
        ASSERT_EQ(restoredRoots.size(), 3U); // the Object + the two restored authored roots
        EXPECT_EQ(restoredRoots[0], editor.GetDocumentObjectUVE());
        EXPECT_NE(restoredRoots[1], first);
        EXPECT_NE(restoredRoots[2], second);
        // The restored selection is the authored pair (the two non-Object restored roots,
        // in their restored order); the Object itself is never part of it.
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
        EditorUVE withoutControl(engine.GetServicesUVE(), "uve_editor_tests_play_no_control.uvscene");
        withoutControl.InitUVE();
        EXPECT_FALSE(withoutControl.EnterPlayModeUVE());
        withoutControl.ShutdownUVE();

        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_play_empty.uvscene", 100U, &engine);
        editor.InitUVE();
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_EQ(editor.GetPlayModeStateUVE(), EditorPlayModeStateUVE::Edit);
        // One-root documents: an otherwise-empty document holds exactly the Object.
        ASSERT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], editor.GetDocumentObjectUVE());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_commit.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_cancel.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_conflict.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_guards.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_noop.uvscene");
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_gesture_snap.uvscene");
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

TEST(EditorUVETest, RetargetPreviewUVE_PutsTheSceneAsideAndBringsItBack) {
    Core::EngineConfigUVE config = MakeEditorTestConfigUVE();
    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_retarget_preview.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE mine = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(mine, Scene::kInvalidEntityUVE);
        const auto namesInWorld = [&entityManager] {
            std::vector<std::string> names;
            entityManager.ForEachUVE<Scene::NameComponentUVE>(
                [&names](const Scene::EntityUVE, const Scene::NameComponentUVE& name) { names.push_back(name.name); });
            return names;
        };
        const auto has = [&namesInWorld](const std::string& name) {
            const std::vector<std::string> names = namesInWorld();
            return std::ranges::find(names, name) != names.end();
        };
        ASSERT_TRUE(editor.IsSceneDirtyUVE());

        editor.OpenRetargetWindowUVE({});
        ASSERT_TRUE(editor.IsRetargetWindowOpenUVE());
        ASSERT_TRUE(EditorUVEAccessUVE::IsRetargetPreviewActiveUVE(editor));
        // The scene is set aside, and nothing of the preview is authored into it.
        EXPECT_FALSE(entityManager.IsAliveUVE(mine));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.SaveSceneUVE());
        EXPECT_FALSE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D), Scene::kInvalidEntityUVE);

        // The world: a sun and the humanoid with a colour for each of its bones (the sky, the ground
        // and the floor are the viewport's studio view, not objects).
        EditorUVEAccessUVE::RebuildRetargetPreviewUVE(editor, RetargetPlanUVE{}, {});
        for (const char* const name : {"Retarget Preview", "Sun", "Figures", "Humanoid", "Humanoid Skeleton"}) {
            EXPECT_TRUE(has(name)) << name;
        }
        EXPECT_FALSE(has("Character")) << "no character was picked";
        const Scene::EntityUVE humanoid = EditorUVEAccessUVE::GetRetargetSourceSkeletonUVE(editor);
        ASSERT_NE(humanoid, Scene::kInvalidEntityUVE);
        const auto& bones = entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(humanoid).bones;
        EXPECT_EQ(bones.size(), 162U);
        EXPECT_EQ(EditorUVEAccessUVE::GetRetargetSourceColourCountUVE(editor), bones.size());
        // A rebuild replaces the world, it does not pile a second one on.
        EditorUVEAccessUVE::RebuildRetargetPreviewUVE(editor, RetargetPlanUVE{}, {});
        const std::vector<std::string> names = namesInWorld();
        EXPECT_EQ(std::ranges::count(names, "Sun"), 1);

        editor.CloseRetargetWindowUVE();
        EXPECT_FALSE(editor.IsRetargetWindowOpenUVE());
        EXPECT_FALSE(EditorUVEAccessUVE::IsRetargetPreviewActiveUVE(editor));
        EXPECT_FALSE(has("Retarget Preview"));
        EXPECT_FALSE(has("Humanoid"));
        EXPECT_TRUE(has("Object3D")) << "the scene is back";
        EXPECT_TRUE(editor.IsSceneDirtyUVE()) << "as unsaved as it was";
        EXPECT_TRUE(editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D) != Scene::kInvalidEntityUVE);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests

namespace UVE::Editor::Tests {
namespace {

TEST(EditorUVETest, ObjectInspectorUVE_ShowsExactlyTheObjectSectionInOrder) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object_inspector.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        // No Name, Hierarchy or Transform; Thread Group lives inside Process.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, root),
                  (std::vector<std::string>{"process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));

        // On an object without Process, Thread Group still has a section of its own.
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE object = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, object, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::ThreadGroupComponentUVE>(object, Scene::ThreadGroupComponentUVE{});
        const std::vector<std::string> ids = EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, object);
        EXPECT_NE(std::find(ids.begin(), ids.end(), "thread-group"), ids.end());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScriptSlotUVE_NewUVScriptOpensATextEditorThatChecksAsYouType) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_uvscript_slot.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("hero"));

        ASSERT_TRUE(editor.CreateUVScriptForSelectedEntityUVE());
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath;
        EXPECT_EQ(path.rfind("scripts/hero", 0), 0U);
        EXPECT_TRUE(path.ends_with(".uvs"));
        EXPECT_TRUE(editor.DescribeScriptAssetProblemUVE(path).empty());
        EXPECT_TRUE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        const EditorUVE::UVScriptDocumentUVE& document = *editor.GetOpenUVScriptUVE();
        EXPECT_EQ(document.path, path);
        EXPECT_EQ(document.text.rfind("entity hero : ", 0), 0U);
        EXPECT_TRUE(document.diagnostics.empty()); // The template compiles against the object.
        EXPECT_FALSE(document.IsDirtyUVE());
        EXPECT_FALSE(editor.CreateUVScriptForSelectedEntityUVE()); // The slot is filled now.

        // A mistake is reported while typing, with its line.
        editor.SetOpenUVScriptTextUVE("on tick(dt):\n    nope += 1\n");
        ASSERT_FALSE(editor.GetOpenUVScriptUVE()->diagnostics.empty());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->diagnostics.front().at.line, 2U);

        // Fixed and saved: the file holds the new text and nothing is unsaved.
        const std::string fixed = "var ticks = 0\n\non tick(dt):\n    ticks += 1\n";
        editor.SetOpenUVScriptTextUVE(fixed);
        EXPECT_TRUE(editor.GetOpenUVScriptUVE()->diagnostics.empty());
        EXPECT_TRUE(editor.GetOpenUVScriptUVE()->IsDirtyUVE());
        ASSERT_TRUE(editor.SaveOpenUVScriptUVE());
        EXPECT_FALSE(editor.GetOpenUVScriptUVE()->IsDirtyUVE());
        std::ifstream file(path, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), fixed);

        // Close returns to the scene; opening the object's script comes back to the text editor.
        editor.CloseOpenUVScriptUVE();
        EXPECT_FALSE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_FALSE(EditorUVEAccessUVE::IsScriptingWorkspaceActiveUVE(editor));
        ASSERT_TRUE(editor.OpenScriptGraphForEntityUVE(root));
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        EXPECT_EQ(editor.GetOpenUVScriptUVE()->text, fixed);

        editor.CloseOpenUVScriptUVE();
        std::error_code error;
        std::filesystem::remove(path, error);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ScriptExportsUVE_ShowTheScriptsExportsAndStoreTheObjectsValues) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_script_exports.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.SetSelectedEntityNameUVE("exporter"));
        EXPECT_TRUE(editor.GetSelectedScriptExportsUVE().empty()); // No script yet.

        ASSERT_TRUE(editor.CreateUVScriptForSelectedEntityUVE());
        editor.SetOpenUVScriptTextUVE("export speed = 6.0\nexport jumps = 2\nexport label = \"hero\"\n"
                                      "export offset = vec3(0.0, 1.0, 0.0)\nvar hidden = 1\n");
        ASSERT_TRUE(editor.SaveOpenUVScriptUVE());
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath;
        editor.CloseOpenUVScriptUVE();

        // Exports only, in declaration order, at the script's defaults.
        std::vector<EditorUVE::ScriptExportRowUVE> rows = editor.GetSelectedScriptExportsUVE();
        ASSERT_EQ(rows.size(), 4U);
        EXPECT_EQ(rows[0].name, "speed");
        EXPECT_EQ(rows[0].valueText, "6.0");
        EXPECT_EQ(rows[1].valueText, "2");
        EXPECT_EQ(rows[2].valueText, "hero");
        EXPECT_EQ(rows[3].valueText, "(0.0, 1.0, 0.0)");
        EXPECT_FALSE(rows[0].overridden);

        // A value of the right type is stored in its canonical form; a wrong one is refused.
        ASSERT_TRUE(editor.SetSelectedScriptExportUVE("speed", "9"));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).exportValues.at("speed"), "9.0");
        EXPECT_FALSE(editor.SetSelectedScriptExportUVE("jumps", "lots"));
        EXPECT_FALSE(editor.SetSelectedScriptExportUVE("hidden", "3"));
        rows = editor.GetSelectedScriptExportsUVE();
        EXPECT_TRUE(rows[0].overridden);
        EXPECT_EQ(rows[0].valueText, "9.0");
        EXPECT_EQ(rows[0].defaultText, "6.0");

        // One undo step per edit; resetting drops the stored value.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).exportValues.empty());
        ASSERT_TRUE(editor.RedoUVE());
        ASSERT_TRUE(editor.SetSelectedScriptExportUVE("speed", std::nullopt));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(root).exportValues.empty());

        std::error_code error;
        std::filesystem::remove(path, error);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ObjectMetadataUVE_EveryEditIsOneUndoStep) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object_metadata.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        editor.SelectEntityUVE(root);
        const auto entries = [&] { return entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(root).entries; };

        EXPECT_FALSE(editor.AddSelectedObjectMetadataUVE("2bad", Core::VariantUVE::MakeIntUVE(1)));
        ASSERT_TRUE(editor.AddSelectedObjectMetadataUVE("charges", Core::VariantUVE::MakeFloatUVE(3.5)));
        EXPECT_FALSE(editor.AddSelectedObjectMetadataUVE("charges", Core::VariantUVE::MakeIntUVE(1)));
        ASSERT_TRUE(editor.RenameSelectedObjectMetadataUVE("charges", "ammo"));
        // 3.5 -> int loses the fraction: refused unless the author confirms.
        EXPECT_FALSE(editor.ChangeSelectedObjectMetadataTypeUVE("ammo", Core::VariantTypeUVE::Int, false));
        ASSERT_TRUE(editor.ChangeSelectedObjectMetadataTypeUVE("ammo", Core::VariantTypeUVE::Int, true));
        EXPECT_EQ(entries().front().value.GetTypeUVE(), Core::VariantTypeUVE::Int);
        ASSERT_TRUE(editor.RemoveSelectedObjectMetadataUVE("ammo"));
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

TEST(EditorUVETest, RenderInstanceChildInspectorUVE_IsOwnSectionThenBasesThenObject3DThenObject) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_render_instance_inspector.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE decal = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, decal, Scene::TransformComponentUVE{});
        Scene::ApplyDecal3DObjectDefinitionUVE(entityManager, decal, Scene::Decal3DObjectDefinitionUVE{});
        const Scene::EntityUVE fog = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, fog, Scene::TransformComponentUVE{});
        Scene::ApplyFogVolume3DObjectDefinitionUVE(entityManager, fog, Scene::FogVolume3DObjectDefinitionUVE{});
        const auto expected = [](const std::string& own) {
            return std::vector<std::string>{own,       "render-instance",       "transform",      "visibility",
                                            "process", "physics-interpolation", "auto-translate", "editor-description",
                                            "script",  "object-metadata"};
        };
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, decal), expected("decal-3d"));
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, fog), expected("fog-volume-3d"));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SunAndWorldEnvironmentInspectorsUVE_FollowTheirClassChains) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_sun_environment_inspector.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        // DirectionalLight3D: its own section, then LightEmitter3D, RenderInstance3D, Object3D, Object.
        const Scene::EntityUVE sun = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
        ASSERT_NE(sun, Scene::kInvalidEntityUVE);
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, sun), Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sun).shadowEnabled)
            << "a sun casts shadows by default";
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, sun),
                  (std::vector<std::string>{"directional-light-3d", "light-emitter", "render-instance", "transform",
                                            "visibility", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        // WorldEnvironment: a pure Object - its own section, then Object's; no Transform, no Visibility.
        const Scene::EntityUVE environment =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
        ASSERT_NE(environment, Scene::kInvalidEntityUVE);
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(environment));
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, environment),
                  (std::vector<std::string>{"world-environment", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

// The 16 kinds that carry a component but no ObjectDefinition of their own used to be named through
// EditorEntityKindUVE::Empty, so every one of them appeared in the Outliner as "Object3D". This
// locks each to its own registry displayName, and locks the uniqueness rule that keeps a second one
// from colliding with the first.
TEST(EditorUVETest, ComponentOnlySceneObjectsUVE_AreNamedForTheirOwnKindNotObject3D) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_component_only_names.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        constexpr std::array<Scene::Objects::SceneObjectKindUVE, 16> kComponentOnlyKinds{
            Scene::Objects::SceneObjectKindUVE::RayCast3D,
            Scene::Objects::SceneObjectKindUVE::NavMeshVolume3D,
            Scene::Objects::SceneObjectKindUVE::NavSeeker3D,
            Scene::Objects::SceneObjectKindUVE::BoneAttachment3D,
            Scene::Objects::SceneObjectKindUVE::Marker3D,
            Scene::Objects::SceneObjectKindUVE::Hitbox3D,
            Scene::Objects::SceneObjectKindUVE::Hurtbox3D,
            Scene::Objects::SceneObjectKindUVE::Projectile3D,
            Scene::Objects::SceneObjectKindUVE::InteractionArea3D,
            Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D,
            Scene::Objects::SceneObjectKindUVE::LODGroup3D,
            Scene::Objects::SceneObjectKindUVE::Occluder3D,
            Scene::Objects::SceneObjectKindUVE::VisibilityRegion3D,
            Scene::Objects::SceneObjectKindUVE::SpawnPoint3D,
            Scene::Objects::SceneObjectKindUVE::LevelStreamer3D,
            Scene::Objects::SceneObjectKindUVE::WorldPartition3D,
        };

        for (const Scene::Objects::SceneObjectKindUVE kind : kComponentOnlyKinds) {
            const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
                Scene::Objects::FindSceneObjectDescriptorUVE(kind);
            ASSERT_NE(descriptor, nullptr);

            const Scene::EntityUVE first = editor.CreateDocumentSceneObjectUVE(kind);
            ASSERT_NE(first, Scene::kInvalidEntityUVE) << descriptor->displayName;
            EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(first).name, descriptor->displayName)
                << "a freshly added " << descriptor->displayName << " must not be called Object3D";

            // Same rule the 29 kinds with an ObjectDefinition follow: the second one is suffixed
            // rather than given a duplicate name.
            const Scene::EntityUVE second = editor.CreateDocumentSceneObjectUVE(kind);
            ASSERT_NE(second, Scene::kInvalidEntityUVE) << descriptor->displayName;
            EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(second).name,
                      std::string{descriptor->displayName} + " 2")
                << "the second " << descriptor->displayName << " must not collide with the first";
        }
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, SurfaceInstanceChildInspectorUVE_IsOwnSectionThenSurfaceRenderObject3DObject) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_surface_instance_inspector.uvscene");
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
                                            "object-metadata"};
        };
        const Scene::EntityUVE mesh = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyMeshInstance3DObjectDefinitionUVE(entityManager, entity, Scene::MeshInstance3DObjectDefinitionUVE{});
        });
        const Scene::EntityUVE box = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyBoxMesh3DObjectDefinitionUVE(entityManager, entity, Scene::BoxMesh3DObjectDefinitionUVE{});
        });
        const Scene::EntityUVE sphere = create([&](const Scene::EntityUVE entity) {
            Scene::ApplySphereMesh3DObjectDefinitionUVE(entityManager, entity, Scene::SphereMesh3DObjectDefinitionUVE{});
        });
        const Scene::EntityUVE plane = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyPlaneMesh3DObjectDefinitionUVE(entityManager, entity, Scene::PlaneMesh3DObjectDefinitionUVE{});
        });
        const Scene::EntityUVE particles = create([&](const Scene::EntityUVE entity) {
            Scene::ApplyParticleEmitter3DObjectDefinitionUVE(entityManager, entity,
                                                           Scene::ParticleEmitter3DObjectDefinitionUVE{});
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_character_body_inspector.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE body = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, body, Scene::TransformComponentUVE{});
        Scene::ApplyCharacter3DObjectDefinitionUVE(entityManager, body, Scene::Character3DObjectDefinitionUVE{});
        // Character3D > SolidBody3D > PhysicsObject3D > Object3D > Object. The capsule, layer and mask
        // are drawn inside PhysicsObject3D, so the collider has no section of its own.
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, body),
                  (std::vector<std::string>{"character-controller", "solid-body", "physics-object", "transform",
                                            "visibility", "process", "physics-interpolation", "auto-translate",
                                            "editor-description", "script", "object-metadata"}));
        // A primitive still draws its collider in its own section, not in PhysicsObject3D's.
        const Scene::EntityUVE box = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, box, Scene::TransformComponentUVE{});
        Scene::ApplyBoxMesh3DObjectDefinitionUVE(entityManager, box, Scene::BoxMesh3DObjectDefinitionUVE{});
        const std::vector<std::string> boxIds = EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, box);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "collider"), 0);
        EXPECT_EQ(std::count(boxIds.begin(), boxIds.end(), "physics-object"), 0);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, FolderUVE_GroupsObjectsWithoutMovingThem) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_folder.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE cube = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        ASSERT_NE(cube, Scene::kInvalidEntityUVE);
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(cube).localPosition = Math::Vector3UVE{3.0F, 0.0F, 0.0F};
        editor.SelectEntityUVE(editor.GetDocumentObjectUVE());
        const Scene::EntityUVE folder = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Folder);
        ASSERT_NE(folder, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::FolderComponentUVE>(folder));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(folder));
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, folder), Scene::Objects::SceneObjectKindUVE::Folder);
        // Moving the cube into the folder keeps it where it was.
        editor.SelectEntityUVE(cube);
        ASSERT_TRUE(editor.ReparentSelectedEntityUVE(folder));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(cube).localPosition.x, 3.0F);
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, folder), (std::vector<std::string>{"Object"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, InspectorHeadersUVE_SpellOutTheClassChain) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_chain_headers.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE body =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Character3D);
        const Scene::EntityUVE player =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationSequencer);
        ASSERT_NE(body, Scene::kInvalidEntityUVE);
        ASSERT_NE(player, Scene::kInvalidEntityUVE);
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, body),
                  (std::vector<std::string>{"Character3D", "SolidBody3D", "PhysicsObject3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, player),
                  (std::vector<std::string>{"AnimationSequencer", "AnimationDriver", "Object"}));
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, editor.GetDocumentObjectUVE()),
                  (std::vector<std::string>{"Object"}));

        const Scene::EntityUVE occluder =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Occluder3D);
        const Scene::EntityUVE fog =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::FogVolume3D);
        const Scene::EntityUVE sun =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
        ASSERT_NE(occluder, Scene::kInvalidEntityUVE);
        ASSERT_NE(fog, Scene::kInvalidEntityUVE);
        ASSERT_NE(sun, Scene::kInvalidEntityUVE);
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, occluder),
                  (std::vector<std::string>{"Occluder3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, fog),
                  (std::vector<std::string>{"FogVolume3D", "RenderInstance3D", "Object3D", "Object"}));
        EXPECT_EQ(EditorUVEAccessUVE::GetInspectorGroupHeadersUVE(editor, sun),
                  (std::vector<std::string>{"DirectionalLight3D", "LightEmitter3D", "RenderInstance3D", "Object3D",
                                            "Object"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AnimationObjectsUVE_InspectorIsTheirOwnSectionThenTheObjectSection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_animation_inspector.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE player =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationSequencer);
        const Scene::EntityUVE tree = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationGraph);
        ASSERT_NE(player, Scene::kInvalidEntityUVE);
        ASSERT_NE(tree, Scene::kInvalidEntityUVE);
        // AnimationSequencer > AnimationDriver > Object: a pure Object, no Transform, no Visibility.
        const std::vector<std::string> objectSection{"animation-mixer", "process", "physics-interpolation",
                                                   "auto-translate", "editor-description", "script", "object-metadata"};
        std::vector<std::string> expected{"animation-player"};
        expected.insert(expected.end(), objectSection.begin(), objectSection.end());
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, player), expected);
        expected.front() = "animation-tree";
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, tree), expected);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, AnimationObjectsUVE_APureObjectReparentsAndUndoes) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_animation_reparent.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE player =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationSequencer);
        editor.SelectEntityUVE(editor.GetDocumentObjectUVE());
        const Scene::EntityUVE door = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_color_session.uvscene");
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

        // Moving to another object while a session is open finishes it on the object it began on.
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_color_prefs.uvscene");
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
        Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
        EXPECT_EQ(settings.GetIntUVE("editor.colorPicker.saved.count", -1), 2);
        EXPECT_EQ(settings.GetStringUVE("editor.colorPicker.saved.0", ""), "#FF8000FF");
        EXPECT_EQ(settings.GetStringUVE("editor.colorPicker.saved.1", ""), "#FF004080");
        editor.ShutdownUVE();
    }
    {
        EditorUVE reloaded(engine.GetServicesUVE(), "uve_editor_tests_color_prefs_reload.uvscene");
        reloaded.InitUVE();
        const Editor::ColorPickerPreferencesUVE& preferences = reloaded.GetColorPickerPreferencesUVE();
        EXPECT_FALSE(preferences.advancedOpen);
        ASSERT_EQ(preferences.saved.size(), 2U);
        EXPECT_EQ(Editor::FormatColorHexUVE(preferences.saved[0], true), "#FF8000FF");
        EXPECT_EQ(Editor::FormatColorHexUVE(preferences.saved[1], true), "#FF004080");
        EXPECT_EQ(preferences.recents.size(), Editor::kMaxRecentColorsUVE);
        for (std::size_t index = 0U; index < Editor::kMaxRecentColorsUVE; ++index) {
            const Editor::EditorColorUVE expected{static_cast<float>(index) / 15.0F, 0.0F, 0.0F, 1.0F};
            EXPECT_EQ(Editor::FormatColorHexUVE(preferences.recents[index], true),
                      Editor::FormatColorHexUVE(expected, true)) << index;
        }
        reloaded.ShutdownUVE();
    }
    // A stored entry that is not a colour is skipped; the rest of the list still loads.
    engine.GetServicesUVE().GetConfigManagerUVE().SetStringUVE("editor.colorPicker.saved.0", "not a colour");
    {
        EditorUVE corrupt(engine.GetServicesUVE(), "uve_editor_tests_color_prefs_corrupt.uvscene");
        corrupt.InitUVE();
        ASSERT_EQ(corrupt.GetColorPickerPreferencesUVE().saved.size(), 1U);
        EXPECT_EQ(Editor::FormatColorHexUVE(corrupt.GetColorPickerPreferencesUVE().saved[0], true), "#FF004080");
        corrupt.ShutdownUVE();
    }
    // Invalid entries do not consume the saved-colour cap: the legacy loader inspected up to 64
    // raw entries before applying the 24 valid-colour limit.
    Config::IConfigManagerUVE& settings = engine.GetServicesUVE().GetConfigManagerUVE();
    settings.SetIntUVE("editor.colorPicker.saved.count", 26);
    for (std::int64_t index = 0; index < 25; ++index) {
        settings.SetStringUVE("editor.colorPicker.saved." + std::to_string(index), "not a colour");
    }
    settings.SetStringUVE("editor.colorPicker.saved.25", "#123456");
    {
        EditorUVE invalidPrefix(engine.GetServicesUVE(), "uve_editor_tests_color_prefs_invalid_prefix.uvscene");
        invalidPrefix.InitUVE();
        ASSERT_EQ(invalidPrefix.GetColorPickerPreferencesUVE().saved.size(), 1U);
        EXPECT_EQ(Editor::FormatColorHexUVE(invalidPrefix.GetColorPickerPreferencesUVE().saved[0], true),
                  "#123456FF");
        invalidPrefix.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove(config.settingsFilePath);
}

TEST(EditorUVETest, InspectorTransformDragUVE_IsOneUndoStepAndCanBeCancelled) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_transform_drag.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE object = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, object, Scene::TransformComponentUVE{});
        editor.SelectEntityUVE(object);
        const auto live = [&] { return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(object); };

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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_inspector_metadata_drag.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE root = editor.GetDocumentObjectUVE();
        editor.SelectEntityUVE(root);
        ASSERT_TRUE(editor.AddSelectedObjectMetadataUVE("speed", Core::VariantUVE::MakeFloatUVE(1.0)));
        const auto speed = [&] {
            return entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(root).entries.front().value;
        };

        for (int frame = 1; frame <= 20; ++frame) {
            ASSERT_TRUE(editor.PreviewSelectedObjectMetadataValueUVE(
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
        ASSERT_TRUE(editor.PreviewSelectedObjectMetadataValueUVE("speed", Core::VariantUVE::MakeFloatUVE(5.0)));
        ASSERT_TRUE(editor.SetSelectedObjectMetadataValueUVE("speed", Core::VariantUVE::MakeFloatUVE(9.0)));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(5.0));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(speed(), Core::VariantUVE::MakeFloatUVE(1.0));
        ASSERT_TRUE(editor.UndoUVE()); // the add
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(root).entries.empty());
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_mesh_rule.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::MeshInstance3D);
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

TEST(EditorUVETest, MultiObjectEditingUVE_PropertySetAppliesToAllHoldersAsOneUndoStep) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_edit.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE first =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        const Scene::EntityUVE second =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        const Scene::EntityUVE plain =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        ASSERT_NE(first, Scene::kInvalidEntityUVE);
        ASSERT_NE(second, Scene::kInvalidEntityUVE);
        ASSERT_NE(plain, Scene::kInvalidEntityUVE);
        const Core::TypeMetadataEntryUVE* const entry =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::PrimitiveMeshComponentUVE)));
        ASSERT_NE(entry, nullptr);
        const auto property = [&](const std::string& name) {
            return *std::find_if(entry->properties.begin(), entry->properties.end(),
                                 [&](const auto& candidate) { return candidate.name == name; });
        };
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const auto baseColor = [&](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).baseColor;
        };
        // All three gathered with the non-holder left active: the active object shapes the
        // Inspector, never the write.
        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        editor.ToggleEntitySelectionUVE(plain);
        ASSERT_FALSE(editor.HasSingleDocumentSelectionUVE());
        EXPECT_FALSE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));

        const Math::Vector3UVE red{1.0F, 0.0F, 0.0F};
        EXPECT_TRUE(
            EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("baseColor"), &red));
        EXPECT_EQ(baseColor(first), red);
        EXPECT_EQ(baseColor(second), red);
        // Already there on every holder: refused, recording nothing.
        EXPECT_FALSE(
            EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("baseColor"), &red));
        EXPECT_FALSE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));

        // One undo step rewinds both holders...
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_NE(baseColor(first), red);
        EXPECT_NE(baseColor(second), red);
        // ...and one redo lands the shared value again.
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(baseColor(first), red);
        EXPECT_EQ(baseColor(second), red);
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, MultiObjectEditingUVE_MixedMarksDisagreementAndRevertRestoresAll) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_multi_mixed.uvscene");
        editor.InitUVE();
        const Scene::EntityUVE first =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        const Scene::EntityUVE second =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        ASSERT_NE(first, Scene::kInvalidEntityUVE);
        ASSERT_NE(second, Scene::kInvalidEntityUVE);
        const Core::TypeMetadataEntryUVE* const entry =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::PrimitiveMeshComponentUVE)));
        ASSERT_NE(entry, nullptr);
        const auto property = [&](const std::string& name) {
            return *std::find_if(entry->properties.begin(), entry->properties.end(),
                                 [&](const auto& candidate) { return candidate.name == name; });
        };
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const auto baseColor = [&](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).baseColor;
        };
        // Each box given its own color while single-selected.
        editor.SelectEntityUVE(first);
        const Math::Vector3UVE red{1.0F, 0.0F, 0.0F};
        ASSERT_TRUE(
            EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("baseColor"), &red));
        editor.SelectEntityUVE(second);
        const Math::Vector3UVE blue{0.0F, 0.0F, 1.0F};
        ASSERT_TRUE(
            EditorUVEAccessUVE::SetSelectedComponentPropertyUVE(editor, *entry, property("baseColor"), &blue));
        // One object alone is never mixed, however it was colored.
        editor.SelectEntityUVE(first);
        EXPECT_FALSE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));
        // Together the row admits they disagree...
        editor.ToggleEntitySelectionUVE(second);
        EXPECT_TRUE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));
        // ...one revert puts both back at the default, and one undo brings both colors back.
        EXPECT_TRUE(EditorUVEAccessUVE::ResetSelectedComponentPropertyUVE(editor, *entry, property("baseColor")));
        EXPECT_NE(baseColor(first), red);
        EXPECT_NE(baseColor(second), blue);
        EXPECT_FALSE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(baseColor(first), red);
        EXPECT_EQ(baseColor(second), blue);
        EXPECT_TRUE(EditorUVEAccessUVE::IsComponentPropertyMixedUVE(editor, *entry, property("baseColor")));
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_model_auto_import.uvscene");
        editor.InitUVE();
        const std::filesystem::path imported = EditorUVEAccessUVE::GetImportedModelPathUVE(editor, "Models/tri.obj");
        for (int frame = 0; frame < 8 && !std::filesystem::exists(imported); ++frame) {
            editor.TickUVE();
            engine.TickFrameUVE();
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
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_skeleton.uvscene");
        editor.InitUVE();
        editor.TickUVE(); // first project refresh
        const Scene::EntityUVE skeleton = editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Skeleton3D);
        ASSERT_NE(skeleton, Scene::kInvalidEntityUVE);
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        // A Object3D child, empty until pointed at a rig, and named for what it is.
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(skeleton).name, "Skeleton3D");
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton).bones.empty());
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, skeleton),
                  (std::vector<std::string>{"skeleton-3d", "transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "object-metadata"}));

        // A model with no armature is refused and the object stays as it was.
        EXPECT_FALSE(EditorUVEAccessUVE::BindSelectedSkeletonSourceUVE(editor, "Characters/rock.gltf"));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton).bones.empty());

        ASSERT_TRUE(EditorUVEAccessUVE::BindSelectedSkeletonSourceUVE(editor, "Characters/hero.gltf"));
        const Scene::Skeleton3DComponentUVE& bound =
            entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton);
        EXPECT_EQ(bound.skeletonAssetPath, "Characters/hero.gltf");
        ASSERT_EQ(bound.bones.size(), 2U);
        EXPECT_EQ(bound.bones[0].name, "Hips");
        EXPECT_EQ(bound.bones[1].parentIndex, 0);
        // One undo step takes the object back to empty.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton).bones.empty());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeleton).skeletonAssetPath.empty());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
    std::filesystem::remove_all("uve_editor_tests_skeleton_derived");
}

TEST(EditorUVETest, Object3DInspectorUVE_IsTransformVisibilityAndTheObjectSection) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_object3d_inspector.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE object = entityManager.CreateEntityUVE();
        AttachRootUVE(engine, object, Scene::TransformComponentUVE{});
        Scene::ApplyObject3DObjectDefinitionUVE(entityManager, object, Scene::Object3DObjectDefinitionUVE{});
        EXPECT_EQ(EditorUVEAccessUVE::GetEligibleInspectorDrawerIdsUVE(editor, object),
                  (std::vector<std::string>{"transform", "visibility", "process", "physics-interpolation",
                                            "auto-translate", "editor-description", "script", "object-metadata"}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ChangeDocumentSceneObjectKindUVE_BoxToSphereKeepsSharedValuesAndRetags) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_change_type.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE entity =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        ASSERT_NE(entity, Scene::kInvalidEntityUVE);

        // Author values the change must carry: a resized collider and a tinted primitive.
        entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).halfExtents =
            Math::Vector3UVE{2.0F, 3.0F, 4.0F};
        entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).baseColor =
            Math::Vector3UVE{1.0F, 0.0F, 0.0F};

        ASSERT_TRUE(editor.ChangeDocumentSceneObjectKindUVE(
            entity, Scene::Objects::SceneObjectKindUVE::SphereMesh3D));
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, entity),
                  Scene::Objects::SceneObjectKindUVE::SphereMesh3D);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind,
                  Scene::PrimitiveMeshKindUVE::UVSphere);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{2.0F, 3.0F, 4.0F}));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).baseColor,
                  (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, entity),
                  Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind,
                  Scene::PrimitiveMeshKindUVE::Cube);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{2.0F, 3.0F, 4.0F}));

        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, entity),
                  Scene::Objects::SceneObjectKindUVE::SphereMesh3D);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind,
                  Scene::PrimitiveMeshKindUVE::UVSphere);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{2.0F, 3.0F, 4.0F}));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(EditorUVETest, ChangeDocumentSceneObjectKindUVE_RefusesSameKindStructuralKindsAndStaleEntities) {
    Core::EngineCoreUVE engine(MakeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_editor_tests_change_type_refusals.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE box =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D);
        ASSERT_NE(box, Scene::kInvalidEntityUVE);
        EXPECT_FALSE(
            editor.ChangeDocumentSceneObjectKindUVE(box, Scene::Objects::SceneObjectKindUVE::BoxMesh3D));
        EXPECT_FALSE(
            editor.ChangeDocumentSceneObjectKindUVE(box, Scene::Objects::SceneObjectKindUVE::Folder));
        EXPECT_FALSE(
            editor.ChangeDocumentSceneObjectKindUVE(box, Scene::Objects::SceneObjectKindUVE::Viewport));
        EXPECT_FALSE(
            editor.ChangeDocumentSceneObjectKindUVE(box, Scene::Objects::SceneObjectKindUVE::Object));
        // A refused change mutates nothing, not even the tag.
        EXPECT_EQ(Scene::ResolveSceneObjectKindUVE(entityManager, box),
                  Scene::Objects::SceneObjectKindUVE::BoxMesh3D);

        const Scene::EntityUVE folder =
            editor.CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Folder);
        ASSERT_NE(folder, Scene::kInvalidEntityUVE);
        EXPECT_FALSE(
            editor.ChangeDocumentSceneObjectKindUVE(folder, Scene::Objects::SceneObjectKindUVE::BoxMesh3D));

        EXPECT_FALSE(editor.ChangeDocumentSceneObjectKindUVE(Scene::kInvalidEntityUVE,
                                                             Scene::Objects::SceneObjectKindUVE::BoxMesh3D));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
