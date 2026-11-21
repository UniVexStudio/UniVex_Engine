// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_tooling_services.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "collider_3d.h"
#include "mesh_component.h"
#include "rigid_3d.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string ToLowerCopyUVE(const std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::string FormatVec3UVE(const Math::Vector3UVE& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", static_cast<double>(v.x),
                  static_cast<double>(v.y), static_cast<double>(v.z));
    return std::string(buf);
}

[[nodiscard]] bool IsSearchableAssetExtUVE(const std::string& ext) {
    return ext == ".uvscene" || ext == ".uvmaterial" || ext == ".uvprefab" ||
           ext == ".uvmatgraph" || ext == ".uvs" || ext == ".uvfx" || ext == ".uvanim" ||
           ext == ".json" || ext == ".glsl" || ext == ".uvproj";
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Asset Dependency Index
// ---------------------------------------------------------------------------
void EditorAssetDependencyIndexUVE::ClearUVE() noexcept {
    m_forward.clear();
    m_reverse.clear();
}

void EditorAssetDependencyIndexUVE::RegisterEdgeUVE(const std::string& fromAsset,
                                                    const std::string& toAsset) {
    if (fromAsset.empty() || toAsset.empty() || fromAsset == toAsset) {
        return;
    }
    auto& fwd = m_forward[fromAsset];
    if (std::find(fwd.begin(), fwd.end(), toAsset) == fwd.end()) {
        fwd.push_back(toAsset);
    }
    auto& rev = m_reverse[toAsset];
    if (std::find(rev.begin(), rev.end(), fromAsset) == rev.end()) {
        rev.push_back(fromAsset);
    }
}

void EditorAssetDependencyIndexUVE::RebuildFromProjectUVE(const std::filesystem::path& projectRoot) {
    ClearUVE();
    std::error_code ec;
    if (!std::filesystem::exists(projectRoot, ec)) {
        return;
    }
    std::vector<std::string> allRelPaths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string rel = std::filesystem::relative(entry.path(), projectRoot, ec).generic_string();
        if (!rel.empty()) {
            allRelPaths.push_back(rel);
        }
    }

    for (const std::string& rel : allRelPaths) {
        const std::filesystem::path fullPath = projectRoot / rel;
        const std::string ext = ToLowerCopyUVE(fullPath.extension().string());
        if (!IsSearchableAssetExtUVE(ext)) {
            continue;
        }
        std::ifstream in(fullPath);
        if (!in) {
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string content = ss.str();
        for (const std::string& candidate : allRelPaths) {
            if (candidate == rel) {
                continue;
            }
            if (content.find(candidate) != std::string::npos) {
                RegisterEdgeUVE(rel, candidate);
            }
        }
    }
}

std::vector<std::string> EditorAssetDependencyIndexUVE::GetDependenciesUVE(
    const std::string& assetPath) const {
    const auto it = m_forward.find(assetPath);
    return it != m_forward.end() ? it->second : std::vector<std::string>{};
}

std::vector<std::string> EditorAssetDependencyIndexUVE::GetReferencersUVE(
    const std::string& assetPath) const {
    const auto it = m_reverse.find(assetPath);
    return it != m_reverse.end() ? it->second : std::vector<std::string>{};
}

std::vector<std::string> EditorAssetDependencyIndexUVE::FindUnusedAssetsUVE(
    const std::vector<std::string>& allAssets) const {
    std::vector<std::string> unused;
    for (const std::string& asset : allAssets) {
        const std::string ext = ToLowerCopyUVE(std::filesystem::path(asset).extension().string());
        if (ext == ".uvscene" || ext == ".uvproj") {
            continue; // Root entry points are never flagged as unused.
        }
        const auto it = m_reverse.find(asset);
        if (it == m_reverse.end() || it->second.empty()) {
            unused.push_back(asset);
        }
    }
    return unused;
}

// ---------------------------------------------------------------------------
// 2. Multi-Scene Editing Session
// ---------------------------------------------------------------------------
void EditorMultiSceneSessionUVE::SetPrimarySceneUVE(std::string scenePath) {
    m_primaryScene = std::move(scenePath);
    m_activeScene = m_primaryScene;
    for (auto& s : m_subScenes) {
        s.activeForEdit = false;
    }
}

bool EditorMultiSceneSessionUVE::AddSubSceneUVE(std::string scenePath) {
    if (scenePath.empty() || scenePath == m_primaryScene) {
        return false;
    }
    for (const auto& s : m_subScenes) {
        if (s.scenePath == scenePath) {
            return false;
        }
    }
    m_subScenes.push_back(EditorSubSceneEntryUVE{std::move(scenePath), true, true, false, false});
    return true;
}

bool EditorMultiSceneSessionUVE::RemoveSubSceneUVE(const std::string_view scenePath) {
    const auto it = std::find_if(m_subScenes.begin(), m_subScenes.end(),
                                 [scenePath](const EditorSubSceneEntryUVE& s) { return s.scenePath == scenePath; });
    if (it == m_subScenes.end()) {
        return false;
    }
    if (m_activeScene == scenePath) {
        m_activeScene = m_primaryScene;
    }
    m_subScenes.erase(it);
    return true;
}

bool EditorMultiSceneSessionUVE::SetSubSceneLoadedUVE(const std::string_view scenePath, const bool loaded) {
    for (auto& s : m_subScenes) {
        if (s.scenePath == scenePath) {
            s.loaded = loaded;
            if (!loaded && s.activeForEdit) {
                s.activeForEdit = false;
                m_activeScene = m_primaryScene;
            }
            return true;
        }
    }
    return false;
}

bool EditorMultiSceneSessionUVE::SetSubSceneVisibleUVE(const std::string_view scenePath, const bool visible) {
    for (auto& s : m_subScenes) {
        if (s.scenePath == scenePath) {
            s.visible = visible;
            return true;
        }
    }
    return false;
}

bool EditorMultiSceneSessionUVE::SetActiveEditSceneUVE(const std::string_view scenePath) {
    if (scenePath == m_primaryScene) {
        m_activeScene = m_primaryScene;
        for (auto& s : m_subScenes) {
            s.activeForEdit = false;
        }
        return true;
    }
    for (auto& s : m_subScenes) {
        if (s.scenePath == scenePath && s.loaded) {
            for (auto& other : m_subScenes) {
                other.activeForEdit = false;
            }
            s.activeForEdit = true;
            m_activeScene = s.scenePath;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 3. Prefab Override Diff / Apply / Revert
// ---------------------------------------------------------------------------
std::vector<EditorPrefabPropertyDiffUVE> DiffPrefabInstanceUVE(
    const EditorPrefabTransformSnapshotUVE& prefabSource,
    const EditorPrefabTransformSnapshotUVE& liveInstance) {
    std::vector<EditorPrefabPropertyDiffUVE> diffs;
    if (prefabSource.name != liveInstance.name) {
        diffs.push_back({"Name", prefabSource.name, liveInstance.name});
    }
    if (Math::LengthUVE(prefabSource.transform.localPosition - liveInstance.transform.localPosition) > 1.0e-4F) {
        diffs.push_back({"Position", FormatVec3UVE(prefabSource.transform.localPosition),
                         FormatVec3UVE(liveInstance.transform.localPosition)});
    }
    if (Math::LengthUVE(prefabSource.transform.localScale - liveInstance.transform.localScale) > 1.0e-4F) {
        diffs.push_back({"Scale", FormatVec3UVE(prefabSource.transform.localScale),
                         FormatVec3UVE(liveInstance.transform.localScale)});
    }
    const float qDiff = std::fabs(prefabSource.transform.localRotation.x - liveInstance.transform.localRotation.x) +
                        std::fabs(prefabSource.transform.localRotation.y - liveInstance.transform.localRotation.y) +
                        std::fabs(prefabSource.transform.localRotation.z - liveInstance.transform.localRotation.z) +
                        std::fabs(prefabSource.transform.localRotation.w - liveInstance.transform.localRotation.w);
    if (qDiff > 1.0e-4F) {
        diffs.push_back({"Rotation", "prefab", "overridden"});
    }
    return diffs;
}

void ApplyInstanceOverridesToPrefabUVE(const EditorPrefabTransformSnapshotUVE& liveInstance,
                                       EditorPrefabTransformSnapshotUVE& prefabSource) {
    prefabSource = liveInstance;
}

void RevertInstanceToPrefabUVE(const EditorPrefabTransformSnapshotUVE& prefabSource,
                               EditorPrefabTransformSnapshotUVE& liveInstance) {
    liveInstance = prefabSource;
}

// ---------------------------------------------------------------------------
// 4. GPU Profiler & Memory Snapshot
// ---------------------------------------------------------------------------
std::uint64_t EditorFrameDebugSnapshotUVE::TotalTrackedMemoryBytesUVE() const noexcept {
    std::uint64_t total = 0U;
    for (const auto& entry : memorySubsystems) {
        total += entry.allocatedBytes;
    }
    return total;
}

// ---------------------------------------------------------------------------
// 5. Terrain Brush Tool
// ---------------------------------------------------------------------------
void ApplyEditorTerrainBrushStrokeUVE(Scene::Terrain3DComponentUVE& terrain, const float localX,
                                      const float localZ, const EditorTerrainBrushSettingsUVE& brush,
                                      const float deltaTimeSeconds) {
    if (deltaTimeSeconds <= 0.0F || brush.radiusMetres <= 0.0F) {
        return;
    }
    const float delta = brush.strength * deltaTimeSeconds;
    switch (brush.mode) {
        case EditorTerrainBrushModeUVE::Raise:
            Scene::Terrain3DUVE::SculptUVE(terrain, localX, localZ, brush.radiusMetres, delta, false, 0.0F);
            break;
        case EditorTerrainBrushModeUVE::Lower:
            Scene::Terrain3DUVE::SculptUVE(terrain, localX, localZ, brush.radiusMetres, -delta, false, 0.0F);
            break;
        case EditorTerrainBrushModeUVE::Flatten:
            Scene::Terrain3DUVE::SculptUVE(terrain, localX, localZ, brush.radiusMetres, delta, true,
                                           brush.flattenTargetHeight);
            break;
        case EditorTerrainBrushModeUVE::Smooth: {
            const float centerH = Scene::Terrain3DUVE::SampleHeightLocalUVE(terrain, localX, localZ);
            const float normH = terrain.heightScale > 1.0e-5F ? (centerH / terrain.heightScale) : 0.0F;
            Scene::Terrain3DUVE::SculptUVE(terrain, localX, localZ, brush.radiusMetres, delta * 0.5F, true,
                                           normH);
            break;
        }
        case EditorTerrainBrushModeUVE::PaintSplat:
            Scene::Terrain3DUVE::PaintSplatUVE(terrain, localX, localZ, brush.radiusMetres,
                                               brush.splatLayerIndex, std::clamp(delta, 0.0F, 1.0F));
            break;
    }
}

// ---------------------------------------------------------------------------
// 6. Git Source-Control Provider
// ---------------------------------------------------------------------------
void EditorGitVcsProviderUVE::ParsePorcelainOutputUVE(const std::string_view porcelainText) {
    m_statuses.clear();
    std::size_t start = 0U;
    while (start < porcelainText.size()) {
        const std::size_t nl = porcelainText.find('\n', start);
        const std::string_view line =
            nl == std::string_view::npos ? porcelainText.substr(start) : porcelainText.substr(start, nl - start);
        start = nl == std::string_view::npos ? porcelainText.size() : (nl + 1U);
        if (line.size() < 4U) {
            continue;
        }
        const char x = line[0];
        const char y = line[1];
        std::string path(line.substr(3U));
        if (!path.empty() && path.back() == '\r') {
            path.pop_back();
        }
        EditorVcsFileStatusUVE status = EditorVcsFileStatusUVE::Modified;
        if (x == '?' && y == '?') {
            status = EditorVcsFileStatusUVE::Untracked;
        } else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            status = EditorVcsFileStatusUVE::Conflicted;
        } else if (x == 'A') {
            status = EditorVcsFileStatusUVE::Added;
        } else if (x == 'D' || y == 'D') {
            status = EditorVcsFileStatusUVE::Deleted;
        }
        m_statuses[path] = status;
    }
}

EditorVcsFileStatusUVE EditorGitVcsProviderUVE::GetFileStatusUVE(
    const std::string& relativePath) const noexcept {
    const auto it = m_statuses.find(relativePath);
    return it != m_statuses.end() ? it->second : EditorVcsFileStatusUVE::Clean;
}

// ---------------------------------------------------------------------------
// 7. Find-in-Project Search
// ---------------------------------------------------------------------------
std::vector<EditorProjectSearchHitUVE> SearchProjectContentUVE(const std::filesystem::path& projectRoot,
                                                               const std::string_view query,
                                                               const std::size_t maxResults) {
    std::vector<EditorProjectSearchHitUVE> hits;
    if (query.empty() || maxResults == 0U) {
        return hits;
    }
    std::error_code ec;
    if (!std::filesystem::exists(projectRoot, ec)) {
        return hits;
    }
    const std::string needle = ToLowerCopyUVE(query);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot, ec)) {
        if (hits.size() >= maxResults) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string rel = std::filesystem::relative(entry.path(), projectRoot, ec).generic_string();
        const std::string relLower = ToLowerCopyUVE(rel);
        if (relLower.find(needle) != std::string::npos) {
            hits.push_back(EditorProjectSearchHitUVE{rel, 0U, "[Asset File] " + rel});
            if (hits.size() >= maxResults) {
                break;
            }
        }
        const std::string ext = ToLowerCopyUVE(entry.path().extension().string());
        if (!IsSearchableAssetExtUVE(ext)) {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in) {
            continue;
        }
        std::string line;
        std::uint32_t lineNo = 0U;
        while (std::getline(in, line)) {
            ++lineNo;
            if (ToLowerCopyUVE(line).find(needle) != std::string::npos) {
                std::string preview = line.size() > 140U ? (line.substr(0U, 140U) + "...") : line;
                hits.push_back(EditorProjectSearchHitUVE{rel, lineNo, std::move(preview)});
                if (hits.size() >= maxResults) {
                    break;
                }
            }
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
// 8. Editor Plugin / Extension Registry
// ---------------------------------------------------------------------------
void EditorExtensionRegistryUVE::RegisterMenuItemUVE(EditorMenuExtensionUVE item) {
    m_menuItems.push_back(std::move(item));
}

void EditorExtensionRegistryUVE::RegisterPanelUVE(EditorPanelExtensionUVE panel) {
    m_panels.push_back(std::move(panel));
}

void EditorExtensionRegistryUVE::RegisterImporterUVE(EditorImporterExtensionUVE importer) {
    importer.fileExtension = ToLowerCopyUVE(importer.fileExtension);
    m_importers.push_back(std::move(importer));
}

bool EditorExtensionRegistryUVE::InvokeMenuItemUVE(const std::string_view menuPath,
                                                   const std::string_view label) const {
    for (const auto& item : m_menuItems) {
        if (item.menuPath == menuPath && item.label == label && item.onInvoke) {
            item.onInvoke();
            return true;
        }
    }
    return false;
}

bool EditorExtensionRegistryUVE::TryImportFileUVE(const std::filesystem::path& filePath) const {
    const std::string ext = ToLowerCopyUVE(filePath.extension().string());
    for (const auto& imp : m_importers) {
        if (imp.fileExtension == ext && imp.onImport) {
            return imp.onImport(filePath);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 9. Automated Project & Scene Validator
// ---------------------------------------------------------------------------
std::vector<EditorValidationIssueUVE> RunEditorSceneValidationUVE(
    Scene::IEntityManagerUVE& entityManager,
    const EditorAssetDependencyIndexUVE* const dependencyIndex,
    const std::vector<std::string>& projectAssets) {
    std::vector<EditorValidationIssueUVE> issues;

    entityManager.ForEachUVE<Scene::MeshComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::MeshComponentUVE& mesh) {
            if (mesh.meshGuid == Asset::kInvalidAssetGuidUVE) {
                issues.push_back(EditorValidationIssueUVE{
                    EditorValidationSeverityUVE::Error,
                    "Rendering",
                    "MeshComponentUVE has an invalid/unassigned mesh GUID",
                    entity,
                    {},
                });
            }
        });

    entityManager.ForEachUVE<Scene::Rigid3DComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::Rigid3DComponentUVE&) {
            if (!entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity)) {
                issues.push_back(EditorValidationIssueUVE{
                    EditorValidationSeverityUVE::Warning,
                    "Physics",
                    "Rigid3D entity has no ColliderComponentUVE attached",
                    entity,
                    {},
                });
            }
        });

    if (dependencyIndex != nullptr && !projectAssets.empty()) {
        for (const std::string& unused : dependencyIndex->FindUnusedAssetsUVE(projectAssets)) {
            issues.push_back(EditorValidationIssueUVE{
                EditorValidationSeverityUVE::Info,
                "Assets",
                "Unused asset has zero scene or material references",
                Scene::kInvalidEntityUVE,
                unused,
            });
        }
    }

    return issues;
}

} // namespace UVE::Editor
