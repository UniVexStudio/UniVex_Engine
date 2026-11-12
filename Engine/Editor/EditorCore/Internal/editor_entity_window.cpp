// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor: one entity asset (.uventity / .uvprefab) opened on its own, in its own OS
// window, in a scene of its own (Core::DocumentSceneUVE) beside the level. Inside an
// EntityDocumentScopeUVE the entity is the document - its scene, selection, undo and dirty state
// stand in for the level's - so the Scene tree, the Inspector, gizmos and undo all work on the
// entity with no second set of tools, while the level stays open and editable in its own window.

#include "editor.h"

#include "blueprint_asset.h"
#include "blueprint_nodes.h"
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
#include "animation_graph_component.h"
#include "name_component.h"
#include "prefab_instance_component.h"
#include "script_component.h"
#include "uvscript_object_host.h"
#include "uvscript_compiler.h"
#include "uvscript_instance.h"
#include "uvscript_parser.h"
#include "uve_file_envelope.h"

#include <nlohmann/json.hpp>

namespace UVE::Editor {
namespace {

[[nodiscard]] bool IsUVScriptFileUVE(const std::string_view path) noexcept { return path.ends_with(".uvs"); }


} // namespace

/// The Entity Editor's Simulate: each scripted object's compiled UVScript, and the entity as it
/// was when Play was pressed.
struct EntitySimulationUVE final {
    struct ScriptUVE final {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        std::unique_ptr<Core::UVScriptObjectHostUVE> host;
        std::unique_ptr<UVScript::ScriptInstanceUVE> instance;
        bool ready = false;
    };
    Scene::SceneSnapshotUVE snapshot;
    bool dirtyBefore = false;
    bool paused = false;
    std::vector<ScriptUVE> scripts;
};

Scene::EntityUVE EditorUVE::LoadEntityIntoDocumentUVE(const Asset::AssetGuidUVE guid) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE root = m_services->GetPrefabSystemUVE().InstantiateUVE(
        entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), guid, Scene::kInvalidEntityUVE);
    if (root == Scene::kInvalidEntityUVE) {
        return root;
    }
    // Edited as itself, not as an instance of itself: saving writes a plain tree.
    if (entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(root)) {
        entityManager.RemoveComponentUVE<Scene::PrefabInstanceComponentUVE>(root);
    }
    // Synchronize root entity name with the open asset filename stem
    if (m_entityEditSession.has_value() && !m_entityEditSession->assetPath.empty()) {
        const std::string assetStem = m_entityEditSession->assetPath.stem().string();
        if (!assetStem.empty()) {
            if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(root)) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name = assetStem;
            } else {
                entityManager.AddComponentUVE<Scene::NameComponentUVE>(root, Scene::NameComponentUVE{assetStem});
            }
        }
    }
    InvalidateHierarchyFilterCacheUVE();
    SelectEntityUVE(root);
    return root;
}

EditorUVE::EntityDocumentScopeUVE::EntityDocumentScopeUVE(EditorUVE& editor) : m_editor(editor) {
    if (m_editor.m_entityScene == nullptr || m_editor.m_entityDocumentActive) {
        return;
    }
    m_editor.SwapDocumentStateUVE();
    m_editor.m_entityDocumentActive = true;
    m_entered = true;
}

EditorUVE::EntityDocumentScopeUVE::~EntityDocumentScopeUVE() {
    if (!m_entered) {
        return;
    }
    m_editor.SwapDocumentStateUVE();
    m_editor.m_entityDocumentActive = false;
    if (m_editor.m_entityClosePending.has_value()) {
        m_editor.m_entityClosePending.reset();
        m_editor.FinishEntityEditorCloseUVE();
    }
}

void EditorUVE::SwapDocumentStateUVE() {
    DocumentStateUVE& other = m_otherDocument;
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
    m_hierarchyRenameEntity = Scene::kInvalidEntityUVE;
    m_hierarchyRenameBuffer.clear();
    m_hierarchyRevealedEntity = Scene::kInvalidEntityUVE;
    m_hierarchyRevealAncestors.clear();
    m_hierarchyRevealPending = false;
    InvalidateHierarchyFilterCacheUVE();
}

bool EditorUVE::OpenEntityEditorUVE(const std::filesystem::path& assetPath) {
    if (m_entityEditSession.has_value() && m_entityEditSession->assetPath == assetPath) {
        m_entityEditSession->focusPending = true;
        return true;
    }
    std::error_code error;
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Edit ||
        m_entityDocumentActive || !IsAuthoringCommandAllowedUVE() || !std::filesystem::is_regular_file(assetPath, error)) {
        return false;
    }
    if (!m_documentSceneFactory) {
        m_contentStatusMessage = "This editor cannot open entities: it has no scene for them.";
        return false;
    }
    if (m_entityEditSession.has_value() && !CloseEntityEditorUVE(true)) {
        return false;
    }
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE()) {
        m_contentStatusMessage = "Save or close " + std::filesystem::path{m_openUVScript->path}.filename().string() +
                                 " first: it has unsaved text.";
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());

    const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(assetPath);
    if (guid == Asset::kInvalidAssetGuidUVE) {
        return false;
    }
    // The entity gets a scene of its own: the level stays exactly as it is, open and editable.
    m_entityScene = m_documentSceneFactory();
    if (m_entityScene == nullptr) {
        return false;
    }
    m_otherDocument = DocumentStateUVE{};
    m_otherDocument.services = &m_entityScene->GetServicesUVE();

    EntityEditSessionUVE session;
    session.assetPath = assetPath;
    session.guid = guid;
    m_entityEditSession = std::move(session);

    Scene::EntityUVE loadedRoot = Scene::kInvalidEntityUVE;
    {
        const EntityDocumentScopeUVE scope(*this);
        loadedRoot = LoadEntityIntoDocumentUVE(guid);
        m_sceneDirty = false;
    }
    if (loadedRoot == Scene::kInvalidEntityUVE) {
        m_entityEditSession.reset();
        m_otherDocument = DocumentStateUVE{};
        if (m_documentSceneClosing) {
            m_documentSceneClosing(m_entityScene->GetServicesUVE());
        }
        m_entityScene.reset();
        m_contentStatusMessage = "Could not read " + assetPath.filename().string() + ".";
        return false;
    }
    m_entityEditSession->rootEntity = loadedRoot;
    m_entityEditSession->focusPending = true;
    // The script editor worked on scene objects; the entity brings its own.
    CloseOpenUVScriptUVE();
    return true;
}

bool EditorUVE::IsEntityEditorOpenUVE() const noexcept {
    return m_entityEditSession.has_value();
}

std::filesystem::path EditorUVE::GetEntityEditorAssetPathUVE() const {
    return m_entityEditSession.has_value() ? m_entityEditSession->assetPath : std::filesystem::path{};
}

Scene::EntityUVE EditorUVE::GetEntityEditorRootUVE() {
    if (!m_entityEditSession.has_value()) {
        return Scene::kInvalidEntityUVE;
    }
    const EntityDocumentScopeUVE scope(*this);
    const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
    if (sceneRoot != Scene::kInvalidEntityUVE) {
        const std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), sceneRoot);
        return children.size() == 1U ? children.front() : Scene::kInvalidEntityUVE;
    }
    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    return roots.size() == 1U ? roots.front() : Scene::kInvalidEntityUVE;
}

bool EditorUVE::SaveEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    // What a simulation did is not the entity: it is put back before anything is written.
    static_cast<void>(StopEntitySimulationUVE());
    const EntityDocumentScopeUVE scope(*this);
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    const Scene::EntityUVE root = GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        m_contentStatusMessage = "An entity has exactly one root object: put the other top-level objects under it, then save.";
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetPrefabSystemUVE().SavePrefabUVE(
        m_services->GetEntityManagerUVE(), m_services->GetAssetDatabaseUVE(), root, m_entityEditSession->assetPath);
    if (guid == Asset::kInvalidAssetGuidUVE) {
        m_contentStatusMessage = "Could not save " + m_entityEditSession->assetPath.filename().string() + ".";
        return false;
    }
    m_entityEditSession->savedOnce = true;
    m_sceneDirty = false;
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE() && !SaveOpenUVScriptUVE()) {
        m_contentStatusMessage = "Saved the entity, but could not write " + m_openUVScript->path + ".";
        return false;
    }
    m_contentStatusMessage = "Saved " + m_entityEditSession->assetPath.filename().string() + ".";
    return true;
}

bool EditorUVE::RevertEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->scripts.clear();
        m_entityEditSession->simulation.reset();
    }
    const EntityDocumentScopeUVE scope(*this);
    m_componentPropertyPreview.reset();
    ClearDocumentSceneUVE();
    const Scene::EntityUVE loadedRoot = LoadEntityIntoDocumentUVE(m_entityEditSession->guid);
    if (loadedRoot == Scene::kInvalidEntityUVE) {
        m_contentStatusMessage = "Could not read " + m_entityEditSession->assetPath.filename().string() + " again.";
        return false;
    }
    m_entityEditSession->rootEntity = loadedRoot;
    m_entityEditSession->focusPending = true;
    ClearHistoryUVE();
    m_sceneDirty = false;
    // Object handles are new; the script text goes back to what is on disk.
    CloseOpenUVScriptUVE();
    m_entityEditSession->problems.clear();
    m_entityEditSession->compiled = false;
    return true;
}

bool EditorUVE::CloseEntityEditorUVE(const bool save) {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (save && HasEntityEditorUnsavedChangesUVE() && !SaveEntityEditorUVE()) {
        m_contentStatusMessage = "Could not save entity changes before closing.";
        return false;
    }
    // Asked from inside the entity's own scope (its window, its shortcuts): the scene it is using
    // goes away once that scope hands the level back.
    if (m_entityDocumentActive) {
        m_entityClosePending = save;
        return true;
    }
    FinishEntityEditorCloseUVE();
    return true;
}

void EditorUVE::FinishEntityEditorCloseUVE() {
    if (!m_entityEditSession.has_value() || m_entityDocumentActive) {
        return;
    }
    CloseOpenUVScriptUVE();
    m_componentPropertyPreview.reset();
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->scripts.clear();   // before the scene they run on goes
        m_entityEditSession->simulation.reset();
    }
    const EntityEditSessionUVE session = std::move(*m_entityEditSession);
    m_entityEditSession.reset();
    m_otherDocument = DocumentStateUVE{};
    if (m_documentSceneClosing && m_entityScene != nullptr) {
        m_documentSceneClosing(m_entityScene->GetServicesUVE());
    }
    m_entityScene.reset();

    // The file changed: instances of it in the level that have no local changes follow it.
    if (session.savedOnce) {
        Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        std::vector<Scene::EntityUVE> instances;
        entityManager.ForEachUVE<Scene::PrefabInstanceComponentUVE>(
            [&instances, &session](const Scene::EntityUVE entity, Scene::PrefabInstanceComponentUVE& instance) {
                if (instance.sourcePrefabGuid == session.guid) {
                    instances.push_back(entity);
                }
            });
        std::size_t refreshed = 0U;
        for (const Scene::EntityUVE instance : instances) {
            const Scene::PrefabRefreshResultUVE result = m_services->GetPrefabSystemUVE().RefreshInstanceUVE(
                entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), instance);
            refreshed += result.code == Scene::PrefabRefreshCodeUVE::Refreshed ? 1U : 0U;
        }
        if (refreshed > 0U) {
            m_sceneDirty = true;
            InvalidateHierarchyFilterCacheUVE();
            m_contentStatusMessage = "Updated " + std::to_string(refreshed) + " placed " +
                                     (refreshed == 1U ? "copy" : "copies") + " of " +
                                     session.assetPath.filename().string() + " in the level.";
        }
    }
}

EditorUVE::EntityEditorTabUVE EditorUVE::GetEntityEditorTabUVE() const noexcept {
    return m_entityEditSession.has_value() ? m_entityEditSession->tab : EntityEditorTabUVE::Viewport;
}

void EditorUVE::SetEntityEditorTabUVE(const EntityEditorTabUVE tab) noexcept {
    if (m_entityEditSession.has_value()) {
        m_entityEditSession->tab = tab;
        m_entityEditSession->forceTab = true;
    }
}

bool EditorUVE::HasEntityEditorUnsavedChangesUVE() const noexcept {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    const bool entityDirty = m_entityDocumentActive ? m_sceneDirty : m_otherDocument.sceneDirty;
    return entityDirty || (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE());
}

std::vector<Scene::EntityUVE> EditorUVE::CollectEntityEditorObjectsUVE() {
    std::vector<Scene::EntityUVE> objects;
    if (!m_entityEditSession.has_value()) {
        return objects;
    }
    const EntityDocumentScopeUVE scope(*this);
    // Depth first, children in their authored order: the order the Scene tree shows.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
    std::vector<Scene::EntityUVE> pending;
    if (sceneRoot != Scene::kInvalidEntityUVE) {
        pending = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, sceneRoot);
    } else {
        pending = GetDocumentRootsUVE();
    }
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::EntityUVE entity = pending.back();
        pending.pop_back();
        objects.push_back(entity);
        std::vector<Scene::EntityUVE> children = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    return objects;
}

std::size_t EditorUVE::CompileEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return 0U;
    }
    const EntityDocumentScopeUVE scope(*this);
    std::vector<EntityCompileProblemUVE> problems;
    const std::vector<Scene::EntityUVE> objects = CollectEntityEditorObjectsUVE();
    if (GetEntityEditorRootUVE() == Scene::kInvalidEntityUVE) {
        EntityCompileProblemUVE problem;
        problem.message = objects.empty() ? "The entity has no objects." :
                                          "An entity has exactly one root object: put the other top-level objects under it.";
        problems.push_back(std::move(problem));
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE entity : objects) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (path.empty()) {
            continue;
        }
        EntityCompileProblemUVE base;
        base.entity = entity;
        base.objectName = GetEntityDisplayLabelUVE(entity);
        base.scriptPath = path;
        if (path.ends_with(".uvbp")) {
            // A Blueprint Class: the graph being edited (or the file), translated and compiled
            // against this object; each problem points at its node.
            std::optional<Asset::BlueprintAssetUVE> blueprint =
                m_openBlueprintProviderUVE ? m_openBlueprintProviderUVE(path) : std::optional<Asset::BlueprintAssetUVE>{};
            if (!blueprint.has_value()) {
                if (const std::optional<std::string> bytes = ReadProjectTextFileUVE(path); bytes.has_value()) {
                    const auto* const data = reinterpret_cast<const std::byte*>(bytes->data());
                    blueprint = Asset::DecodeBlueprintAssetUVE(std::vector<std::byte>(data, data + bytes->size()));
                }
            }
            if (!blueprint.has_value()) {
                base.message = "The Blueprint at " + path + " cannot be read.";
                problems.push_back(std::move(base));
                continue;
            }
            const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
                Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
            const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(
                *blueprint, std::filesystem::path(path).stem().string(), descriptor != nullptr ? descriptor->displayName : "Object3D");
            const auto titled = [&blueprint](const std::uint32_t node, const std::string& message) {
                for (const Asset::BlueprintNodeUVE& n : blueprint->nodes) {
                    if (n.id == node) {
                        const Asset::BlueprintNodeTypeUVE* const type = Asset::FindBlueprintNodeTypeUVE(n.type);
                        const std::string title = type == nullptr ? n.type : n.variable.empty() ? type->title : type->title + " " + n.variable;
                        return title + ": " + message;
                    }
                }
                return message;
            };
            for (const auto& [node, message] : translated.problems) {
                EntityCompileProblemUVE problem = base;
                problem.blueprintNode = node;
                problem.message = titled(node, message);
                problems.push_back(std::move(problem));
            }
            Core::UVScriptObjectHostUVE host(entityManager, nullptr, entity);
            for (const UVScript::DiagnosticUVE& diagnostic : UVScript::CompileUVScriptSourceUVE(translated.source, host).diagnostics) {
                EntityCompileProblemUVE problem = base;
                problem.blueprintNode = translated.NodeAtLineUVE(diagnostic.at.line);
                problem.message = titled(problem.blueprintNode, diagnostic.message);
                problems.push_back(std::move(problem));
            }
            continue;
        }
        if (!IsUVScriptFileUVE(path)) {
            base.message = "The script is not a UVScript file (.uvs).";
            problems.push_back(std::move(base));
            continue;
        }
        // The text being edited is what the user means, saved or not.
        std::optional<std::string> text;
        if (m_openUVScript.has_value() && m_openUVScript->path == path) {
            text = m_openUVScript->text;
        } else {
            text = ReadProjectTextFileUVE(path);
        }
        if (!text.has_value()) {
            base.message = "There is no file at " + path + ".";
            problems.push_back(std::move(base));
            continue;
        }
        Core::UVScriptObjectHostUVE host(entityManager, nullptr, entity);
        for (const UVScript::DiagnosticUVE& diagnostic : UVScript::CompileUVScriptSourceUVE(*text, host).diagnostics) {
            EntityCompileProblemUVE problem = base;
            problem.at = diagnostic.at;
            problem.message = diagnostic.message;
            problems.push_back(std::move(problem));
        }
    }
    m_entityEditSession->problems = std::move(problems);
    m_entityEditSession->compiled = true;
    return m_entityEditSession->problems.size();
}

std::vector<EntityDiffRowUVE> EditorUVE::DiffEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return {};
    }
    const EntityDocumentScopeUVE scope(*this);
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    const auto payloadText = [](const std::vector<std::byte>& payload) {
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    };
    std::string saved;
    if (const auto file = Asset::ReadUveFileUVE(m_entityEditSession->assetPath); file.has_value()) {
        saved = payloadText(file->second);
    }
    std::string current;
    const Scene::EntityUVE root = GetEntityEditorRootUVE();
    if (root != Scene::kInvalidEntityUVE) {
        const std::optional<Scene::SceneSnapshotUVE> snapshot = m_services->GetSceneSerializerUVE().CaptureUVE(
            m_services->GetEntityManagerUVE(), {root}, Asset::AssetKindUVE::Prefab);
        if (snapshot.has_value()) {
            if (const auto decoded = Asset::DecodeUveFileEnvelopeUVE(snapshot->bytes); decoded.has_value()) {
                current = payloadText(decoded->second);
            }
        }
    }
    return DiffEntityPayloadsUVE(saved, current);
}

std::vector<EditorUVE::EntityFindRowUVE> EditorUVE::FindInEntityEditorUVE(const std::string_view query) {
    std::vector<EntityFindRowUVE> rows;
    if (!m_entityEditSession.has_value() || query.empty()) {
        return rows;
    }
    const EntityDocumentScopeUVE scope(*this);
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string needle = lower(std::string(query));
    const auto matches = [&](const std::string& text) { return lower(text).find(needle) != std::string::npos; };
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE entity : CollectEntityEditorObjectsUVE()) {
        const std::string object = GetEntityDisplayLabelUVE(entity);
        if (matches(object)) {
            rows.push_back(EntityFindRowUVE{entity, object, "Name", object});
        }
        const std::string type(GetObjectTypeNameUVE(entity));
        if (matches(type)) {
            rows.push_back(EntityFindRowUVE{entity, object, "Type", type});
        }
        // Its components as the file holds them: every property name and value can be found.
        const std::optional<Scene::SceneSnapshotUVE> snapshot =
            m_services->GetSceneSerializerUVE().CaptureUVE(entityManager, {entity}, Asset::AssetKindUVE::Prefab);
        if (!snapshot.has_value()) {
            continue;
        }
        const auto decoded = Asset::DecodeUveFileEnvelopeUVE(snapshot->bytes);
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
            if (!components.is_object() || !components.contains("HierarchyComponentUVE") ||
                components["HierarchyComponentUVE"].value("parentLocalId", static_cast<std::int64_t>(0)) >= 0) {
                continue;
            }
            for (auto c = components.begin(); c != components.end(); ++c) {
                std::string component = c.key();
                if (component == "HierarchyComponentUVE" || component == "NameComponentUVE") {
                    continue;
                }
                if (component.size() > 12U && component.ends_with("ComponentUVE")) {
                    component.resize(component.size() - 12U);
                }
                if (matches(component)) {
                    rows.push_back(EntityFindRowUVE{entity, object, component, component});
                }
                if (!c.value().is_object()) {
                    continue;
                }
                for (auto p = c.value().begin(); p != c.value().end(); ++p) {
                    const std::string value = p.value().is_string() ? p.value().get<std::string>() : p.value().dump();
                    if (matches(p.key()) || (p.value().is_string() && matches(value))) {
                        rows.push_back(EntityFindRowUVE{entity, object, component + "." + p.key(), value});
                    }
                }
            }
            break;
        }
    }
    return rows;
}

std::optional<std::string> EditorUVE::ReadEntityScriptSourceUVE(const std::string& path, const Scene::EntityUVE entity) const {
    if (path.ends_with(".uvbp")) {
        std::optional<Asset::BlueprintAssetUVE> blueprint =
            m_openBlueprintProviderUVE ? m_openBlueprintProviderUVE(path) : std::optional<Asset::BlueprintAssetUVE>{};
        if (!blueprint.has_value()) {
            if (const std::optional<std::string> bytes = ReadProjectTextFileUVE(path); bytes.has_value()) {
                const auto* const data = reinterpret_cast<const std::byte*>(bytes->data());
                blueprint = Asset::DecodeBlueprintAssetUVE(std::vector<std::byte>(data, data + bytes->size()));
            }
        }
        if (!blueprint.has_value()) {
            return std::nullopt;
        }
        const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
            Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
        return Asset::TranslateBlueprintToUVScriptUVE(*blueprint, std::filesystem::path(path).stem().string(),
                                                      descriptor != nullptr ? descriptor->displayName : "Object3D")
            .source;
    }
    if (m_openUVScript.has_value() && m_openUVScript->path == path) {
        return m_openUVScript->text;
    }
    return ReadProjectTextFileUVE(path);
}

const std::vector<EditorUVE::EntityCompileProblemUVE>& EditorUVE::GetEntityEditorProblemsUVE() const noexcept {
    static const std::vector<EntityCompileProblemUVE> kNoneUVE;
    return m_entityEditSession.has_value() ? m_entityEditSession->problems : kNoneUVE;
}

bool EditorUVE::HasEntityEditorCompiledUVE() const noexcept {
    return m_entityEditSession.has_value() && m_entityEditSession->compiled;
}

std::vector<EditorUVE::EntitySignalRowUVE> EditorUVE::GetEntityEditorSignalsUVE() {
    std::vector<EntitySignalRowUVE> rows;
    const EntityDocumentScopeUVE scope(*this);
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE entity : CollectEntityEditorObjectsUVE()) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (!IsUVScriptFileUVE(path) && !path.ends_with(".uvbp")) {
            continue;
        }
        const std::optional<std::string> text = ReadEntityScriptSourceUVE(path, entity);
        if (!text.has_value()) {
            continue;
        }
        const std::string objectName = GetEntityDisplayLabelUVE(entity);
        // Whatever parsed is listed, even while the file has mistakes elsewhere.
        for (const UVScript::HandlerUVE& handler : UVScript::ParseUVScriptUVE(*text).file.handlers) {
            EntitySignalRowUVE row;
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

bool EditorUVE::GoToEntityScriptUVE(const Scene::EntityUVE entity, const std::uint32_t line) {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    const EntityDocumentScopeUVE scope(*this);
    SelectEntityUVE(entity);
    if (!OpenUVScriptForEntityUVE(entity)) {
        return false;
    }
    m_activeWorkspace = EditorWorkspaceUVE::Library; // The script lives in this window, not the main one.
    m_entityEditSession->tab = EntityEditorTabUVE::Scripting;
    m_entityEditSession->forceTab = true;
    m_uvscriptJumpLine = line;
    return true;
}


// ------------------------------------------------------------------------------------ simulate


bool EditorUVE::PlayEntitySimulationUVE() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->paused = false;
        return true;
    }
    const EntityDocumentScopeUVE scope(*this);
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    const Scene::EntityUVE root = GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        m_contentStatusMessage = "An entity has exactly one root object: put the other top-level objects under it, then simulate.";
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::optional<Scene::SceneSnapshotUVE> snapshot =
        m_services->GetSceneSerializerUVE().CaptureUVE(entityManager, {root}, Asset::AssetKindUVE::Prefab);
    if (!snapshot.has_value()) {
        m_contentStatusMessage = "Could not take the entity's state to simulate it.";
        return false;
    }
    auto simulation = std::make_shared<EntitySimulationUVE>();
    simulation->snapshot = *snapshot;
    simulation->dirtyBefore = m_sceneDirty;
    std::size_t failed = 0U;
    for (const Scene::EntityUVE entity : CollectEntityEditorObjectsUVE()) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            continue;
        }
        const Scene::ScriptComponentUVE& component = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity);
        if (!IsUVScriptFileUVE(component.scriptAssetPath) && !component.scriptAssetPath.ends_with(".uvbp")) {
            continue;
        }
        std::optional<std::string> text = ReadEntityScriptSourceUVE(component.scriptAssetPath, entity);
        if (!text.has_value()) {
            ++failed;
            continue;
        }
        EntitySimulationUVE::ScriptUVE script;
        script.entity = entity;
        EntitySimulationUVE* const raw = simulation.get();
        script.host = std::make_unique<Core::UVScriptObjectHostUVE>(
            entityManager, &m_services->GetInputSystemUVE(), entity, nullptr, nullptr,
            [raw](const Scene::EntityUVE other) -> UVScript::ScriptInstanceUVE* {
                for (EntitySimulationUVE::ScriptUVE& candidate : raw->scripts) {
                    if (candidate.entity == other) {
                        return candidate.instance.get();
                    }
                }
                return nullptr;
            });
        const UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(*text, *script.host);
        if (!compiled.IsSuccessUVE()) {
            ++failed;
            continue;
        }
        script.instance = std::make_unique<UVScript::ScriptInstanceUVE>(compiled.program, *script.host);
        for (const UVScript::FieldInfoUVE& field : UVScript::GetProgramFieldsUVE(*compiled.program)) {
            const auto stored = component.exportValues.find(field.name);
            if (field.kind != UVScript::FieldKindUVE::Export || stored == component.exportValues.end()) {
                continue;
            }
            if (const std::optional<UVScript::ValueUVE> value = UVScript::ParseValueTextUVE(stored->second, field.type)) {
                static_cast<void>(script.instance->SetFieldUVE(field.name, *value));
            }
        }
        simulation->scripts.push_back(std::move(script));
    }
    m_entityEditSession->simulation = std::move(simulation);
    const std::size_t running = m_entityEditSession->simulation->scripts.size();
    m_contentStatusMessage = "Simulating: " + std::to_string(running) + (running == 1U ? " script" : " scripts") +
                             (failed > 0U ? ", " + std::to_string(failed) + " did not compile (Compile lists them)" : "") + ".";
    return true;
}

void EditorUVE::PauseEntitySimulationUVE() {
    if (m_entityEditSession.has_value() && m_entityEditSession->simulation != nullptr) {
        m_entityEditSession->simulation->paused = true;
    }
}

bool EditorUVE::StopEntitySimulationUVE() {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr) {
        return false;
    }
    const EntityDocumentScopeUVE scope(*this);
    const std::shared_ptr<EntitySimulationUVE> simulation = std::move(m_entityEditSession->simulation);
    m_entityEditSession->simulation.reset();
    // The scripts go first: they hold the objects about to be replaced.
    const Scene::SceneSnapshotUVE snapshot = simulation->snapshot;
    const bool dirtyBefore = simulation->dirtyBefore;
    simulation->scripts.clear();
    m_componentPropertyPreview.reset();
    ClearDocumentSceneUVE();
    const std::vector<Scene::EntityUVE> restored =
        m_services->GetSceneSerializerUVE().RestoreUVE(m_services->GetEntityManagerUVE(), snapshot);
    if (restored.empty()) {
        m_contentStatusMessage = "Could not put the entity back; Revert reads it from its file.";
        return false;
    }
    m_entityEditSession->rootEntity = restored.front();
    // The objects are new: what undo remembered named the old ones.
    ClearHistoryUVE();
    m_sceneDirty = dirtyBefore;
    InvalidateHierarchyFilterCacheUVE();
    SelectEntityUVE(restored.front());
    m_contentStatusMessage = "Simulation stopped: the entity is as it was.";
    return true;
}

EditorUVE::EntitySimulationStateUVE EditorUVE::GetEntitySimulationStateUVE() const noexcept {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr) {
        return EntitySimulationStateUVE::Stopped;
    }
    return m_entityEditSession->simulation->paused ? EntitySimulationStateUVE::Paused : EntitySimulationStateUVE::Running;
}

void EditorUVE::TickEntitySimulationUVE(const double deltaSeconds) {
    if (!m_entityEditSession.has_value() || m_entityEditSession->simulation == nullptr ||
        m_entityEditSession->simulation->paused || !(deltaSeconds >= 0.0)) {
        return;
    }
    const EntityDocumentScopeUVE scope(*this);
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const double dt = std::min(deltaSeconds, 0.1);
    for (EntitySimulationUVE::ScriptUVE& script : m_entityEditSession->simulation->scripts) {
        if (script.instance == nullptr || !entityManager.IsAliveUVE(script.entity)) {
            continue;
        }
        if (!script.ready) {
            script.ready = true;
            static_cast<void>(script.instance->RaiseEventUVE("ready"));
        }
        const UVScript::ValueUVE args[] = {dt};
        static_cast<void>(script.instance->RaiseEventUVE("tick", args));
        script.instance->AdvanceUVE(dt);
    }
}

} // namespace UVE::Editor
