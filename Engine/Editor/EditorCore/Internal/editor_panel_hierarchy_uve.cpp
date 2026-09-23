// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scene Hierarchy panel: the outliner tree, its filter, rename-in-place, and the
// drag-and-drop reparenting that goes with it.
//
// Split out of editor_uve.cpp, which had grown past seven thousand lines with twenty-eight draw
// methods in it - a file where fixing one panel means scrolling past every other. The methods are
// still EditorUVE members and are declared in the same header they always were: they read and
// write nine pieces of editor state (selection, the rename buffer, the filter, panel visibility),
// and turning those into parameters purely to make the functions free would have been a redesign
// of the editor's state ownership dressed up as a file move. A translation unit boundary gets the
// navigability without touching the design.
//
// Each row also has a right-click menu - add a child, rename, duplicate, delete - running the same
// commands as the keyboard shortcuts, so both paths share one set of rules (the scene root, for
// one, can be renamed but never duplicated, deleted or dragged).

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_node_icons_uve.h"

#include "uve/component/name_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Editor {

namespace {

/// The node library as menu items, grouped by category - shared by the panel's Add Node button and
/// each row's "Add Child Node" submenu so the two lists can never drift apart.
template <typename CreateFn>
void DrawNodeLibraryMenuItemsUVE(CreateFn&& create) {
    std::string_view lastCategory;
    for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : Scene::Nodes::GetSceneNodeDescriptorsUVE()) {
        if (descriptor.category != lastCategory) {
            if (!lastCategory.empty()) {
                ImGui::Separator();
            }
            ImGui::TextDisabled("%s", descriptor.category.data());
            lastCategory = descriptor.category;
        }
        ImGui::BeginDisabled(!descriptor.libraryCreatable);
        if (ImGui::MenuItem(descriptor.displayName.data())) {
            create(descriptor.kind);
        }
        ImGui::EndDisabled();
    }
}

} // namespace

void EditorUVE::DrawHierarchyPanelUVE() {
    if (!m_scenePanelVisible) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible);
    // Always, not FirstUseEver: this is one of the 5 core structural panels that must tile the
    // screen with zero gaps/overlaps on every single launch, regardless of any stale imgui.ini
    // from a previous version of this layout - an earlier version of this code used FirstUseEver
    // reasoning about a real docking system that was never actually built (this editor's panels
    // are independently-positioned floating windows arranged to look tiled, not a real
    // DockSpace/DockBuilder tree), so a stale ini entry from any prior layout formula change
    // permanently froze this panel at an outdated position/size - exactly the "sira at di align"
    // seams reported against a live build. Only secondary/optional windows (Plugin Tools, the
    // Scripting canvas) keep FirstUseEver, since those are genuinely meant to be
    // user-repositionable extras rather than part of the fixed chrome.
    ImGui::SetNextWindowPos(layout.scenePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.sceneSize, ImGuiCond_Always);
    ImGui::Begin(kPanelLabelSceneUVE);
    std::array<char, 256> filterBuffer{};
    m_hierarchyFilter.copy(filterBuffer.data(), filterBuffer.size() - 1U);
    const float addNodeButtonWidth = ImGui::GetFrameHeight();
    const bool canCreateNode = IsAuthoringCommandAllowedUVE();
    ImGui::PushID("scene-add-node");
    ImGui::BeginDisabled(!canCreateNode);
    if (ImGui::Button("+", ImVec2{addNodeButtonWidth, addNodeButtonWidth})) {
        ImGui::OpenPopup("scene-add-node-popup");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Add Node");
    }
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
    // No "Script" shortcut button here anymore - it duplicated the already-existing Scripting
    // workspace tab (Scene / Scripting / Game) and only added clutter/clipping risk to this row.
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##hierarchy-filter", "Search Nodes", filterBuffer.data(), filterBuffer.size())) {
        m_hierarchyFilter = filterBuffer.data();
        InvalidateHierarchyFilterCacheUVE();
    }
    if (ImGui::BeginPopup("scene-add-node-popup")) {
        ImGui::TextDisabled("Add Node");
        ImGui::Separator();
        DrawNodeLibraryMenuItemsUVE(
            [this](const Scene::Nodes::SceneNodeKindUVE kind) { static_cast<void>(CreateDocumentSceneNodeUVE(kind)); });
        ImGui::EndPopup();
    }
    ImGui::PopID();
    RebuildHierarchyFilterCacheUVE();
    const float hierarchyItemsHeight = std::max(36.0F, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("##scene-hierarchy-items", ImVec2{0.0F, hierarchyItemsHeight}, true,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        for (const Scene::EntityUVE root : GetDocumentRootsUVE()) {
            DrawHierarchyNodeUVE(root);
        }
        if (!GetDocumentRootsUVE().empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Drop entity here to make it a root");
            AcceptHierarchyDropTargetUVE(Scene::kInvalidEntityUVE);
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
    }
    ImGui::End();
}

void EditorUVE::DrawHierarchyNodeUVE(const Scene::EntityUVE entity) {
    if (!IsHierarchyEntityVisibleUVE(entity)) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> children =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity);
    // OpenOnDoubleClick deliberately omitted: a double-click on this row now starts renaming (see
    // below, matching Godot's own Scene dock convention) rather than toggling expand/collapse -
    // OpenOnArrow alone still lets the arrow itself expand/collapse on click.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const bool selected = IsEntitySelectedUVE(entity);
    const bool active = entity == m_selectedEntity;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (IsHierarchyFilterActiveUVE()) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    const bool renaming = entity == m_hierarchyRenameEntity;
    // Just enough leading space for the icon DrawHierarchyNodeIconUVE() draws into (see below) plus
    // a small gap - was 4 spaces, which (combined with TreeNodeEx's own arrow-toggle spacing that
    // every row reserves, leaf or not) pushed the icon+name noticeably right of the panel's left
    // edge instead of hugging it.
    // The gap is measured, not guessed: as many spaces as it takes to clear the icon plus a gap,
    // in the current font. A fixed two spaces was narrower than the icon, which then sat on top of
    // the name's first letter.
    const float spaceWidth = std::max(1.0F, ImGui::CalcTextSize(" ").x);
    const auto gapSpaces = static_cast<std::size_t>(
        std::ceil(((kHierarchyNodeIconRadiusUVE * 2.0F) + 6.0F) / spaceWidth));
    const std::string visibleLabel = renaming ? "" : std::string(gapSpaces, ' ') + GetEntityDisplayLabelUVE(entity);
    const std::string nodeLabel = visibleLabel + "##entity-" + std::to_string(entity.index) + ":" +
                                  std::to_string(entity.generation);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(66, 84, 101, 235));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(101, 130, 154, 245));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(88, 112, 133, 240));
    }
    const bool open = ImGui::TreeNodeEx(nodeLabel.c_str(), flags);
    if (active) {
        ImGui::PopStyleColor(3);
    }
    if (!renaming) {
        // Draws into the gap the row's own 4-space label prefix already reserves before the name,
        // so the icon lines up with the name the same way every other icon+name pair in this file
        // does, without needing a second ImGui column or child window just for one glyph.
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float iconCenterY = (itemMin.y + itemMax.y) * 0.5F;
        const float iconCenterX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing() + kHierarchyNodeIconRadiusUVE;
        const HierarchyNodeIconKindUVE iconKind = ClassifyHierarchyNodeIconUVE(entityManager, entity);
        DrawHierarchyNodeIconUVE(*ImGui::GetWindowDrawList(), ImVec2{iconCenterX, iconCenterY},
                                kHierarchyNodeIconRadiusUVE, iconKind,
                                m_uiAssets.GetGeneralIconTextureIdUVE("sun"),
                                m_uiAssets.GetGeneralIconTextureIdUVE("environment"));
    }
    if (ImGui::IsItemClicked() && !renaming) {
        if (ImGui::GetIO().KeyCtrl) {
            ToggleEntitySelectionUVE(entity);
        } else {
            SelectEntityUVE(entity);
        }
    }
    if (!renaming) {
        DrawHierarchyNodeContextMenuUVE(entity);
    }
    // Rename triggers the same way Godot's own Scene dock does: F2, or a double-click on an
    // already-selected row - no separate "Rename" button cluttering the row (the button used to
    // sit here, pushing further controls toward the panel's edge).
    const bool canRenameSelected =
        !renaming && HasSingleDocumentSelectionUVE() && entity == m_selectedEntity && IsAuthoringCommandAllowedUVE();
    if (canRenameSelected && (ImGui::IsKeyPressed(ImGuiKey_F2) ||
                              (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))) {
        m_hierarchyRenameEntity = entity;
        m_hierarchyRenameBuffer = GetEntityDisplayLabelUVE(entity);
        m_hierarchyRenameFocusRequested = true;
    }
    if (renaming) {
        ImGui::SameLine();
        std::array<char, kMaximumEntityNameBytesUVE + 1U> renameBuffer{};
        m_hierarchyRenameBuffer.copy(renameBuffer.data(), renameBuffer.size() - 1U);
        if (m_hierarchyRenameFocusRequested) {
            ImGui::SetKeyboardFocusHere();
            m_hierarchyRenameFocusRequested = false;
        }
        const bool committed = ImGui::InputText("##hierarchy-rename", renameBuffer.data(), renameBuffer.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        m_hierarchyRenameBuffer = renameBuffer.data();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CancelHierarchyRenameUVE();
        } else if (committed) {
            if (SetSelectedEntityNameUVE(m_hierarchyRenameBuffer)) {
                InvalidateHierarchyFilterCacheUVE();
            }
            CancelHierarchyRenameUVE();
        }
    }
    // The scene root is the document itself: it has no parent to leave, so it is never a drag source.
    if (IsLifecycleCommandAllowedUVE() && IsDocumentEntityUVE(entity) && !IsSceneRootEntityUVE(entity) &&
        ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kHierarchyEntityPayloadUVE, &entity, sizeof(entity));
        ImGui::Text("Move %s", GetEntityDisplayLabelUVE(entity).c_str());
        ImGui::EndDragDropSource();
    }
    AcceptHierarchyDropTargetUVE(entity);
    if (open) {
        for (const Scene::EntityUVE child : children) {
            DrawHierarchyNodeUVE(child);
        }
        ImGui::TreePop();
    }
}

void EditorUVE::DrawHierarchyNodeContextMenuUVE(const Scene::EntityUVE entity) {
    // Right-click acts on the row under the cursor: it becomes the selection first (unless it is
    // already part of it), so every command below targets what the user pointed at.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsEntitySelectedUVE(entity)) {
        SelectEntityUVE(entity);
    }
    if (!ImGui::BeginPopupContextItem("##hierarchy-node-context")) {
        return;
    }
    const bool authoring = IsAuthoringCommandAllowedUVE();
    const bool lifecycle = IsLifecycleCommandAllowedUVE();
    const bool single = HasSingleDocumentSelectionUVE() && entity == m_selectedEntity;
    const bool sceneRoot = IsSceneRootEntityUVE(entity);

    ImGui::TextDisabled("%s", GetEntityDisplayLabelUVE(entity).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(!authoring || !single);
    if (ImGui::BeginMenu("Add Child Node")) {
        // New nodes go under the single selection, which the right-click has just made this row.
        DrawNodeLibraryMenuItemsUVE(
            [this](const Scene::Nodes::SceneNodeKindUVE kind) { static_cast<void>(CreateDocumentSceneNodeUVE(kind)); });
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Rename", "F2")) {
        m_hierarchyRenameEntity = entity;
        m_hierarchyRenameBuffer = GetEntityDisplayLabelUVE(entity);
        m_hierarchyRenameFocusRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::BeginDisabled(!lifecycle || !single || sceneRoot);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        static_cast<void>(DuplicateSelectedEntityUVE());
    }
    if (ImGui::MenuItem("Delete", "Del")) {
        static_cast<void>(DeleteSelectedEntityUVE());
    }
    ImGui::EndDisabled();
    if (sceneRoot && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("The scene root holds the whole scene and cannot be duplicated or deleted.");
    }
    ImGui::EndPopup();
}

} // namespace UVE::Editor
