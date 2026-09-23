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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_node_icons_uve.h"
#include "editor_text_search_uve.h"

#include "uve/component/name_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Editor {


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
        m_nodePickerOpenRequested = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Add Node");
    }
    ImGui::PopID();
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
    // No "Script" shortcut button here anymore - it duplicated the already-existing Scripting
    // workspace tab (Scene / Scripting / Game) and only added clutter/clipping risk to this row.
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##hierarchy-filter", "Search Nodes", filterBuffer.data(), filterBuffer.size())) {
        m_hierarchyFilter = filterBuffer.data();
        InvalidateHierarchyFilterCacheUVE();
    }
    DrawNodePickerUVE();
    RebuildHierarchyFilterCacheUVE();
    if (m_selectedEntity != m_hierarchyRevealedEntity) {
        m_hierarchyRevealedEntity = m_selectedEntity;
        m_hierarchyRevealAncestors.clear();
        m_hierarchyRevealPending = IsDocumentEntityUVE(m_selectedEntity);
        Scene::EntityUVE cursor = m_selectedEntity;
        Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        // Bounded by the entity count, so a malformed parent loop can never hang the panel.
        for (std::size_t guard = 0U; m_hierarchyRevealPending && guard < 4096U &&
                                     TryGetDocumentParentUVE(cursor, parent) && parent != Scene::kInvalidEntityUVE;
             ++guard) {
            m_hierarchyRevealAncestors.push_back(parent);
            cursor = parent;
        }
    }
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
    } else if (m_hierarchyRevealPending &&
               std::find(m_hierarchyRevealAncestors.begin(), m_hierarchyRevealAncestors.end(), entity) !=
                   m_hierarchyRevealAncestors.end()) {
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
    if (m_hierarchyRevealPending && active) {
        if (!ImGui::IsItemVisible()) {
            ImGui::SetScrollHereY(0.5F);
        }
        m_hierarchyRevealPending = false;
        m_hierarchyRevealAncestors.clear();
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
    // Opens the same searchable picker as the + button. New nodes go under the single selection,
    // which the right-click has just made this row.
    if (ImGui::MenuItem("Add Child Node...")) {
        m_nodePickerOpenRequested = true;
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

void EditorUVE::DrawNodePickerUVE() {
    constexpr const char* kPopupId = "##node-picker";
    bool focusSearch = false;
    if (m_nodePickerOpenRequested) {
        m_nodePickerOpenRequested = false;
        m_nodePickerFilter.clear();
        m_nodePickerScrolledFilter.clear();
        focusSearch = true;
        ImGui::OpenPopup(kPopupId);
    }
    // Small and fixed: the list scrolls inside the box instead of the box growing to the screen's
    // height. Placed at the cursor, which is where the click that asked for it happened.
    const float fontSize = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2{fontSize * 17.0F, fontSize * 21.0F}, ImGuiCond_Always);
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }

    // Name the parent, so it is obvious where the new node will land.
    const Scene::EntityUVE parent =
        (m_selectedEntity != Scene::kInvalidEntityUVE && IsDocumentEntityUVE(m_selectedEntity) &&
         HasSingleDocumentSelectionUVE())
            ? m_selectedEntity
            : Scene::kInvalidEntityUVE;
    if (parent != Scene::kInvalidEntityUVE) {
        ImGui::TextDisabled("Add child to %s", GetEntityDisplayLabelUVE(parent).c_str());
    } else {
        ImGui::TextDisabled("Add node to the scene");
    }

    std::array<char, 128> buffer{};
    m_nodePickerFilter.copy(buffer.data(), buffer.size() - 1U);
    if (focusSearch) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##node-picker-search", "Search nodes", buffer.data(), buffer.size())) {
        m_nodePickerFilter = buffer.data();
    }
    const bool enterPressed = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    // A node matches on its name or its category, so "physics" lists the whole group.
    const auto matches = [this](const Scene::Nodes::SceneNodeDescriptorUVE& descriptor) {
        return descriptor.libraryCreatable && (ContainsCaseInsensitiveUVE(descriptor.displayName, m_nodePickerFilter) ||
                                               ContainsCaseInsensitiveUVE(descriptor.category, m_nodePickerFilter));
    };

    // The best match is what Enter takes and what is highlighted: a name that starts with the query
    // beats one that merely contains it, so "box" picks BoxMesh3D rather than Hitbox3D.
    const auto descriptors = Scene::Nodes::GetSceneNodeDescriptorsUVE();
    std::optional<Scene::Nodes::SceneNodeKindUVE> bestMatch;
    std::optional<Scene::Nodes::SceneNodeKindUVE> firstMatch;
    for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : descriptors) {
        if (!matches(descriptor)) {
            continue;
        }
        if (!firstMatch.has_value()) {
            firstMatch = descriptor.kind;
        }
        const std::string_view name = descriptor.displayName;
        if (!bestMatch.has_value() && name.size() >= m_nodePickerFilter.size() &&
            ContainsCaseInsensitiveUVE(name.substr(0U, m_nodePickerFilter.size()), m_nodePickerFilter)) {
            bestMatch = descriptor.kind;
        }
    }
    if (!bestMatch.has_value()) {
        bestMatch = firstMatch;
    }

    std::optional<Scene::Nodes::SceneNodeKindUVE> chosen;
    ImGui::Separator();
    if (ImGui::BeginChild("##node-picker-list", ImVec2{0.0F, 0.0F}, false)) {
        std::string_view shownCategory;
        for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : descriptors) {
            if (!matches(descriptor)) {
                continue;
            }
            // A category heading only above the first match in it, so an empty group never shows.
            if (descriptor.category != shownCategory) {
                if (!shownCategory.empty()) {
                    ImGui::Spacing();
                }
                ImGui::TextDisabled("%s", descriptor.category.data());
                shownCategory = descriptor.category;
            }
            ImGui::Indent(fontSize * 0.6F);
            const bool highlight = !m_nodePickerFilter.empty() && bestMatch == descriptor.kind;
            if (ImGui::Selectable(descriptor.displayName.data(), highlight)) {
                chosen = descriptor.kind;
            }
            if (highlight && m_nodePickerFilter != m_nodePickerScrolledFilter) {
                ImGui::SetScrollHereY(0.5F); // keep the Enter target in sight as the query changes
                m_nodePickerScrolledFilter = m_nodePickerFilter;
            }
            ImGui::Unindent(fontSize * 0.6F);
        }
        if (!firstMatch.has_value()) {
            ImGui::TextDisabled("No node matches \"%s\".", m_nodePickerFilter.c_str());
        }
    }
    ImGui::EndChild();

    // Enter takes the best match - the highlighted row - so typing a few letters and pressing
    // Enter is enough to add a node without touching the mouse.
    if (!chosen.has_value() && enterPressed && bestMatch.has_value()) {
        chosen = bestMatch;
    }
    if (chosen.has_value()) {
        static_cast<void>(CreateDocumentSceneNodeUVE(*chosen));
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace UVE::Editor
