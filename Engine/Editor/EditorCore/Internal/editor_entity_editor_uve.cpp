// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor: one entity asset (.uventity / .uvprefab) opened on its own, in its own OS
// window. While it is open the entity is the document - the scene waits in a snapshot, exactly
// the way Play keeps it - so the Scene tree, the Inspector, gizmos and undo all work on the
// entity with no second set of tools, and the main window has nothing to draw.

#include "uve/editor/editor_uve.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/component/prefab_instance_component_uve.h"

namespace UVE::Editor {

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
    return true;
}

bool EditorUVE::CloseEntityEditorUVE(const bool save) {
    if (!m_entityEditSession.has_value()) {
        return false;
    }
    if (save && m_sceneDirty && !SaveEntityEditorUVE()) {
        return false;
    }
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
    const std::string title = "Entity Editor - " + session.assetPath.filename().string() + (m_sceneDirty ? " *" : "") +
                              "###entity-editor";
    bool open = true;
    ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // Toolbar.
    if (ImGui::Button("Save")) {
        static_cast<void>(SaveEntityEditorUVE());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Write the entity back to its file (Ctrl+S)");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_sceneDirty);
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
    ImGui::TextDisabled("%s", m_sceneDirty ? "Unsaved changes" : session.savedOnce ? "Saved" : "No changes");
    if (!m_contentStatusMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  %s", m_contentStatusMessage.c_str());
    }
    ImGui::Separator();

    // Scene tree | view | Inspector.
    const float height = std::max(80.0F, ImGui::GetContentRegionAvail().y);
    const float width = ImGui::GetContentRegionAvail().x;
    const float treeWidth = std::clamp(width * 0.22F, 200.0F, 320.0F);
    const float inspectorWidth = std::clamp(width * 0.28F, 260.0F, 420.0F);
    ImGui::BeginChild("##entity-tree", ImVec2{treeWidth, height}, true);
    DrawHierarchyBodyUVE();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::BeginChild("##entity-view", ImVec2{std::max(120.0F, width - treeWidth - inspectorWidth - 16.0F), height}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawViewportImageUVE();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::BeginChild("##entity-inspector", ImVec2{0.0F, height}, true);
    DrawInspectorContentUVE();
    ImGui::EndChild();

    // The X with unsaved changes asks first; without them it just closes.
    if (!open) {
        if (m_sceneDirty) {
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
