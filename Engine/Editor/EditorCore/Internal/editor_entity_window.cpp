// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor: one entity asset (.uventity / .uvprefab) opened on its own, in its own OS
// window, in a scene of its own (Core::DocumentScene) beside the level. Inside an
// EntityDocumentScope the entity is the document - its scene, selection, undo and dirty state
// stand in for the level's - so the Scene tree, the Inspector, gizmos and undo all work on the
// entity with no second set of tools, while the level stays open and editable in its own window.

#include "editor.h"
#include "transform_component.h"

#include "blueprint_asset.h"
#include "blueprint_graph_lowering.h"
#include "blueprint_nodes.h"
#include "widget_blueprint_asset.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>


#include "animation_sequencer_component.h"
#include "bone_attachment_pass.h"
#include "collider_component.h"
#include "animation_graph_component.h"
#include "name_component.h"
#include "prefab_instance_component.h"
#include "ragdoll_3d.h"
#include "script_component.h"
#include "spring_bone_simulator_3d.h"
#include "blueprint_object_host.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_parser.h"
#include "uve_file_envelope.h"

#include <nlohmann/json.hpp>

namespace UVE::Editor {
namespace {

/// A script slot that holds a node graph: a Blueprint Class, or a Widget Blueprint's own graph.
[[nodiscard]] bool IsGraphScriptPath(const std::string_view path) noexcept {
    return path.ends_with(".uvbp") || Asset::IsWidgetBlueprintPath(path);
}

[[nodiscard]] std::optional<Asset::BlueprintAsset> DecodeGraphScript(const std::string_view path, const std::vector<std::byte>& bytes) {
    return Asset::IsWidgetBlueprintPath(path) ? Asset::ReadWidgetBlueprintGraph(bytes) : Asset::DecodeBlueprintAsset(bytes);
}



} // namespace

/// The Entity Editor's Simulate: each scripted object's compiled Blueprint, and the entity as it
/// was when Play was pressed.
struct EntitySimulation final {
    struct Script final {
        Scene::Entity entity = Scene::kInvalidEntity;
        std::unique_ptr<Core::BlueprintObjectHost> host;
        std::unique_ptr<Blueprint::BlueprintInstance> instance;
        bool ready = false;
    };
    Scene::SceneSnapshot snapshot;
    bool dirtyBefore = false;
    bool paused = false;
    std::vector<Script> scripts;
};

Scene::Entity Editor::LoadEntityIntoDocument(const Asset::AssetGuid guid) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity root = m_services->GetPrefabSystem().Instantiate(
        entityManager, m_services->GetSceneGraph(), m_services->GetAssetDatabase(), guid, Scene::kInvalidEntity);
    if (root == Scene::kInvalidEntity) {
        return root;
    }
    // Edited as itself, not as an instance of itself: saving writes a plain tree.
    if (entityManager.HasComponent<Scene::PrefabInstanceComponent>(root)) {
        entityManager.RemoveComponent<Scene::PrefabInstanceComponent>(root);
    }
    // Synchronize root entity name with the open asset filename stem
    if (m_entityEditSession.has_value() && !m_entityEditSession->assetPath.empty()) {
        const std::string assetStem = m_entityEditSession->assetPath.stem().string();
        if (!assetStem.empty()) {
            if (entityManager.HasComponent<Scene::NameComponent>(root)) {
                entityManager.GetComponent<Scene::NameComponent>(root).name = assetStem;
            } else {
                entityManager.AddComponent<Scene::NameComponent>(root, Scene::NameComponent{assetStem});
            }
        }
    }
    InvalidateHierarchyFilterCache();
    SelectEntity(root);
    return root;
}

Editor::EntityDocumentScope::EntityDocumentScope(Editor& editor) : m_editor(editor) {
    if (m_editor.m_entityScene == nullptr || m_editor.m_entityDocumentActive) {
        return;
    }
    m_editor.SwapDocumentState();
    m_editor.m_entityDocumentActive = true;
    m_entered = true;
}

Editor::EntityDocumentScope::~EntityDocumentScope() {
    if (!m_entered) {
        return;
    }
    m_editor.SwapDocumentState();
    m_editor.m_entityDocumentActive = false;
    if (m_editor.m_entityLevelRefreshPending && m_editor.m_entityEditSession.has_value()) {
        m_editor.m_entityLevelRefreshPending = false;
        const std::string status = m_editor.m_contentStatusMessage;
        if (m_editor.RefreshLevelCopies(m_editor.m_entityEditSession->guid, m_editor.m_entityEditSession->assetPath) == 0U) {
            m_editor.m_contentStatusMessage = status;
        }
    }
    if (m_editor.m_entityClosePending.has_value()) {
        m_editor.m_entityClosePending.reset();
        m_editor.FinishEntityEditorClose();
    }
}

void Editor::SwapDocumentState() {
    DocumentState& other = m_otherDocument;
    std::swap(m_services, other.services);
    std::swap(m_selectedEntities, other.selectedEntities);
    std::swap(m_selectedEntity, other.selectedEntity);
    std::swap(m_undoHistory, other.undoHistory);
    std::swap(m_redoHistory, other.redoHistory);
    std::swap(m_sceneDirty, other.sceneDirty);
    std::swap(m_lockedFolders, other.lockedFolders);
    std::swap(m_lastUsedFolder, other.lastUsedFolder);
    std::swap(m_pendingComponentChangeEntity, other.pendingComponentChangeEntity);
    std::swap(m_hierarchyPendingRowOpen, other.hierarchyPendingRowOpen);
    std::swap(m_previewCamera, other.previewCamera);
    // A value still being dragged belongs to the document it was started in, and stays with it.
    std::swap(m_componentPropertyPreview, other.componentPropertyPreview);
    // Each document has its own view: camera requests, gizmo mode, grid, shading, preview lighting.
    std::swap(m_viewportOverlayState, m_entityViewportOverlayState);
    // The scene tree's caches and one-shot gestures describe the document they were made for.
    m_hierarchyRenameEntity = Scene::kInvalidEntity;
    m_hierarchyRenameBuffer.clear();
    m_hierarchyRevealedEntity = Scene::kInvalidEntity;
    m_hierarchyRevealAncestors.clear();
    m_hierarchyRevealPending = false;
    InvalidateHierarchyFilterCache();
}

bool Editor::OpenEntityEditor(const std::filesystem::path& assetPath) {
    if (m_entityEditSession.has_value() && m_entityEditSession->assetPath == assetPath) {
        m_entityEditSession->focusPending = true;
        return true;
    }
    std::error_code error;
    if (m_state != EditorState::Running || m_playModeState != EditorPlayModeState::Edit ||
        m_entityDocumentActive || !IsAuthoringCommandAllowed() || !std::filesystem::is_regular_file(assetPath, error)) {
        return false;
    }
    if (!m_documentSceneFactory) {
        m_contentStatusMessage = "This editor cannot open entities: it has no scene for them.";
        return false;
    }
    if (m_entityEditSession.has_value() && !CloseEntityEditor(true)) {
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreview());

    const Asset::AssetGuid guid = m_services->GetAssetDatabase().Register(assetPath);
    if (guid == Asset::kInvalidAssetGuid) {
        return false;
    }
    // The entity gets a scene of its own: the level stays exactly as it is, open and editable.
    m_entityScene = m_documentSceneFactory();
    if (m_entityScene == nullptr) {
        return false;
    }
    m_otherDocument = DocumentState{};
    m_otherDocument.services = &m_entityScene->GetServices();

    EntityEditSession session;
    session.assetPath = assetPath;
    session.guid = guid;
    m_entityEditSession = std::move(session);

    Scene::Entity loadedRoot = Scene::kInvalidEntity;
    {
        const EntityDocumentScope scope(*this);
        loadedRoot = LoadEntityIntoDocument(guid);
        m_sceneDirty = false;
    }
    if (loadedRoot == Scene::kInvalidEntity) {
        m_entityEditSession.reset();
        m_otherDocument = DocumentState{};
        if (m_documentSceneClosing) {
            m_documentSceneClosing(m_entityScene->GetServices());
        }
        m_entityScene.reset();
        m_contentStatusMessage = "Could not read " + assetPath.filename().string() + ".";
        return false;
    }
    m_entityEditSession->rootEntity = loadedRoot;
    m_entityEditSession->focusPending = true;
    // The script editor worked on scene objects; the entity brings its own.
    return true;
}

bool Editor::IsEntityEditorOpen() const noexcept {
    return m_entityEditSession.has_value();
}

std::filesystem::path Editor::GetEntityEditorAssetPath() const {
    return m_entityEditSession.has_value() ? m_entityEditSession->assetPath : std::filesystem::path{};
}

Scene::Entity Editor::GetEntityEditorRoot() {
    if (!m_entityEditSession.has_value()) {
        return Scene::kInvalidEntity;
    }
    const EntityDocumentScope scope(*this);
    const Scene::Entity sceneRoot = GetDocumentSceneRoot();
    if (sceneRoot != Scene::kInvalidEntity) {
        const std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(m_services->GetEntityManager(), sceneRoot);
        return children.size() == 1U ? children.front() : Scene::kInvalidEntity;
    }
    const std::vector<Scene::Entity> roots = GetDocumentRoots();
    return roots.size() == 1U ? roots.front() : Scene::kInvalidEntity;
}

bool Editor::SaveEntityEditor() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    // Remember the pose being shown: the entity's thumbnail stands in it once saved.
    {
        const EntityDocumentScope poseScope(*this);
        bool posed = false;
        m_services->GetEntityManager().ForEach<Scene::Skeleton3DComponent>(
            [&](const Scene::Entity, Scene::Skeleton3DComponent& skeleton) {
                if (skeleton.pose.size() != skeleton.bones.size() || skeleton.bones.empty() ||
                    skeleton.skeletonAssetPath.empty()) {
                    return;
                }
                auto& shown = m_thumbnailPoses[std::filesystem::path{skeleton.skeletonAssetPath}.filename().generic_string()];
                shown.clear();
                for (std::size_t bone = 0U; bone < skeleton.bones.size(); ++bone) {
                    shown[skeleton.bones[bone].name] = skeleton.pose[bone];
                }
                posed = true;
            });
        if (posed) {
            m_meshThumbnailCache.clear();
            ++m_meshThumbnailGeneration;
        }
    }
    // The entity is saved in its own pose, never the pose the Sequencer is previewing on it (the
    // preview writes it again next frame).
    RestoreAnimationTimelineObject();
    if (m_saveOpenBlueprint) {
        m_saveOpenBlueprint();   // Ctrl+S saves the graph and the entity together
    }
    // What a simulation did is not the entity: it is put back before anything is written.
    static_cast<void>(StopEntitySimulation());
    const EntityDocumentScope scope(*this);
    static_cast<void>(CommitComponentPropertyPreview());
    const Scene::Entity root = GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        m_contentStatusMessage = "An entity has exactly one root object: put the other top-level objects under it, then save.";
        return false;
    }
    // A Widget Blueprint's graph lives in the same file: kept across the widgets being written.
    const bool widget = Asset::IsWidgetBlueprintPath(m_entityEditSession->assetPath.generic_string());
    std::optional<Asset::BlueprintAsset> widgetGraph;
    if (widget) {
        widgetGraph = Asset::LoadWidgetBlueprintGraph(m_entityEditSession->assetPath);
    }
    const Asset::AssetGuid guid = m_services->GetPrefabSystem().SavePrefab(
        m_services->GetEntityManager(), m_services->GetAssetDatabase(), root, m_entityEditSession->assetPath);
    if (guid != Asset::kInvalidAssetGuid && widget && widgetGraph.has_value()) {
        static_cast<void>(Asset::SaveWidgetBlueprintGraph(m_entityEditSession->assetPath, *widgetGraph));
    }
    if (guid == Asset::kInvalidAssetGuid) {
        m_contentStatusMessage = "Could not save " + m_entityEditSession->assetPath.filename().string() + ".";
        return false;
    }
    m_entityEditSession->savedOnce = true;
    m_sceneDirty = false;
    // The level's copies follow every save, not only the close (done once the level is active).
    m_entityLevelRefreshPending = true;
    m_contentStatusMessage = "Saved " + m_entityEditSession->assetPath.filename().string() + ".";
    return true;
}

bool Editor::RevertEntityEditor() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (m_saveOpenBlueprint) {
        m_saveOpenBlueprint();   // Revert is the entity's, not the Blueprint's edits
    }
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->scripts.clear();
        m_entityEditSession->simulation.reset();
    }
    const EntityDocumentScope scope(*this);
    StopAnimationTimelinePreview();
    m_timeline = AnimationTimelineState{};
    m_componentPropertyPreview.reset();
    ClearDocumentScene();
    const Scene::Entity loadedRoot = LoadEntityIntoDocument(m_entityEditSession->guid);
    if (loadedRoot == Scene::kInvalidEntity) {
        m_contentStatusMessage = "Could not read " + m_entityEditSession->assetPath.filename().string() + " again.";
        return false;
    }
    m_entityEditSession->rootEntity = loadedRoot;
    m_entityEditSession->focusPending = true;
    ClearHistory();
    m_sceneDirty = false;
    // Object handles are new; the script text goes back to what is on disk.
    m_entityEditSession->problems.clear();
    m_entityEditSession->compiled = false;
    return true;
}

bool Editor::CloseEntityEditor(const bool save) {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (save && HasEntityEditorUnsavedChanges() && !SaveEntityEditor()) {
        m_contentStatusMessage = "Could not save entity changes before closing.";
        return false;
    }
    // Asked from inside the entity's own scope (its window, its shortcuts): the scene it is using
    // goes away once that scope hands the level back.
    if (m_entityDocumentActive) {
        m_entityClosePending = save;
        return true;
    }
    FinishEntityEditorClose();
    return true;
}

void Editor::FinishEntityEditorClose() {
    if (!m_entityEditSession.has_value() || m_entityDocumentActive) {
        return;
    }
    {
        const EntityDocumentScope scope(*this);
        StopAnimationTimelinePreview();
    }
    m_timeline = AnimationTimelineState{};
    m_timelineOwnsKeys = false;
    m_componentPropertyPreview.reset();
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->scripts.clear();   // before the scene they run on goes
        m_entityEditSession->simulation.reset();
    }
    const EntityEditSession session = std::move(*m_entityEditSession);
    m_entityEditSession.reset();
    m_otherDocument = DocumentState{};
    if (m_documentSceneClosing && m_entityScene != nullptr) {
        m_documentSceneClosing(m_entityScene->GetServices());
    }
    m_entityScene.reset();

    // The file changed: the copies of it in the level follow it.
    if (session.savedOnce) {
        RefreshLevelCopies(session.guid, session.assetPath);
    }
}

std::size_t Editor::RefreshLevelCopies(const Asset::AssetGuid guid, const std::filesystem::path& assetPath) {
    if (m_entityDocumentActive || guid == Asset::kInvalidAssetGuid) {
        return 0U;   // only on the level's own scene
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    std::vector<Scene::Entity> instances;
    entityManager.ForEach<Scene::PrefabInstanceComponent>(
        [&instances, guid](const Scene::Entity entity, Scene::PrefabInstanceComponent& instance) {
            if (instance.sourcePrefabGuid == guid) {
                instances.push_back(entity);
            }
        });
    std::size_t refreshed = 0U;
    for (const Scene::Entity instance : instances) {
        // Where the copy stands and what it is called are the level's, not the file's: kept.
        const std::optional<Scene::TransformComponent> placed =
            entityManager.HasComponent<Scene::TransformComponent>(instance)
                ? std::optional<Scene::TransformComponent>{entityManager.GetComponent<Scene::TransformComponent>(instance)}
                : std::nullopt;
        const std::optional<std::string> name =
            entityManager.HasComponent<Scene::NameComponent>(instance)
                ? std::optional<std::string>{entityManager.GetComponent<Scene::NameComponent>(instance).name}
                : std::nullopt;
        const Scene::PrefabRefreshResult result = m_services->GetPrefabSystem().RefreshInstance(
            entityManager, sceneGraph, m_services->GetAssetDatabase(), instance, true);
        if (result.code != Scene::PrefabRefreshCode::Refreshed || !entityManager.IsAlive(result.rootEntity)) {
            continue;
        }
        if (placed.has_value() && entityManager.HasComponent<Scene::TransformComponent>(result.rootEntity)) {
            sceneGraph.SetLocalTransform(entityManager, result.rootEntity, *placed);
        }
        if (name.has_value() && entityManager.HasComponent<Scene::NameComponent>(result.rootEntity)) {
            entityManager.GetComponent<Scene::NameComponent>(result.rootEntity).name = *name;
        }
        ++refreshed;
    }
    if (refreshed > 0U) {
        m_sceneDirty = true;
        InvalidateHierarchyFilterCache();
        m_contentStatusMessage = "Updated " + std::to_string(refreshed) + " placed " + (refreshed == 1U ? "copy" : "copies") + " of " +
                                 assetPath.filename().string() + " in the level.";
    }
    return refreshed;
}

Editor::EntityEditorTab Editor::GetEntityEditorTab() const noexcept {
    return m_entityEditSession.has_value() ? m_entityEditSession->tab : EntityEditorTab::Viewport;
}

void Editor::SetEntityEditorTab(const EntityEditorTab tab) noexcept {
    if (m_entityEditSession.has_value()) {
        m_entityEditSession->tab = tab;
        m_entityEditSession->forceTab = true;
    }
}

bool Editor::HasEntityEditorUnsavedChanges() const noexcept {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    const bool entityDirty = m_entityDocumentActive ? m_sceneDirty : m_otherDocument.sceneDirty;
    // An unsaved Blueprint counts: closing must never drop the nodes made in it.
    const bool blueprintDirty = m_openBlueprintDirty && m_openBlueprintDirty();
    return entityDirty || blueprintDirty;
}

std::vector<Scene::Entity> Editor::CollectEntityEditorObjects() {
    std::vector<Scene::Entity> objects;
    if (!m_entityEditSession.has_value()) {
        return objects;
    }
    const EntityDocumentScope scope(*this);
    // Depth first, children in their authored order: the order the Scene tree shows.
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity sceneRoot = GetDocumentSceneRoot();
    std::vector<Scene::Entity> pending;
    if (sceneRoot != Scene::kInvalidEntity) {
        pending = m_services->GetSceneGraph().GetChildren(entityManager, sceneRoot);
    } else {
        pending = GetDocumentRoots();
    }
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::Entity entity = pending.back();
        pending.pop_back();
        objects.push_back(entity);
        std::vector<Scene::Entity> children = m_services->GetSceneGraph().GetChildren(entityManager, entity);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    return objects;
}

std::vector<Editor::EntityCompileProblem> Editor::CollectEntityStructureProblems() {
    std::vector<EntityCompileProblem> problems;
    if (!m_entityEditSession.has_value()) {
        return problems;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& graph = m_services->GetSceneGraph();
    using Kind = Scene::Objects::SceneObjectKind;
    for (const Scene::Entity entity : CollectEntityEditorObjects()) {
        const Kind kind = Scene::ResolveSceneObjectKind(entityManager, entity);
        if (kind != Kind::Character3D && kind != Kind::Player3D && kind != Kind::Static3D && kind != Kind::Rigid3D &&
            kind != Kind::Kinematic3D) {
            continue;
        }
        // A collider on the body itself or anywhere under it.
        bool collides = false;
        std::vector<Scene::Entity> pending{entity};
        for (std::size_t next = 0U; next < pending.size() && next < 4096U && !collides; ++next) {
            collides = entityManager.HasComponent<Scene::ColliderComponent>(pending[next]);
            const std::vector<Scene::Entity> children = graph.GetChildren(entityManager, pending[next]);
            pending.insert(pending.end(), children.begin(), children.end());
        }
        if (collides) {
            continue;
        }
        EntityCompileProblem problem;
        problem.entity = entity;
        problem.objectName = GetEntityDisplayLabel(entity);
        problem.message = std::string(GetObjectTypeName(entity)) + " '" + problem.objectName +
                          "' has no Collider3D: add one so it can collide.";
        problem.structural = true;
        problems.push_back(std::move(problem));
    }
    return problems;
}

bool Editor::RefreshEntityStructureProblems() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    std::vector<EntityCompileProblem> now;
    {
        const EntityDocumentScope scope(*this);
        now = CollectEntityStructureProblems();
    }
    std::vector<EntityCompileProblem>& problems = m_entityEditSession->problems;
    std::vector<std::string> before;
    for (const EntityCompileProblem& problem : problems) {
        if (problem.structural) {
            before.push_back(problem.message);
        }
    }
    std::vector<std::string> after;
    for (const EntityCompileProblem& problem : now) {
        after.push_back(problem.message);
    }
    if (before == after) {
        return false;
    }
    std::erase_if(problems, [](const EntityCompileProblem& problem) { return problem.structural; });
    problems.insert(problems.begin(), now.begin(), now.end());
    return true;
}

std::size_t Editor::CompileEntityEditor() {
    if (!m_entityEditSession.has_value()) {
        return 0U;
    }
    const EntityDocumentScope scope(*this);
    std::vector<EntityCompileProblem> problems;
    const std::vector<Scene::Entity> objects = CollectEntityEditorObjects();
    if (GetEntityEditorRoot() == Scene::kInvalidEntity) {
        EntityCompileProblem problem;
        problem.message = objects.empty() ? "The entity has no objects." :
                                          "An entity has exactly one root object: put the other top-level objects under it.";
        problems.push_back(std::move(problem));
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const Scene::Entity entity : objects) {
        if (!entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
        if (path.empty()) {
            continue;
        }
        EntityCompileProblem base;
        base.entity = entity;
        base.objectName = GetEntityDisplayLabel(entity);
        base.scriptPath = path;
        if (IsGraphScriptPath(path)) {
            // A Blueprint Class: the graph being edited (or the file), translated and compiled
            // against this object; each problem points at its node.
            std::optional<Asset::BlueprintAsset> blueprint =
                m_openBlueprintProvider ? m_openBlueprintProvider(path) : std::optional<Asset::BlueprintAsset>{};
            if (!blueprint.has_value()) {
                if (const std::optional<std::string> bytes = ReadProjectTextFile(path); bytes.has_value()) {
                    const auto* const data = reinterpret_cast<const std::byte*>(bytes->data());
                    blueprint = DecodeGraphScript(path, std::vector<std::byte>(data, data + bytes->size()));
                }
            }
            if (!blueprint.has_value()) {
                base.message = "The Blueprint at " + path + " cannot be read.";
                problems.push_back(std::move(base));
                continue;
            }
            const Scene::Objects::SceneObjectDescriptor* const descriptor =
                Scene::Objects::FindSceneObjectDescriptor(Scene::ResolveSceneObjectKind(entityManager, entity));
            const auto titled = [&blueprint](const std::uint32_t node, const std::string& message) {
                for (const Asset::BlueprintNode& n : blueprint->nodes) {
                    if (n.id == node) {
                        const Asset::BlueprintNodeType* const type = Asset::FindBlueprintNodeType(n.type);
                        const std::string title = type == nullptr ? n.type : n.variable.empty() ? type->title : type->title + " " + n.variable;
                        return title + ": " + message;
                    }
                }
                return message;
            };
            Core::BlueprintObjectHost host(entityManager, nullptr, entity);
            const Asset::BlueprintCompilation compiled = Asset::CompileBlueprintGraph(
                *blueprint, std::filesystem::path(path).stem().string(),
                descriptor != nullptr ? descriptor->displayName : "Object3D", host);
            for (const auto& [node, message] : compiled.problems) {
                EntityCompileProblem problem = base;
                problem.blueprintNode = node;
                problem.message = titled(node, message);
                problems.push_back(std::move(problem));
            }
            // A graph's diagnostics name nodes: the position's `line` is the node's id.
            for (const Blueprint::Diagnostic& diagnostic : compiled.result.diagnostics) {
                EntityCompileProblem problem = base;
                problem.blueprintNode = diagnostic.at.line;
                problem.message = titled(problem.blueprintNode, diagnostic.message);
                problems.push_back(std::move(problem));
            }
            continue;
        }
        base.message = "The script is not a Blueprint (.uvbp).";
        problems.push_back(std::move(base));
    }
    {
        std::vector<EntityCompileProblem> structure = CollectEntityStructureProblems();
        problems.insert(problems.begin(), structure.begin(), structure.end());
    }
    m_entityEditSession->problems = std::move(problems);
    m_entityEditSession->compiled = true;
    return m_entityEditSession->problems.size();
}

std::vector<EntityDiffRow> Editor::DiffEntityEditor() {
    if (!m_entityEditSession.has_value()) {
        return {};
    }
    const EntityDocumentScope scope(*this);
    static_cast<void>(CommitComponentPropertyPreview());
    const auto payloadText = [](const std::vector<std::byte>& payload) {
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    };
    std::string saved;
    if (const auto file = Asset::ReadUveFile(m_entityEditSession->assetPath); file.has_value()) {
        saved = payloadText(file->second);
    }
    std::string current;
    const Scene::Entity root = GetEntityEditorRoot();
    if (root != Scene::kInvalidEntity) {
        const std::optional<Scene::SceneSnapshot> snapshot = m_services->GetSceneSerializer().Capture(
            m_services->GetEntityManager(), {root}, Asset::AssetKind::Prefab);
        if (snapshot.has_value()) {
            if (const auto decoded = Asset::DecodeUveFileEnvelope(snapshot->bytes); decoded.has_value()) {
                current = payloadText(decoded->second);
            }
        }
    }
    return DiffEntityPayloads(saved, current);
}

std::vector<Editor::EntityFindRow> Editor::FindInEntityEditor(const std::string_view query) {
    std::vector<EntityFindRow> rows;
    if (!m_entityEditSession.has_value() || query.empty()) {
        return rows;
    }
    const EntityDocumentScope scope(*this);
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string needle = lower(std::string(query));
    const auto matches = [&](const std::string& text) { return lower(text).find(needle) != std::string::npos; };
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const Scene::Entity entity : CollectEntityEditorObjects()) {
        const std::string object = GetEntityDisplayLabel(entity);
        if (matches(object)) {
            rows.push_back(EntityFindRow{entity, object, "Name", object});
        }
        const std::string type(GetObjectTypeName(entity));
        if (matches(type)) {
            rows.push_back(EntityFindRow{entity, object, "Type", type});
        }
        // Its components as the file holds them: every property name and value can be found.
        const std::optional<Scene::SceneSnapshot> snapshot =
            m_services->GetSceneSerializer().Capture(entityManager, {entity}, Asset::AssetKind::Prefab);
        if (!snapshot.has_value()) {
            continue;
        }
        const auto decoded = Asset::DecodeUveFileEnvelope(snapshot->bytes);
        if (!decoded.has_value()) {
            continue;
        }
        const nlohmann::json doc = nlohmann::json::parse(
            std::string(reinterpret_cast<const char*>(decoded->second.data()), decoded->second.size()), nullptr, false);
        if (doc.is_discarded() || !doc.contains("entities") || !doc["entities"].is_array()) {
            continue;
        }
        for (const nlohmann::json& item : doc["entities"]) {
            // The captured object itself (its descendants are listed as themselves).
            const nlohmann::json& components = item.contains("components") ? item["components"] : nlohmann::json::object();
            const nlohmann::json* hierarchyJson = nullptr;
            if (components.is_object()) {
                if (const auto h = components.find("HierarchyComponent"); h != components.end() && h->is_object()) {
                    hierarchyJson = &(*h);
                } else if (const auto hl = components.find("HierarchyComponentUVE"); hl != components.end() && hl->is_object()) {
                    hierarchyJson = &(*hl);
                }
            }
            if (hierarchyJson == nullptr ||
                hierarchyJson->value("parentLocalId", static_cast<std::int64_t>(0)) >= 0) {
                continue;
            }
            for (auto c = components.begin(); c != components.end(); ++c) {
                std::string component = c.key();
                if (component.size() > 3U && component.ends_with("UVE")) {
                    component.resize(component.size() - 3U);
                }
                if (component == "HierarchyComponent" || component == "NameComponent") {
                    continue;
                }
                if (component.size() > 12U && component.ends_with("Component")) {
                    component.resize(component.size() - 12U);
                }
                if (matches(component)) {
                    rows.push_back(EntityFindRow{entity, object, component, component});
                }
                if (!c.value().is_object()) {
                    continue;
                }
                for (auto p = c.value().begin(); p != c.value().end(); ++p) {
                    const std::string value = p.value().is_string() ? p.value().get<std::string>() : p.value().dump();
                    if (matches(p.key()) || (p.value().is_string() && matches(value))) {
                        rows.push_back(EntityFindRow{entity, object, component + "." + p.key(), value});
                    }
                }
            }
            break;
        }
    }
    return rows;
}

std::optional<Editor::EntityScriptSource> Editor::ReadEntityScriptSource(const std::string& path, const Scene::Entity entity) const {
    EntityScriptSource source;
    if (IsGraphScriptPath(path)) {
        std::optional<Asset::BlueprintAsset> blueprint =
            m_openBlueprintProvider ? m_openBlueprintProvider(path) : std::optional<Asset::BlueprintAsset>{};
        if (!blueprint.has_value()) {
            if (const std::optional<std::string> bytes = ReadProjectTextFile(path); bytes.has_value()) {
                const auto* const data = reinterpret_cast<const std::byte*>(bytes->data());
                blueprint = DecodeGraphScript(path, std::vector<std::byte>(data, data + bytes->size()));
            }
        }
        if (!blueprint.has_value()) {
            return std::nullopt;
        }
        const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
        const Scene::Objects::SceneObjectDescriptor* const descriptor =
            Scene::Objects::FindSceneObjectDescriptor(Scene::ResolveSceneObjectKind(entityManager, entity));
        source.objectName = std::filesystem::path(path).stem().string();
        source.objectKind = descriptor != nullptr ? descriptor->displayName : "Object3D";
        source.graph = std::move(blueprint);
        return source;
    }
    return std::nullopt; // only Blueprints run
}

const std::vector<Editor::EntityCompileProblem>& Editor::GetEntityEditorProblems() const noexcept {
    static const std::vector<EntityCompileProblem> kNone;
    return m_entityEditSession.has_value() ? m_entityEditSession->problems : kNone;
}

bool Editor::HasEntityEditorCompiled() const noexcept {
    return m_entityEditSession.has_value() && m_entityEditSession->compiled;
}

std::vector<Editor::EntitySignalRow> Editor::GetEntityEditorSignals() {
    std::vector<EntitySignalRow> rows;
    const EntityDocumentScope scope(*this);
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const Scene::Entity entity : CollectEntityEditorObjects()) {
        if (!entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
        if (!IsGraphScriptPath(path)) {
            continue;
        }
        const std::optional<EntityScriptSource> source = ReadEntityScriptSource(path, entity);
        if (!source.has_value()) {
            continue;
        }
        const std::string objectName = GetEntityDisplayLabel(entity);
        // Whatever parsed is listed, even while the file has mistakes elsewhere.
        const Blueprint::File listed = Asset::LowerBlueprintGraph(*source->graph, source->objectName, source->objectKind).file;
        for (const Blueprint::Handler& handler : listed.handlers) {
            EntitySignalRow row;
            row.entity = entity;
            row.objectName = objectName;
            row.scriptPath = path;
            row.event = handler.event;
            if (!handler.params.empty()) {
                row.params = "(";
                for (std::size_t index = 0U; index < handler.params.size(); ++index) {
                    row.params += (index == 0U ? "" : ", ") + handler.params[index].name;
                }
                row.params += ")";
            }
            row.line = handler.at.line;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

// ------------------------------------------------------------------------------------ simulate


bool Editor::PlayEntitySimulation() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (m_entityEditSession->simulation == nullptr && m_saveOpenBlueprint) {
        m_saveOpenBlueprint();   // the run and the editor read the same graph, and Stop keeps it
    }
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->paused = false;
        return true;
    }
    const EntityDocumentScope scope(*this);
    static_cast<void>(CommitComponentPropertyPreview());
    const Scene::Entity root = GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        m_contentStatusMessage = "An entity has exactly one root object: put the other top-level objects under it, then simulate.";
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // Stop any live Edit-mode ragdoll preview before taking the Stop-restore snapshot.
    std::vector<Scene::Entity> previewRagdolls;
    entityManager.ForEach<Scene::Ragdoll3DComponent>(
        [&previewRagdolls](const Scene::Entity e, const Scene::Ragdoll3DComponent& r) {
            if (r.editorPreviewSimulating) {
                previewRagdolls.push_back(e);
            }
        });
    for (const Scene::Entity e : previewRagdolls) {
        static_cast<void>(Scene::StopRagdollEditorPreview(entityManager, e));
    }
    m_services->GetSceneGraph().Update(entityManager);
    const std::optional<Scene::SceneSnapshot> snapshot =
        m_services->GetSceneSerializer().Capture(entityManager, {root}, Asset::AssetKind::Prefab);
    if (!snapshot.has_value()) {
        m_contentStatusMessage = "Could not take the entity's state to simulate it.";
        return false;
    }
    auto simulation = std::make_shared<EntitySimulation>();
    simulation->snapshot = *snapshot;
    simulation->dirtyBefore = m_sceneDirty;
    std::size_t failed = 0U;
    std::size_t ragdollCount = 0U;
    std::string ragdollError;
    for (const Scene::Entity entity : CollectEntityEditorObjects()) {
        if (entityManager.HasComponent<Scene::Ragdoll3DComponent>(entity) &&
            entityManager.GetComponent<Scene::Ragdoll3DComponent>(entity).enabled) {
            // Same preparation the Details "Simulate Preview" runs: link the nearest skeleton and
            // generate bones when needed. A ragdoll left Kinematic would not fall, so simulation
            // drops it for the session; the snapshot restores the authored mode on Stop.
            bool built = Scene::PrepareRagdoll3D(entityManager, entity);
            Scene::Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Scene::Ragdoll3DComponent>(entity);
            if (built) {
                ragdoll.initialized = false;
                built = Scene::InitializeRagdoll3D(entityManager, entity);
            }
            if (built) {
                if (ragdoll.mode == Scene::RagdollMode::Kinematic) {
                    ragdoll.mode = Scene::RagdollMode::Simulating;
                }
                ragdoll.solver.Wake();
                // Standing back up goes to the pose the entity is simulated from: nothing animates it here.
                if (entityManager.IsAlive(ragdoll.skeleton) &&
                    entityManager.HasComponent<Scene::Skeleton3DComponent>(ragdoll.skeleton)) {
                    ragdoll.recoverTargetPose = Scene::GetSkeletonCurrentPose(
                        entityManager.GetComponent<Scene::Skeleton3DComponent>(ragdoll.skeleton));
                    ragdoll.recoverToCapturedPose = true;
                    ragdoll.restSeconds = 0.0F;
                }
                ++ragdollCount;
            } else if (ragdollError.empty()) {
                ragdollError = ragdoll.lastSetupError;
            }
        }
        if (!entityManager.HasComponent<Scene::ScriptComponent>(entity)) {
            continue;
        }
        const Scene::ScriptComponent& component = entityManager.GetComponent<Scene::ScriptComponent>(entity);
        if (!IsGraphScriptPath(component.scriptAssetPath)) {
            continue;
        }
        std::optional<EntityScriptSource> source = ReadEntityScriptSource(component.scriptAssetPath, entity);
        if (!source.has_value()) {
            ++failed;
            continue;
        }
        EntitySimulation::Script script;
        script.entity = entity;
        EntitySimulation* const raw = simulation.get();
        script.host = std::make_unique<Core::BlueprintObjectHost>(
            entityManager, &m_services->GetInputSystem(), entity, nullptr, nullptr,
            [raw](const Scene::Entity other) -> Blueprint::BlueprintInstance* {
                for (EntitySimulation::Script& candidate : raw->scripts) {
                    if (candidate.entity == other) {
                        return candidate.instance.get();
                    }
                }
                return nullptr;
            });
        const Blueprint::CompileResult compiled =
            Asset::CompileBlueprintGraph(*source->graph, source->objectName, source->objectKind, *script.host).result;
        if (!compiled.IsSuccess()) {
            ++failed;
            continue;
        }
        script.instance = std::make_unique<Blueprint::BlueprintInstance>(compiled.program, *script.host);
        for (const Blueprint::FieldInfo& field : Blueprint::GetProgramFields(*compiled.program)) {
            const auto stored = component.exportValues.find(field.name);
            if (field.kind != Blueprint::FieldKind::Export || stored == component.exportValues.end()) {
                continue;
            }
            if (const std::optional<Blueprint::Value> value = Blueprint::ParseValueText(stored->second, field.type)) {
                static_cast<void>(script.instance->SetField(field.name, *value));
            }
        }
        simulation->scripts.push_back(std::move(script));
    }
    m_entityEditSession->simulation = std::move(simulation);
    const std::size_t running = m_entityEditSession->simulation->scripts.size();
    m_contentStatusMessage = "Simulating: " + std::to_string(running) + (running == 1U ? " script" : " scripts") +
                             (ragdollCount > 0U
                                  ? ", " + std::to_string(ragdollCount) + (ragdollCount == 1U ? " ragdoll" : " ragdolls")
                                  : std::string()) +
                             (failed > 0U ? ", " + std::to_string(failed) + " did not compile (Compile lists them)" : "") + "." +
                             (ragdollError.empty() ? std::string() : " Ragdoll3D: " + ragdollError);
    return true;
}

void Editor::PauseEntitySimulation() {
    if (m_entityEditSession.has_value() && m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->paused = true;
    }
}

bool Editor::StopEntitySimulation() {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr) {
        return false;
    }
    const EntityDocumentScope scope(*this);
    const std::shared_ptr<EntitySimulation> simulation = std::move(m_entityEditSession->simulation);
    m_entityEditSession->simulation.reset();
    // The scripts go first: they hold the objects about to be replaced.
    const Scene::SceneSnapshot snapshot = simulation->snapshot;
    const bool dirtyBefore = simulation->dirtyBefore;
    simulation->scripts.clear();
    m_componentPropertyPreview.reset();
    ClearDocumentScene();
    const std::vector<Scene::Entity> restored =
        m_services->GetSceneSerializer().Restore(m_services->GetEntityManager(), snapshot);
    if (restored.empty()) {
        m_contentStatusMessage = "Could not put the entity back; Revert reads it from its file.";
        return false;
    }
    m_entityEditSession->rootEntity = restored.front();
    // The objects are new: what undo remembered named the old ones.
    ClearHistory();
    m_sceneDirty = dirtyBefore;
    InvalidateHierarchyFilterCache();
    SelectEntity(restored.front());
    m_contentStatusMessage = "Simulation stopped: the entity is as it was.";
    return true;
}

Editor::EntitySimulationState Editor::GetEntitySimulationState() const noexcept {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr) {
        return EntitySimulationState::Stopped;
    }
    return m_entityEditSession->simulation->paused ? EntitySimulationState::Paused : EntitySimulationState::Running;
}

void Editor::TickEntitySimulation(const double deltaSeconds) {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr ||
        m_entityEditSession->simulation->paused || !(deltaSeconds >= 0.0)) {
        return;
    }
    const EntityDocumentScope scope(*this);
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const double dt = std::min(deltaSeconds, 0.1);
    for (EntitySimulation::Script& script : m_entityEditSession->simulation->scripts) {
        if (script.instance == nullptr || !entityManager.IsAlive(script.entity)) {
            continue;
        }
        if (!script.ready) {
            script.ready = true;
            static_cast<void>(script.instance->RaiseEvent("ready"));
        }
        const Blueprint::Value args[] = {dt};
        static_cast<void>(script.instance->RaiseEvent("tick", args));
        script.instance->Advance(dt);
    }
    sceneGraph.Update(entityManager);
    if (dt > 0.0) {
        const float dtFloat = static_cast<float>(dt);
        static_cast<void>(Scene::UpdateRagdoll3DObjects(entityManager, dtFloat, /*editorPreviewOnly=*/false,
                                                        /*fallbackFloorPlaneY=*/0.0F));
        static_cast<void>(Scene::SyncBoneAttachment3DObjects(entityManager, sceneGraph));
        static_cast<void>(Scene::UpdateSpringBoneSimulator3DObjects(entityManager, dtFloat));
        sceneGraph.Update(entityManager);
    }
}

} // namespace UVE::Editor
