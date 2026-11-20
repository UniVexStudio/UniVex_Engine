// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "entity.h"
#include "i_entity_manager.h"
#include "terrain_3d.h"
#include "transform_component.h"
#include "vector3.h"

namespace UVE::Editor {

// ---------------------------------------------------------------------------
// 1. Content Browser Asset Dependency Index (forward & reverse references)
// ---------------------------------------------------------------------------
class EditorAssetDependencyIndexUVE final {
public:
    /// Scans `projectRoot` for project assets (`.uvscene`, `.uvmaterial`, `.uvprefab`,
    /// `.uvmatgraph`, `.uvanim`, `.uvfx`, `.json`) and indexes forward + reverse references.
    void RebuildFromProjectUVE(const std::filesystem::path& projectRoot);

    void RegisterEdgeUVE(const std::string& fromAsset, const std::string& toAsset);
    void ClearUVE() noexcept;

    [[nodiscard]] std::vector<std::string> GetDependenciesUVE(const std::string& assetPath) const;
    [[nodiscard]] std::vector<std::string> GetReferencersUVE(const std::string& assetPath) const;
    [[nodiscard]] std::vector<std::string> FindUnusedAssetsUVE(
        const std::vector<std::string>& allAssets) const;

private:
    std::unordered_map<std::string, std::vector<std::string>> m_forward;
    std::unordered_map<std::string, std::vector<std::string>> m_reverse;
};

// ---------------------------------------------------------------------------
// 2. Multi-Scene / Additive Sub-Scene Editing Session
// ---------------------------------------------------------------------------
struct EditorSubSceneEntryUVE final {
    std::string scenePath;
    bool loaded = true;
    bool visible = true;
    bool dirty = false;
    bool activeForEdit = false;
};

class EditorMultiSceneSessionUVE final {
public:
    void SetPrimarySceneUVE(std::string scenePath);
    [[nodiscard]] const std::string& PrimarySceneUVE() const noexcept { return m_primaryScene; }

    bool AddSubSceneUVE(std::string scenePath);
    bool RemoveSubSceneUVE(std::string_view scenePath);
    bool SetSubSceneLoadedUVE(std::string_view scenePath, bool loaded);
    bool SetSubSceneVisibleUVE(std::string_view scenePath, bool visible);
    bool SetActiveEditSceneUVE(std::string_view scenePath);

    [[nodiscard]] const std::string& ActiveEditSceneUVE() const noexcept { return m_activeScene; }
    [[nodiscard]] const std::vector<EditorSubSceneEntryUVE>& SubScenesUVE() const noexcept {
        return m_subScenes;
    }

private:
    std::string m_primaryScene;
    std::string m_activeScene;
    std::vector<EditorSubSceneEntryUVE> m_subScenes;
};

// ---------------------------------------------------------------------------
// 3. Prefab Override Diff / Apply / Revert Workflow
// ---------------------------------------------------------------------------
struct EditorPrefabPropertyDiffUVE final {
    std::string propertyName;
    std::string prefabValue;
    std::string instanceValue;
};

struct EditorPrefabTransformSnapshotUVE final {
    std::string name;
    Scene::TransformComponentUVE transform{};
};

[[nodiscard]] std::vector<EditorPrefabPropertyDiffUVE> DiffPrefabInstanceUVE(
    const EditorPrefabTransformSnapshotUVE& prefabSource,
    const EditorPrefabTransformSnapshotUVE& liveInstance);

void ApplyInstanceOverridesToPrefabUVE(const EditorPrefabTransformSnapshotUVE& liveInstance,
                                       EditorPrefabTransformSnapshotUVE& prefabSource);

void RevertInstanceToPrefabUVE(const EditorPrefabTransformSnapshotUVE& prefabSource,
                               EditorPrefabTransformSnapshotUVE& liveInstance);

// ---------------------------------------------------------------------------
// 4. GPU Profiler, Render-Graph Pass & Memory Breakdown Snapshot
// ---------------------------------------------------------------------------
struct EditorGpuPassTimingUVE final {
    std::string passName;
    float gpuMs = 0.0F;
    std::uint32_t drawCalls = 0U;
    std::uint32_t triangles = 0U;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

struct EditorMemorySubsystemUVE final {
    std::string subsystemName;
    std::uint64_t allocatedBytes = 0U;
    std::uint32_t resourceCount = 0U;
};

struct EditorFrameDebugSnapshotUVE final {
    std::uint64_t frameIndex = 0U;
    float totalCpuMs = 0.0F;
    float totalGpuMs = 0.0F;
    std::vector<EditorGpuPassTimingUVE> gpuPasses;
    std::vector<EditorMemorySubsystemUVE> memorySubsystems;

    [[nodiscard]] std::uint64_t TotalTrackedMemoryBytesUVE() const noexcept;
};

// ---------------------------------------------------------------------------
// 5. Terrain Sculpting & Splat Painting Brush Tool
// ---------------------------------------------------------------------------
enum class EditorTerrainBrushModeUVE : std::uint8_t {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    PaintSplat = 4,
};

struct EditorTerrainBrushSettingsUVE final {
    EditorTerrainBrushModeUVE mode = EditorTerrainBrushModeUVE::Raise;
    float radiusMetres = 4.0F;
    float strength = 1.0F;
    float flattenTargetHeight = 0.5F;
    std::uint8_t splatLayerIndex = 0U;
};

void ApplyEditorTerrainBrushStrokeUVE(Scene::Terrain3DComponentUVE& terrain, float localX,
                                      float localZ, const EditorTerrainBrushSettingsUVE& brush,
                                      float deltaTimeSeconds);

// ---------------------------------------------------------------------------
// 6. Git Source-Control Integration
// ---------------------------------------------------------------------------
enum class EditorVcsFileStatusUVE : std::uint8_t {
    Clean = 0,
    Modified = 1,
    Added = 2,
    Deleted = 3,
    Untracked = 4,
    Conflicted = 5,
};

class EditorGitVcsProviderUVE final {
public:
    /// Parses `git status --porcelain` output into per-path VCS status badges.
    void ParsePorcelainOutputUVE(std::string_view porcelainText);

    [[nodiscard]] EditorVcsFileStatusUVE GetFileStatusUVE(const std::string& relativePath) const noexcept;
    [[nodiscard]] std::size_t TrackedChangeCountUVE() const noexcept { return m_statuses.size(); }
    [[nodiscard]] const std::map<std::string, EditorVcsFileStatusUVE>& AllStatusesUVE() const noexcept {
        return m_statuses;
    }

private:
    std::map<std::string, EditorVcsFileStatusUVE> m_statuses;
};

// ---------------------------------------------------------------------------
// 7. Find-in-Project Search Service
// ---------------------------------------------------------------------------
struct EditorProjectSearchHitUVE final {
    std::string relativePath;
    std::uint32_t lineNumber = 0U;
    std::string previewLine;
};

[[nodiscard]] std::vector<EditorProjectSearchHitUVE> SearchProjectContentUVE(
    const std::filesystem::path& projectRoot, std::string_view query,
    std::size_t maxResults = 200U);

// ---------------------------------------------------------------------------
// 8. Editor Plugin / Extension Registry API
// ---------------------------------------------------------------------------
struct EditorMenuExtensionUVE final {
    std::string menuPath;
    std::string label;
    std::function<void()> onInvoke;
};

struct EditorPanelExtensionUVE final {
    std::string panelId;
    std::string title;
};

struct EditorImporterExtensionUVE final {
    std::string fileExtension;
    std::string importerName;
    std::function<bool(const std::filesystem::path&)> onImport;
};

class EditorExtensionRegistryUVE final {
public:
    void RegisterMenuItemUVE(EditorMenuExtensionUVE item);
    void RegisterPanelUVE(EditorPanelExtensionUVE panel);
    void RegisterImporterUVE(EditorImporterExtensionUVE importer);

    [[nodiscard]] bool InvokeMenuItemUVE(std::string_view menuPath, std::string_view label) const;
    [[nodiscard]] bool TryImportFileUVE(const std::filesystem::path& filePath) const;

    [[nodiscard]] const std::vector<EditorMenuExtensionUVE>& MenuItemsUVE() const noexcept {
        return m_menuItems;
    }
    [[nodiscard]] const std::vector<EditorPanelExtensionUVE>& PanelsUVE() const noexcept {
        return m_panels;
    }
    [[nodiscard]] const std::vector<EditorImporterExtensionUVE>& ImportersUVE() const noexcept {
        return m_importers;
    }

private:
    std::vector<EditorMenuExtensionUVE> m_menuItems;
    std::vector<EditorPanelExtensionUVE> m_panels;
    std::vector<EditorImporterExtensionUVE> m_importers;
};

// ---------------------------------------------------------------------------
// 9. Automated In-Editor Project & Scene Validator
// ---------------------------------------------------------------------------
enum class EditorValidationSeverityUVE : std::uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
};

struct EditorValidationIssueUVE final {
    EditorValidationSeverityUVE severity = EditorValidationSeverityUVE::Warning;
    std::string category;
    std::string message;
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::string assetPath;
};

[[nodiscard]] std::vector<EditorValidationIssueUVE> RunEditorSceneValidationUVE(
    Scene::IEntityManagerUVE& entityManager,
    const EditorAssetDependencyIndexUVE* dependencyIndex = nullptr,
    const std::vector<std::string>& projectAssets = {});

} // namespace UVE::Editor
