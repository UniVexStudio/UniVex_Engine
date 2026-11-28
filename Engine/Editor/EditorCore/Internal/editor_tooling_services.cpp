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

[[nodiscard]] std::string ToLowerCopy(const std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] std::string FormatVec3(const Math::Vector3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", static_cast<double>(v.x),
                  static_cast<double>(v.y), static_cast<double>(v.z));
    return std::string(buf);
}

[[nodiscard]] bool IsSearchableAssetExt(const std::string& ext) {
    return ext == ".uvscene" || ext == ".uvmaterial" || ext == ".uvprefab" ||
           ext == ".uvmatgraph" || ext == ".uvbp" || ext == ".uvfx" || ext == ".uvanim" ||
           ext == ".json" || ext == ".glsl" || ext == ".uvproject";
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Asset Dependency Index
// ---------------------------------------------------------------------------
void EditorAssetDependencyIndex::Clear() noexcept {
    m_forward.clear();
    m_reverse.clear();
}

void EditorAssetDependencyIndex::RegisterEdge(const std::string& fromAsset,
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

void EditorAssetDependencyIndex::RebuildFromProject(const std::filesystem::path& projectRoot) {
    Clear();
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
        const std::string ext = ToLowerCopy(fullPath.extension().string());
        if (!IsSearchableAssetExt(ext)) {
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
                RegisterEdge(rel, candidate);
            }
        }
    }
}

std::vector<std::string> EditorAssetDependencyIndex::GetDependencies(
    const std::string& assetPath) const {
    const auto it = m_forward.find(assetPath);
    return it != m_forward.end() ? it->second : std::vector<std::string>{};
}

std::vector<std::string> EditorAssetDependencyIndex::GetReferencers(
    const std::string& assetPath) const {
    const auto it = m_reverse.find(assetPath);
    return it != m_reverse.end() ? it->second : std::vector<std::string>{};
}

std::vector<std::string> EditorAssetDependencyIndex::FindUnusedAssets(
    const std::vector<std::string>& allAssets) const {
    std::vector<std::string> unused;
    for (const std::string& asset : allAssets) {
        const std::string ext = ToLowerCopy(std::filesystem::path(asset).extension().string());
        if (ext == ".uvscene" || ext == ".uvproject") {
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
void EditorMultiSceneSession::SetPrimaryScene(std::string scenePath) {
    m_primaryScene = std::move(scenePath);
    m_activeScene = m_primaryScene;
    for (auto& s : m_subScenes) {
        s.activeForEdit = false;
    }
}

bool EditorMultiSceneSession::AddSubScene(std::string scenePath) {
    if (scenePath.empty() || scenePath == m_primaryScene) {
        return false;
    }
    for (const auto& s : m_subScenes) {
        if (s.scenePath == scenePath) {
            return false;
        }
    }
    m_subScenes.push_back(EditorSubSceneEntry{std::move(scenePath), true, true, false, false});
    return true;
}

bool EditorMultiSceneSession::RemoveSubScene(const std::string_view scenePath) {
    const auto it = std::find_if(m_subScenes.begin(), m_subScenes.end(),
                                 [scenePath](const EditorSubSceneEntry& s) { return s.scenePath == scenePath; });
    if (it == m_subScenes.end()) {
        return false;
    }
    if (m_activeScene == scenePath) {
        m_activeScene = m_primaryScene;
    }
    m_subScenes.erase(it);
    return true;
}

bool EditorMultiSceneSession::SetSubSceneLoaded(const std::string_view scenePath, const bool loaded) {
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

bool EditorMultiSceneSession::SetSubSceneVisible(const std::string_view scenePath, const bool visible) {
    for (auto& s : m_subScenes) {
        if (s.scenePath == scenePath) {
            s.visible = visible;
            return true;
        }
    }
    return false;
}

bool EditorMultiSceneSession::SetActiveEditScene(const std::string_view scenePath) {
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
std::vector<EditorPrefabPropertyDiff> DiffPrefabInstance(
    const EditorPrefabTransformSnapshot& prefabSource,
    const EditorPrefabTransformSnapshot& liveInstance) {
    std::vector<EditorPrefabPropertyDiff> diffs;
    if (prefabSource.name != liveInstance.name) {
        diffs.push_back({"Name", prefabSource.name, liveInstance.name});
    }
    if (Math::Length(prefabSource.transform.localPosition - liveInstance.transform.localPosition) > 1.0e-4F) {
        diffs.push_back({"Position", FormatVec3(prefabSource.transform.localPosition),
                         FormatVec3(liveInstance.transform.localPosition)});
    }
    if (Math::Length(prefabSource.transform.localScale - liveInstance.transform.localScale) > 1.0e-4F) {
        diffs.push_back({"Scale", FormatVec3(prefabSource.transform.localScale),
                         FormatVec3(liveInstance.transform.localScale)});
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

void ApplyInstanceOverridesToPrefab(const EditorPrefabTransformSnapshot& liveInstance,
                                       EditorPrefabTransformSnapshot& prefabSource) {
    prefabSource = liveInstance;
}

void RevertInstanceToPrefab(const EditorPrefabTransformSnapshot& prefabSource,
                               EditorPrefabTransformSnapshot& liveInstance) {
    liveInstance = prefabSource;
}

// ---------------------------------------------------------------------------
// 4. GPU Profiler & Memory Snapshot
// ---------------------------------------------------------------------------
std::uint64_t EditorFrameDebugSnapshot::TotalTrackedMemoryBytes() const noexcept {
    std::uint64_t total = 0U;
    for (const auto& entry : memorySubsystems) {
        total += entry.allocatedBytes;
    }
    return total;
}

// ---------------------------------------------------------------------------
// 5. Terrain Brush Tool
// ---------------------------------------------------------------------------
void ApplyEditorTerrainBrushStroke(Scene::Terrain3DComponent& terrain, const float localX,
                                      const float localZ, const EditorTerrainBrushSettings& brush,
                                      const float deltaTimeSeconds) {
    if (deltaTimeSeconds <= 0.0F || brush.radiusMetres <= 0.0F) {
        return;
    }
    const float delta = brush.strength * deltaTimeSeconds;
    switch (brush.mode) {
        case EditorTerrainBrushMode::Raise:
            Scene::Terrain3D::Sculpt(terrain, localX, localZ, brush.radiusMetres, delta, false, 0.0F);
            break;
        case EditorTerrainBrushMode::Lower:
            Scene::Terrain3D::Sculpt(terrain, localX, localZ, brush.radiusMetres, -delta, false, 0.0F);
            break;
        case EditorTerrainBrushMode::Flatten:
            Scene::Terrain3D::Sculpt(terrain, localX, localZ, brush.radiusMetres, delta, true,
                                           brush.flattenTargetHeight);
            break;
        case EditorTerrainBrushMode::Smooth: {
            const float centerH = Scene::Terrain3D::SampleHeightLocal(terrain, localX, localZ);
            const float normH = terrain.heightScale > 1.0e-5F ? (centerH / terrain.heightScale) : 0.0F;
            Scene::Terrain3D::Sculpt(terrain, localX, localZ, brush.radiusMetres, delta * 0.5F, true,
                                           normH);
            break;
        }
        case EditorTerrainBrushMode::PaintSplat:
            Scene::Terrain3D::PaintSplat(terrain, localX, localZ, brush.radiusMetres,
                                               brush.splatLayerIndex, std::clamp(delta, 0.0F, 1.0F));
            break;
    }
}

// ---------------------------------------------------------------------------
// 6. Git Source-Control Provider
// ---------------------------------------------------------------------------
void EditorGitVcsProvider::ParsePorcelainOutput(const std::string_view porcelainText) {
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
        EditorVcsFileStatus status = EditorVcsFileStatus::Modified;
        if (x == '?' && y == '?') {
            status = EditorVcsFileStatus::Untracked;
        } else if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
            status = EditorVcsFileStatus::Conflicted;
        } else if (x == 'A') {
            status = EditorVcsFileStatus::Added;
        } else if (x == 'D' || y == 'D') {
            status = EditorVcsFileStatus::Deleted;
        }
        m_statuses[path] = status;
    }
}

EditorVcsFileStatus EditorGitVcsProvider::GetFileStatus(
    const std::string& relativePath) const noexcept {
    const auto it = m_statuses.find(relativePath);
    return it != m_statuses.end() ? it->second : EditorVcsFileStatus::Clean;
}

// ---------------------------------------------------------------------------
// 7. Find-in-Project Search
// ---------------------------------------------------------------------------
std::vector<EditorProjectSearchHit> SearchProjectContent(const std::filesystem::path& projectRoot,
                                                               const std::string_view query,
                                                               const std::size_t maxResults) {
    std::vector<EditorProjectSearchHit> hits;
    if (query.empty() || maxResults == 0U) {
        return hits;
    }
    std::error_code ec;
    if (!std::filesystem::exists(projectRoot, ec)) {
        return hits;
    }
    const std::string needle = ToLowerCopy(query);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectRoot, ec)) {
        if (hits.size() >= maxResults) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string rel = std::filesystem::relative(entry.path(), projectRoot, ec).generic_string();
        const std::string relLower = ToLowerCopy(rel);
        if (relLower.find(needle) != std::string::npos) {
            hits.push_back(EditorProjectSearchHit{rel, 0U, "[Asset File] " + rel});
            if (hits.size() >= maxResults) {
                break;
            }
        }
        const std::string ext = ToLowerCopy(entry.path().extension().string());
        if (!IsSearchableAssetExt(ext)) {
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
            if (ToLowerCopy(line).find(needle) != std::string::npos) {
                std::string preview = line.size() > 140U ? (line.substr(0U, 140U) + "...") : line;
                hits.push_back(EditorProjectSearchHit{rel, lineNo, std::move(preview)});
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
void EditorExtensionRegistry::RegisterMenuItem(EditorMenuExtension item) {
    m_menuItems.push_back(std::move(item));
}

void EditorExtensionRegistry::RegisterPanel(EditorPanelExtension panel) {
    m_panels.push_back(std::move(panel));
}

void EditorExtensionRegistry::RegisterImporter(EditorImporterExtension importer) {
    importer.fileExtension = ToLowerCopy(importer.fileExtension);
    m_importers.push_back(std::move(importer));
}

bool EditorExtensionRegistry::InvokeMenuItem(const std::string_view menuPath,
                                                   const std::string_view label) const {
    for (const auto& item : m_menuItems) {
        if (item.menuPath == menuPath && item.label == label && item.onInvoke) {
            item.onInvoke();
            return true;
        }
    }
    return false;
}

bool EditorExtensionRegistry::TryImportFile(const std::filesystem::path& filePath) const {
    const std::string ext = ToLowerCopy(filePath.extension().string());
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
std::vector<EditorValidationIssue> RunEditorSceneValidation(
    Scene::IEntityManager& entityManager,
    const EditorAssetDependencyIndex* const dependencyIndex,
    const std::vector<std::string>& projectAssets) {
    std::vector<EditorValidationIssue> issues;

    entityManager.ForEach<Scene::MeshComponent>(
        [&](const Scene::Entity entity, const Scene::MeshComponent& mesh) {
            if (mesh.meshGuid == Asset::kInvalidAssetGuid) {
                issues.push_back(EditorValidationIssue{
                    EditorValidationSeverity::Error,
                    "Rendering",
                    "MeshComponent has an invalid/unassigned mesh GUID",
                    entity,
                    {},
                });
            }
        });

    entityManager.ForEach<Scene::Rigid3DComponent>(
        [&](const Scene::Entity entity, const Scene::Rigid3DComponent&) {
            if (!entityManager.HasComponent<Scene::ColliderComponent>(entity)) {
                issues.push_back(EditorValidationIssue{
                    EditorValidationSeverity::Warning,
                    "Physics",
                    "Rigid3D entity has no ColliderComponent attached",
                    entity,
                    {},
                });
            }
        });

    if (dependencyIndex != nullptr && !projectAssets.empty()) {
        for (const std::string& unused : dependencyIndex->FindUnusedAssets(projectAssets)) {
            issues.push_back(EditorValidationIssue{
                EditorValidationSeverity::Info,
                "Assets",
                "Unused asset has zero scene or material references",
                Scene::kInvalidEntity,
                unused,
            });
        }
    }

    return issues;
}

} // namespace UVE::Editor
