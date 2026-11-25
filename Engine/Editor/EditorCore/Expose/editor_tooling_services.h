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
class EditorAssetDependencyIndex final {
public:
    /// Scans `projectRoot` for project assets (`.uvscene`, `.uvmaterial`, `.uvprefab`,
    /// `.uvmatgraph`, `.uvanim`, `.uvfx`, `.json`) and indexes forward + reverse references.
    void RebuildFromProject(const std::filesystem::path& projectRoot);

    void RegisterEdge(const std::string& fromAsset, const std::string& toAsset);
    void Clear() noexcept;

    [[nodiscard]] std::vector<std::string> GetDependencies(const std::string& assetPath) const;
    [[nodiscard]] std::vector<std::string> GetReferencers(const std::string& assetPath) const;
    [[nodiscard]] std::vector<std::string> FindUnusedAssets(
        const std::vector<std::string>& allAssets) const;

private:
    std::unordered_map<std::string, std::vector<std::string>> m_forward;
    std::unordered_map<std::string, std::vector<std::string>> m_reverse;
};

// ---------------------------------------------------------------------------
// 2. Multi-Scene / Additive Sub-Scene Editing Session
// ---------------------------------------------------------------------------
struct EditorSubSceneEntry final {
    std::string scenePath;
    bool loaded = true;
    bool visible = true;
    bool dirty = false;
    bool activeForEdit = false;
};

class EditorMultiSceneSession final {
public:
    void SetPrimaryScene(std::string scenePath);
    [[nodiscard]] const std::string& PrimaryScene() const noexcept { return m_primaryScene; }

    bool AddSubScene(std::string scenePath);
    bool RemoveSubScene(std::string_view scenePath);
    bool SetSubSceneLoaded(std::string_view scenePath, bool loaded);
    bool SetSubSceneVisible(std::string_view scenePath, bool visible);
    bool SetActiveEditScene(std::string_view scenePath);

    [[nodiscard]] const std::string& ActiveEditScene() const noexcept { return m_activeScene; }
    [[nodiscard]] const std::vector<EditorSubSceneEntry>& SubScenes() const noexcept {
        return m_subScenes;
    }

private:
    std::string m_primaryScene;
    std::string m_activeScene;
    std::vector<EditorSubSceneEntry> m_subScenes;
};

// ---------------------------------------------------------------------------
// 3. Prefab Override Diff / Apply / Revert Workflow
// ---------------------------------------------------------------------------
struct EditorPrefabPropertyDiff final {
    std::string propertyName;
    std::string prefabValue;
    std::string instanceValue;
};

struct EditorPrefabTransformSnapshot final {
    std::string name;
    Scene::TransformComponent transform{};
};

[[nodiscard]] std::vector<EditorPrefabPropertyDiff> DiffPrefabInstance(
    const EditorPrefabTransformSnapshot& prefabSource,
    const EditorPrefabTransformSnapshot& liveInstance);

void ApplyInstanceOverridesToPrefab(const EditorPrefabTransformSnapshot& liveInstance,
                                       EditorPrefabTransformSnapshot& prefabSource);

void RevertInstanceToPrefab(const EditorPrefabTransformSnapshot& prefabSource,
                               EditorPrefabTransformSnapshot& liveInstance);

// ---------------------------------------------------------------------------
// 4. GPU Profiler, Render-Graph Pass & Memory Breakdown Snapshot
// ---------------------------------------------------------------------------
struct EditorGpuPassTiming final {
    std::string passName;
    float gpuMs = 0.0F;
    std::uint32_t drawCalls = 0U;
    std::uint32_t triangles = 0U;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

struct EditorMemorySubsystem final {
    std::string subsystemName;
    std::uint64_t allocatedBytes = 0U;
    std::uint32_t resourceCount = 0U;
};

struct EditorFrameDebugSnapshot final {
    std::uint64_t frameIndex = 0U;
    float totalCpuMs = 0.0F;
    float totalGpuMs = 0.0F;
    std::vector<EditorGpuPassTiming> gpuPasses;
    std::vector<EditorMemorySubsystem> memorySubsystems;

    [[nodiscard]] std::uint64_t TotalTrackedMemoryBytes() const noexcept;
};

// ---------------------------------------------------------------------------
// 5. Terrain Sculpting & Splat Painting Brush Tool
// ---------------------------------------------------------------------------
enum class EditorTerrainBrushMode : std::uint8_t {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    PaintSplat = 4,
};

struct EditorTerrainBrushSettings final {
    EditorTerrainBrushMode mode = EditorTerrainBrushMode::Raise;
    float radiusMetres = 4.0F;
    float strength = 1.0F;
    float flattenTargetHeight = 0.5F;
    std::uint8_t splatLayerIndex = 0U;
};

void ApplyEditorTerrainBrushStroke(Scene::Terrain3DComponent& terrain, float localX,
                                      float localZ, const EditorTerrainBrushSettings& brush,
                                      float deltaTimeSeconds);

// ---------------------------------------------------------------------------
// 6. Git Source-Control Integration
// ---------------------------------------------------------------------------
enum class EditorVcsFileStatus : std::uint8_t {
    Clean = 0,
    Modified = 1,
    Added = 2,
    Deleted = 3,
    Untracked = 4,
    Conflicted = 5,
};

class EditorGitVcsProvider final {
public:
    /// Parses `git status --porcelain` output into per-path VCS status badges.
    void ParsePorcelainOutput(std::string_view porcelainText);

    [[nodiscard]] EditorVcsFileStatus GetFileStatus(const std::string& relativePath) const noexcept;
    [[nodiscard]] std::size_t TrackedChangeCount() const noexcept { return m_statuses.size(); }
    [[nodiscard]] const std::map<std::string, EditorVcsFileStatus>& AllStatuses() const noexcept {
        return m_statuses;
    }

private:
    std::map<std::string, EditorVcsFileStatus> m_statuses;
};

// ---------------------------------------------------------------------------
// 7. Find-in-Project Search Service
// ---------------------------------------------------------------------------
struct EditorProjectSearchHit final {
    std::string relativePath;
    std::uint32_t lineNumber = 0U;
    std::string previewLine;
};

[[nodiscard]] std::vector<EditorProjectSearchHit> SearchProjectContent(
    const std::filesystem::path& projectRoot, std::string_view query,
    std::size_t maxResults = 200U);

// ---------------------------------------------------------------------------
// 8. Editor Plugin / Extension Registry API
// ---------------------------------------------------------------------------
struct EditorMenuExtension final {
    std::string menuPath;
    std::string label;
    std::function<void()> onInvoke;
};

struct EditorPanelExtension final {
    std::string panelId;
    std::string title;
};

struct EditorImporterExtension final {
    std::string fileExtension;
    std::string importerName;
    std::function<bool(const std::filesystem::path&)> onImport;
};

class EditorExtensionRegistry final {
public:
    void RegisterMenuItem(EditorMenuExtension item);
    void RegisterPanel(EditorPanelExtension panel);
    void RegisterImporter(EditorImporterExtension importer);

    [[nodiscard]] bool InvokeMenuItem(std::string_view menuPath, std::string_view label) const;
    [[nodiscard]] bool TryImportFile(const std::filesystem::path& filePath) const;

    [[nodiscard]] const std::vector<EditorMenuExtension>& MenuItems() const noexcept {
        return m_menuItems;
    }
    [[nodiscard]] const std::vector<EditorPanelExtension>& Panels() const noexcept {
        return m_panels;
    }
    [[nodiscard]] const std::vector<EditorImporterExtension>& Importers() const noexcept {
        return m_importers;
    }

private:
    std::vector<EditorMenuExtension> m_menuItems;
    std::vector<EditorPanelExtension> m_panels;
    std::vector<EditorImporterExtension> m_importers;
};

// ---------------------------------------------------------------------------
// 9. Automated In-Editor Project & Scene Validator
// ---------------------------------------------------------------------------
enum class EditorValidationSeverity : std::uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
};

struct EditorValidationIssue final {
    EditorValidationSeverity severity = EditorValidationSeverity::Warning;
    std::string category;
    std::string message;
    Scene::Entity entity = Scene::kInvalidEntity;
    std::string assetPath;
};

[[nodiscard]] std::vector<EditorValidationIssue> RunEditorSceneValidation(
    Scene::IEntityManager& entityManager,
    const EditorAssetDependencyIndex* dependencyIndex = nullptr,
    const std::vector<std::string>& projectAssets = {});

} // namespace UVE::Editor
