// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor: one entity asset (.uventity / .uvprefab) opened on its own, in its own OS
// window. While it is open the entity is the document - the scene waits in a snapshot, exactly
// the way Play keeps it - so the Scene tree, the Inspector, gizmos and undo all work on the
// entity with no second set of tools, and the main window has nothing to draw.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/component/animation_player_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/core/uvscript_node_host_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_parser_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool IsUVScriptFileUVE(const std::string_view path) noexcept { return path.ends_with(".uvs"); }

constexpr ImVec4 kProblemColourUVE{0.95F, 0.62F, 0.35F, 1.0F};
constexpr ImVec4 kCleanColourUVE{0.45F, 0.80F, 0.55F, 1.0F};

} // namespace

Scene::EntityUVE EditorUVE::LoadEntityIntoDocumentUVE(const Asset::AssetGuidUVE guid) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE sceneRoot = EnsureDocumentSceneRootUVE();
    const Scene::EntityUVE root = m_services->GetPrefabSystemUVE().InstantiateUVE(
        entityManager, m_services->GetSceneGraphUVE(), m_services->GetAssetDatabaseUVE(), guid, sceneRoot);
    if (root == Scene::kInvalidEntityUVE) {
        return root;
    }
    // Edited as itself, not as an instance of itself: saving writes a plain tree.
    if (entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(root)) {
        entityManager.RemoveComponentUVE<Scene::PrefabInstanceComponentUVE>(root);
    }
    InvalidateHierarchyFilterCacheUVE();
    SelectEntityUVE(root);
    return root;
}

bool EditorUVE::OpenEntityEditorUVE(const std::filesystem::path& assetPath) {
    if (m_entityEditSession.has_value()) {
        return m_entityEditSession->assetPath == assetPath;
    }
    std::error_code error;
    if (m_state != EditorStateUVE::Running || m_playModeState != EditorPlayModeStateUVE::Edit ||
        !IsAuthoringCommandAllowedUVE() || !std::filesystem::is_regular_file(assetPath, error)) {
        return false;
    }
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE()) {
        m_contentStatusMessage = "Save or close " + std::filesystem::path{m_openUVScript->path}.filename().string() +
                                 " first: it has unsaved text.";
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());

    EntityEditSessionUVE session;
    session.assetPath = assetPath;
    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    session.sceneWasEmpty = roots.empty();
    if (!session.sceneWasEmpty) {
        const std::optional<Scene::SceneSnapshotUVE> snapshot = m_services->GetSceneSerializerUVE().CaptureUVE(
            m_services->GetEntityManagerUVE(), roots, Asset::AssetKindUVE::Scene);
        if (!snapshot.has_value()) {
            return false;
        }
        session.sceneSnapshot = *snapshot;
    }
    session.sceneDirtyBefore = m_sceneDirty;
    session.selectionBefore = CaptureSelectionPathsUVE(roots);
    session.guid = m_services->GetAssetDatabaseUVE().RegisterUVE(assetPath);
    if (session.guid == Asset::kInvalidAssetGuidUVE) {
        return false;
    }

    ClearDocumentSceneUVE();
    if (LoadEntityIntoDocumentUVE(session.guid) == Scene::kInvalidEntityUVE) {
        // Could not read the entity: put the scene back as it was.
        ClearDocumentSceneUVE();
        if (!session.sceneWasEmpty) {
            const std::vector<Scene::EntityUVE> restored =
                m_services->GetSceneSerializerUVE().RestoreUVE(m_services->GetEntityManagerUVE(), session.sceneSnapshot);
            RestoreSelectionUVE(ResolveSelectionPathsUVE(session.selectionBefore, restored));
        }
        m_sceneDirty = session.sceneDirtyBefore;
        InvalidateHierarchyFilterCacheUVE();
        return false;
    }
    ClearHistoryUVE();
    m_sceneDirty = false;
    // The script editor worked on scene nodes; the entity brings its own.
    CloseOpenUVScriptUVE();
    // Nothing runs on its own while an entity is edited: it is saved as authored.
    if (m_simulationControl != nullptr) {
        session.simulationBefore = m_simulationControl->GetSimulationExecutionModeUVE();
        static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Paused));
    }
    m_entityEditSession = std::move(session);
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
    const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
    if (sceneRoot == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    const std::vector<Scene::EntityUVE> children =
        m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), sceneRoot);
    return children.size() == 1U ? children.front() : Scene::kInvalidEntityUVE;
}

bool EditorUVE::SaveEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    const Scene::EntityUVE root = GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        m_contentStatusMessage = "An entity has exactly one root node: put the other top-level nodes under it, then save.";
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
    return true;
}

bool EditorUVE::RevertEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    m_componentPropertyPreview.reset();
    ClearDocumentSceneUVE();
    if (LoadEntityIntoDocumentUVE(m_entityEditSession->guid) == Scene::kInvalidEntityUVE) {
        m_contentStatusMessage = "Could not read " + m_entityEditSession->assetPath.filename().string() + " again.";
        return false;
    }
    ClearHistoryUVE();
    m_sceneDirty = false;
    // Node handles are new; the script text goes back to what is on disk.
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
        return false;
    }
    CloseOpenUVScriptUVE();
    m_componentPropertyPreview.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Kept until the scene is back, so a failed restore can return to the entity instead of
    // leaving nothing on screen.
    const std::vector<Scene::EntityUVE> entityRoots = GetDocumentRootsUVE();
    const std::optional<Scene::SceneSnapshotUVE> entitySnapshot =
        entityRoots.empty() ? std::nullopt
                            : m_services->GetSceneSerializerUVE().CaptureUVE(entityManager, entityRoots,
                                                                              Asset::AssetKindUVE::Scene);
    const EntityEditSessionUVE session = std::move(*m_entityEditSession);
    m_entityEditSession.reset();

    ClearDocumentSceneUVE();
    std::vector<Scene::EntityUVE> restored;
    if (!session.sceneWasEmpty) {
        restored = m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, session.sceneSnapshot);
        if (restored.empty()) {
            if (entitySnapshot.has_value()) {
                static_cast<void>(m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, *entitySnapshot));
            }
            m_entityEditSession = session;
            m_contentStatusMessage = "Could not bring the scene back; the entity stays open.";
            InvalidateHierarchyFilterCacheUVE();
            return false;
        }
    }
    RestoreSelectionUVE(ResolveSelectionPathsUVE(session.selectionBefore, restored));
    m_sceneDirty = session.sceneDirtyBefore;
    ClearHistoryUVE();
    if (m_simulationControl != nullptr && session.simulationBefore.has_value()) {
        static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(*session.simulationBefore));
    }
    InvalidateHierarchyFilterCacheUVE();

    // The file changed: instances of it in the scene that have no local changes follow it.
    if (session.savedOnce) {
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
                                     session.assetPath.filename().string() + " in the scene.";
        }
    }
    return true;
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
    return m_entityEditSession.has_value() && (m_sceneDirty || (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE()));
}

std::vector<Scene::EntityUVE> EditorUVE::CollectEntityEditorNodesUVE() {
    std::vector<Scene::EntityUVE> nodes;
    if (!m_entityEditSession.has_value()) {
        return nodes;
    }
    const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
    if (sceneRoot == Scene::kInvalidEntityUVE) {
        return nodes;
    }
    // Depth first, children in their authored order: the order the Scene tree shows.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> pending = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, sceneRoot);
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::EntityUVE entity = pending.back();
        pending.pop_back();
        nodes.push_back(entity);
        std::vector<Scene::EntityUVE> children = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    return nodes;
}

std::size_t EditorUVE::CompileEntityEditorUVE() {
    if (!m_entityEditSession.has_value()) {
        return 0U;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    std::vector<EntityCompileProblemUVE> problems;
    const std::vector<Scene::EntityUVE> nodes = CollectEntityEditorNodesUVE();
    if (GetEntityEditorRootUVE() == Scene::kInvalidEntityUVE) {
        EntityCompileProblemUVE problem;
        problem.message = nodes.empty() ? "The entity has no nodes." :
                                          "An entity has exactly one root node: put the other top-level nodes under it.";
        problems.push_back(std::move(problem));
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE entity : nodes) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (path.empty()) {
            continue;
        }
        EntityCompileProblemUVE base;
        base.entity = entity;
        base.nodeName = GetEntityDisplayLabelUVE(entity);
        base.scriptPath = path;
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
        Core::UVScriptNodeHostUVE host(entityManager, nullptr, entity);
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

const std::vector<EditorUVE::EntityCompileProblemUVE>& EditorUVE::GetEntityEditorProblemsUVE() const noexcept {
    static const std::vector<EntityCompileProblemUVE> kNoneUVE;
    return m_entityEditSession.has_value() ? m_entityEditSession->problems : kNoneUVE;
}

bool EditorUVE::HasEntityEditorCompiledUVE() const noexcept {
    return m_entityEditSession.has_value() && m_entityEditSession->compiled;
}

std::vector<EditorUVE::EntitySignalRowUVE> EditorUVE::GetEntityEditorSignalsUVE() {
    std::vector<EntitySignalRowUVE> rows;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE entity : CollectEntityEditorNodesUVE()) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            continue;
        }
        const std::string path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (!IsUVScriptFileUVE(path)) {
            continue;
        }
        const std::optional<std::string> text = m_openUVScript.has_value() && m_openUVScript->path == path
                                                    ? std::optional<std::string>{m_openUVScript->text}
                                                    : ReadProjectTextFileUVE(path);
        if (!text.has_value()) {
            continue;
        }
        const std::string nodeName = GetEntityDisplayLabelUVE(entity);
        // Whatever parsed is listed, even while the file has mistakes elsewhere.
        for (const UVScript::HandlerUVE& handler : UVScript::ParseUVScriptUVE(*text).file.handlers) {
            EntitySignalRowUVE row;
            row.entity = entity;
            row.nodeName = nodeName;
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

void EditorUVE::DrawEntityCompileBadgeUVE(const EntityEditSessionUVE& session) {
    if (!session.compiled) {
        ImGui::TextDisabled("Not compiled");
    } else if (session.problems.empty()) {
        ImGui::TextColored(kCleanColourUVE, "Compiled");
    } else {
        ImGui::TextColored(kProblemColourUVE, "%zu problem%s", session.problems.size(),
                           session.problems.size() == 1U ? "" : "s");
    }
}

void EditorUVE::DrawEntityEditorMiddleUVE(EntityEditSessionUVE& session) {
    // Compile's list sits under the tabs while it has something to say.
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const bool showProblems = session.compiled && !session.problems.empty();
    const float problemsHeight =
        showProblems ? std::min(ImGui::GetContentRegionAvail().y * 0.3F,
                                lineHeight * static_cast<float>(session.problems.size() + 2U) + 8.0F)
                     : 0.0F;
    const float tabsHeight = ImGui::GetContentRegionAvail().y - problemsHeight - (showProblems ? 4.0F : 0.0F);

    ImGui::BeginChild("##entity-tabs", ImVec2{0.0F, tabsHeight}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::BeginTabBar("##entity-middle-tabs")) {
        const auto tab = [&session](const char* label, const EntityEditorTabUVE which) {
            // The tab the session names is forced once; after that clicks decide.
            const ImGuiTabItemFlags flags = session.tab == which && session.forceTab ? ImGuiTabItemFlags_SetSelected : 0;
            const bool open = ImGui::BeginTabItem(label, nullptr, flags);
            if (open && !session.forceTab) {
                session.tab = which;
            }
            return open;
        };
        if (tab("Viewport", EntityEditorTabUVE::Viewport)) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::BeginChild("##entity-view", ImVec2{0.0F, 0.0F}, true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawViewportImageUVE();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
        if (tab("Scripting", EntityEditorTabUVE::Scripting)) {
            DrawEntityEditorScriptingTabUVE();
            ImGui::EndTabItem();
        }
        if (tab("Signals", EntityEditorTabUVE::Signals)) {
            DrawEntityEditorSignalsTabUVE();
            ImGui::EndTabItem();
        }
        session.forceTab = false;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    if (!showProblems) {
        return;
    }
    ImGui::BeginChild("##entity-problems", ImVec2{0.0F, 0.0F}, true);
    ImGui::TextColored(kProblemColourUVE, "Compile found %zu problem%s", session.problems.size(),
                       session.problems.size() == 1U ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("- click one to go there");
    if (ImGui::BeginTable("##entity-problem-list", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Where");
        ImGui::TableSetupColumn("Problem", ImGuiTableColumnFlags_WidthStretch);
        std::optional<std::size_t> clicked;
        for (std::size_t index = 0U; index < session.problems.size(); ++index) {
            const EntityCompileProblemUVE& problem = session.problems[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string node = problem.nodeName.empty() ? std::string{"(entity)"} : problem.nodeName;
            if (ImGui::Selectable(node.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                clicked = index;
            }
            ImGui::TableSetColumnIndex(1);
            if (problem.scriptPath.empty()) {
                ImGui::TextDisabled("-");
            } else if (problem.at.line == 0U) {
                ImGui::TextDisabled("%s", std::filesystem::path{problem.scriptPath}.filename().string().c_str());
            } else {
                ImGui::TextDisabled("%s:%u", std::filesystem::path{problem.scriptPath}.filename().string().c_str(),
                                    problem.at.line);
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", problem.message.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (clicked.has_value()) {
            const EntityCompileProblemUVE problem = session.problems[*clicked];
            if (problem.entity != Scene::kInvalidEntityUVE && !GoToEntityScriptUVE(problem.entity, problem.at.line)) {
                SelectEntityUVE(problem.entity);
            }
        }
    }
    ImGui::EndChild();
}

void EditorUVE::DrawEntityEditorDockUVE(EntityEditSessionUVE& session) {
    if (!ImGui::BeginTabBar("##entity-dock-tabs")) {
        return;
    }
    const auto tab = [&session](const char* label, const EntityEditorDockTabUVE which) {
        const bool open = ImGui::BeginTabItem(label);
        if (open) {
            session.dockTab = which;
        }
        return open;
    };
    if (tab("Content", EntityEditorDockTabUVE::Content)) {
        // The project's Content Browser, the same one as the main window's: drag an entity or
        // model from here onto the tree to add it under the entity.
        ImGui::BeginChild("##entity-dock-content", ImVec2{0.0F, 0.0F}, false);
        DrawContentBrowserBodyUVE();
        ImGui::EndChild();
        // The item right-click menu; the main window's dock draws it only while that dock is shown.
        DrawFilesystemContextPopupUVE();
        ImGui::EndTabItem();
    }
    const auto upcoming = [](const char* what, const char* detail) {
        const ImVec2 area = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, area.y * 0.35F));
        ImGui::SetCursorPosX((area.x - ImGui::CalcTextSize(what).x) * 0.5F);
        ImGui::TextUnformatted(what);
        ImGui::SetCursorPosX((area.x - ImGui::CalcTextSize(detail).x) * 0.5F);
        ImGui::TextDisabled("%s", detail);
    };
    // The animation tabs only exist once the entity has a node that uses them.
    bool hasPlayer = false;
    bool hasTree = false;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE node : CollectEntityEditorNodesUVE()) {
        hasPlayer = hasPlayer || entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(node);
        hasTree = hasTree || entityManager.HasComponentUVE<Scene::AnimationTreeComponentUVE>(node);
    }
    bool timelineShown = false;
    if (hasPlayer && tab("Timeline", EntityEditorDockTabUVE::Timeline)) {
        timelineShown = true;
        DrawAnimationTimelineUVE();
        ImGui::EndTabItem();
    }
    if (!timelineShown) {
        StopAnimationTimelinePreviewUVE();
    }
    if (hasTree && tab("Anim Graph", EntityEditorDockTabUVE::AnimGraph)) {
        upcoming("The Anim Graph is not built yet.",
                 "The AnimationTree as boxes and wires, with its state machine, comes with the Timeline.");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void EditorUVE::DrawEntityEditorScriptingTabUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Scene::EntityUVE entity = m_selectedEntity;
    const bool alive = entity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(entity);
    const std::string path = alive && entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)
                                 ? entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath
                                 : std::string{};
    // Follow the selection: the tab always shows the selected node's script.
    if (alive && IsUVScriptFileUVE(path) && (!m_openUVScript.has_value() || m_openUVScript->entity != entity)) {
        if (!m_openUVScript.has_value() || !m_openUVScript->IsDirtyUVE() || m_openUVScript->path == path) {
            static_cast<void>(OpenUVScriptForEntityUVE(entity));
            m_activeWorkspace = EditorWorkspaceUVE::Library;
        }
    }
    if (m_openUVScript.has_value() && alive && m_openUVScript->entity == entity) {
        static_cast<void>(DrawUVScriptEditorBodyUVE(false));
        return;
    }

    // Nothing to show: say why, and offer the one useful action.
    const ImVec2 area = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + area.y * 0.38F);
    const auto centred = [&area](const std::string& text, const bool disabled) {
        ImGui::SetCursorPosX((area.x - ImGui::CalcTextSize(text.c_str()).x) * 0.5F);
        if (disabled) {
            ImGui::TextDisabled("%s", text.c_str());
        } else {
            ImGui::TextUnformatted(text.c_str());
        }
    };
    if (!alive) {
        centred("Select a node to see its script.", true);
        return;
    }
    const std::string name = GetEntityDisplayLabelUVE(entity);
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE() && IsUVScriptFileUVE(path)) {
        centred(std::filesystem::path{m_openUVScript->path}.filename().string() + " has unsaved text.", false);
        centred("Save it (Ctrl+S) before switching to " + name + "'s script.", true);
        return;
    }
    if (!path.empty() && !IsUVScriptFileUVE(path)) {
        centred(name + " points at " + path + ", which is not a UVScript file.", false);
        return;
    }
    centred(name + " has no script yet.", false);
    centred("What it does on ready, every tick, and when things happen to it.", true);
    ImGui::Spacing();
    const bool canCreate = entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity) && path.empty();
    ImGui::SetCursorPosX((area.x - 160.0F) * 0.5F);
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("New UVScript", ImVec2{160.0F, 0.0F})) {
        if (CreateUVScriptForSelectedEntityUVE()) {
            m_activeWorkspace = EditorWorkspaceUVE::Library;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(canCreate ? "Write scripts/%s.uvs for this node and open it here" :
                                      "This node has no Script slot",
                          name.c_str());
    }
}

void EditorUVE::DrawEntityEditorSignalsTabUVE() {
    const std::vector<EntitySignalRowUVE> rows = GetEntityEditorSignalsUVE();
    if (rows.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No node answers to anything yet.");
        ImGui::TextDisabled("Add an `on <event>:` block to a node's script (Scripting tab) and it is listed here.");
        return;
    }
    ImGui::TextDisabled("What each node answers to. Double-click a row to open the handler.");
    if (!ImGui::BeginTable("##entity-signals", 4,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Node");
    ImGui::TableSetupColumn("Event");
    ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Script");
    ImGui::TableHeadersRow();
    std::optional<std::size_t> opened;
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const EntitySignalRowUVE& row = rows[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // The node name only on its first row, so each node reads as one group.
        const bool firstOfNode = index == 0U || rows[index - 1U].entity != row.entity;
        if (ImGui::Selectable(firstOfNode ? row.nodeName.c_str() : "", m_selectedEntity == row.entity,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            opened = index;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4{0.55F, 0.78F, 0.98F, 1.0F}, "on %s", row.event.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("%s", row.params.empty() ? "-" : row.params.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%s:%u", std::filesystem::path{row.scriptPath}.filename().string().c_str(), row.line);
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (opened.has_value()) {
        static_cast<void>(GoToEntityScriptUVE(rows[*opened].entity, rows[*opened].line));
    }
}

void EditorUVE::DrawEntityEditorPlaceholderUVE() {
    // The main window stays out of the way while the entity is open: nothing to render or edit
    // here, only where the work is and how to come back.
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + ImGui::GetFrameHeight() * 2.0F});
    ImGui::SetNextWindowSize(ImVec2{mainViewport->WorkSize.x, mainViewport->WorkSize.y - ImGui::GetFrameHeight() * 2.0F});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##entity-editor-placeholder", nullptr, flags);
    const std::string name = m_entityEditSession->assetPath.filename().string();
    const std::string line1 = "Editing " + name + " in the Entity Editor window.";
    const char* const line2 = "The scene waits here, untouched. Close the Entity Editor to come back to it.";
    const ImVec2 area = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2{(area.x - ImGui::CalcTextSize(line1.c_str()).x) * 0.5F, area.y * 0.42F});
    ImGui::TextUnformatted(line1.c_str());
    ImGui::SetCursorPosX((area.x - ImGui::CalcTextSize(line2).x) * 0.5F);
    ImGui::TextDisabled("%s", line2);
    ImGui::SetCursorPosX((area.x - 180.0F) * 0.5F);
    if (ImGui::Button("Show the Entity Editor", ImVec2{180.0F, 0.0F})) {
        ImGui::SetWindowFocus("###entity-editor");
    }
    ImGui::End();
}

void EditorUVE::DrawEntityEditorWindowUVE() {
    EntityEditSessionUVE& session = *m_entityEditSession;
    // Its own OS window: never merged back into the main one, never docked.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{mainViewport->Pos.x + 60.0F, mainViewport->Pos.y + 60.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{1180.0F, 720.0F}, ImGuiCond_FirstUseEver);
    const bool unsaved = HasEntityEditorUnsavedChangesUVE();
    const std::string title = "Entity Editor - " + session.assetPath.filename().string() + (unsaved ? " *" : "") +
                              "###entity-editor";
    bool open = true;
    ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // Toolbar.
    if (ImGui::Button("Compile")) {
        static_cast<void>(CompileEntityEditorUVE());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Check the whole entity: one root, and every node's script against its node (F7)");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) {
        static_cast<void>(CompileEntityEditorUVE());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
         ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))) {
        static_cast<void>(SaveEntityEditorUVE());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Write the entity and its open script back to their files (Ctrl+S)");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!unsaved);
    if (ImGui::Button("Revert")) {
        static_cast<void>(RevertEntityEditorUVE());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Throw away the changes since the last save");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    DrawEntityCompileBadgeUVE(session);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", unsaved ? "Unsaved changes" : session.savedOnce ? "Saved" : "No changes");
    if (!m_contentStatusMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  %s", m_contentStatusMessage.c_str());
    }
    ImGui::Separator();

    // Scene tree | Viewport, Scripting, Signals | Inspector, with Compile's problems under the
    // middle once it has found any.
    // The dock (Content, Timeline, Anim Graph) takes the bottom, resized by the bar above it.
    constexpr float kSplitterUVE = 6.0F;
    const float available = ImGui::GetContentRegionAvail().y;
    session.dockHeight = std::clamp(session.dockHeight, 90.0F, std::max(90.0F, available - 160.0F));
    const float height = std::max(80.0F, available - session.dockHeight - kSplitterUVE);
    const float width = ImGui::GetContentRegionAvail().x;
    const float treeWidth = std::clamp(width * 0.22F, 200.0F, 320.0F);
    const float inspectorWidth = std::clamp(width * 0.28F, 260.0F, 420.0F);
    ImGui::BeginChild("##entity-tree", ImVec2{treeWidth, height}, true);
    DrawHierarchyBodyUVE();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##entity-middle", ImVec2{std::max(120.0F, width - treeWidth - inspectorWidth - 16.0F), height},
                      false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawEntityEditorMiddleUVE(session);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##entity-inspector", ImVec2{0.0F, height}, true);
    DrawInspectorContentUVE();
    ImGui::EndChild();

    ImGui::InvisibleButton("##entity-dock-splitter", ImVec2{-FLT_MIN, kSplitterUVE});
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        session.dockHeight -= ImGui::GetIO().MouseDelta.y;
    }
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = (min.y + max.y) * 0.5F;
        ImGui::GetWindowDrawList()->AddLine(ImVec2{min.x, y}, ImVec2{max.x, y},
                                            ImGui::GetColorU32(ImGui::IsItemActive() || ImGui::IsItemHovered()
                                                                   ? ImGuiCol_SeparatorHovered
                                                                   : ImGuiCol_Separator));
    }
    ImGui::BeginChild("##entity-dock", ImVec2{0.0F, 0.0F}, true);
    DrawEntityEditorDockUVE(session);
    ImGui::EndChild();

    // The X with unsaved changes asks first; without them it just closes.
    if (!open) {
        if (unsaved) {
            session.confirmClose = true;
        } else {
            static_cast<void>(CloseEntityEditorUVE(false));
            ImGui::End();
            return;
        }
    }
    if (session.confirmClose) {
        ImGui::OpenPopup("Close Entity##entity-close");
        session.confirmClose = false;
    }
    bool closeNow = false;
    bool saveFirst = false;
    if (ImGui::BeginPopupModal("Close Entity##entity-close", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the changes to %s?", session.assetPath.filename().string().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2{100.0F, 0.0F})) {
            closeNow = true;
            saveFirst = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2{100.0F, 0.0F})) {
            closeNow = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2{100.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
    if (closeNow) {
        static_cast<void>(CloseEntityEditorUVE(saveFirst));
    }
}

} // namespace UVE::Editor
