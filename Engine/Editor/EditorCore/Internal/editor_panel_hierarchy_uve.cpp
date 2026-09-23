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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_node_icons_uve.h"
#include "editor_text_search_uve.h"

#include "uve/asset/i_asset_database_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Editor {

namespace {

/// An eye, drawn rather than taken from a font so it never depends on the editor font's glyphs.
/// Open when the node is shown; closed (a lid line with lashes) when it is hidden.
void DrawEyeGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const bool open, const ImU32 color) {
    const float halfWidth = size * 0.42F;
    const float halfHeight = size * 0.24F;
    constexpr int kSegments = 10;
    constexpr float kPi = 3.14159265F;
    const float thickness = std::max(1.0F, size * 0.08F);
    const auto lidPoint = [&](const float t, const float lift) {
        return ImVec2{center.x - halfWidth + (2.0F * halfWidth * t), center.y + (lift * std::sin(t * kPi))};
    };
    if (open) {
        // Two arcs meeting at the corners make the almond outline; a filled pupil sits inside.
        for (const float lift : {-halfHeight, halfHeight}) {
            for (int i = 0; i <= kSegments; ++i) {
                drawList.PathLineTo(lidPoint(static_cast<float>(i) / static_cast<float>(kSegments), lift));
            }
            drawList.PathStroke(color, 0, thickness);
        }
        drawList.AddCircleFilled(center, size * 0.12F, color, 12);
        return;
    }
    // Closed: the lower lid only, with three short lashes.
    const float lid = halfHeight * 0.6F;
    for (int i = 0; i <= kSegments; ++i) {
        drawList.PathLineTo(lidPoint(static_cast<float>(i) / static_cast<float>(kSegments), lid));
    }
    drawList.PathStroke(color, 0, thickness);
    for (const float t : {0.25F, 0.5F, 0.75F}) {
        const ImVec2 root = lidPoint(t, lid);
        drawList.AddLine(root, ImVec2{root.x + ((t - 0.5F) * size * 0.25F), root.y + (size * 0.16F)}, color, thickness);
    }
}

// `text` cut at a character boundary and ended with "..." so it fits `maxWidth` in the current
// font; unchanged when it already fits, and just "..." when nothing else does.
std::string FitTextToWidthUVE(const std::string& text, const float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    constexpr const char* kEllipsis = "...";
    std::size_t length = text.size();
    while (length > 0U) {
        // Step back one whole UTF-8 character: skip continuation bytes (10xxxxxx).
        do {
            --length;
        } while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U);
        const std::string candidate = text.substr(0U, length) + kEllipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return kEllipsis;
}

// An amber warning triangle with a dark exclamation mark.
void DrawWarningGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size) {
    const float half = size * 0.42F;
    const ImVec2 top{center.x, center.y - half};
    const ImVec2 left{center.x - half, center.y + (half * 0.8F)};
    const ImVec2 right{center.x + half, center.y + (half * 0.8F)};
    drawList.AddTriangleFilled(top, right, left, IM_COL32(236, 172, 52, 255));
    const ImU32 mark = IM_COL32(28, 22, 12, 255);
    const float stroke = std::max(1.0F, size * 0.1F);
    drawList.AddLine(ImVec2{center.x, center.y - (half * 0.35F)}, ImVec2{center.x, center.y + (half * 0.25F)}, mark,
                     stroke);
    drawList.AddCircleFilled(ImVec2{center.x, center.y + (half * 0.55F)}, stroke * 0.6F, mark, 8);
}

// A script: a pair of braces, drawn as strokes so it never depends on the font.
void DrawScriptGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const ImU32 color) {
    const float halfHeight = size * 0.36F;
    const float halfWidth = size * 0.30F;
    const float notch = size * 0.10F;
    const float stroke = std::max(1.0F, size * 0.09F);
    for (const float side : {-1.0F, 1.0F}) {
        const float outer = center.x + (side * halfWidth);
        const float inner = outer - (side * notch);
        drawList.PathLineTo(ImVec2{inner + (side * notch * 0.2F), center.y - halfHeight});
        drawList.PathLineTo(ImVec2{inner, center.y - (halfHeight * 0.75F)});
        drawList.PathLineTo(ImVec2{inner, center.y - (halfHeight * 0.2F)});
        drawList.PathLineTo(ImVec2{outer, center.y});
        drawList.PathLineTo(ImVec2{inner, center.y + (halfHeight * 0.2F)});
        drawList.PathLineTo(ImVec2{inner, center.y + (halfHeight * 0.75F)});
        drawList.PathLineTo(ImVec2{inner + (side * notch * 0.2F), center.y + halfHeight});
        drawList.PathStroke(color, 0, stroke);
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
    const auto pendingOpen = m_hierarchyPendingRowOpen.find(entity);
    if (IsHierarchyFilterActiveUVE()) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    } else if (pendingOpen != m_hierarchyPendingRowOpen.end()) {
        ImGui::SetNextItemOpen(pendingOpen->second, ImGuiCond_Always);
        m_hierarchyPendingRowOpen.erase(pendingOpen);
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
    // The row's right-hand columns (eye, warning, script) are fixed; the name gives way to them.
    // A name that would run under the leftmost column this row uses is cut short with "..." and
    // shown in full on hover, instead of being painted over by the badges.
    const std::vector<std::string> warnings = GetNodeWarningsUVE(entity);
    const std::optional<std::string> script = GetNodeScriptPathUVE(entity);
    float usedColumns = entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) ? 1.0F : 0.0F;
    if (!warnings.empty()) {
        usedColumns = 2.0F;
    }
    if (script.has_value()) {
        usedColumns = 3.0F;
    }
    const std::string fullName = GetEntityDisplayLabelUVE(entity);
    const float labelStart = ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing() +
                             (static_cast<float>(gapSpaces) * spaceWidth);
    const float labelLimit = ImGui::GetWindowContentRegionMax().x - (usedColumns * ImGui::GetFrameHeight()) -
                             ImGui::GetStyle().ItemSpacing.x;
    const std::string shownName = FitTextToWidthUVE(fullName, labelLimit - labelStart);
    const bool nameTruncated = shownName.size() != fullName.size();
    const std::string visibleLabel = renaming ? "" : std::string(gapSpaces, ' ') + shownName;
    // "###" keys the row on the entity alone. With "##" the visible text was part of the ID, so
    // renaming a node, or narrowing the panel until its name was cut short, gave the row a new
    // ID and it forgot it was open.
    const std::string nodeLabel = visibleLabel + "###entity-" + std::to_string(entity.index) + ":" +
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
    if (nameTruncated && !renaming && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", fullName.c_str());
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
    if (!renaming) {
        DrawHierarchyRowBadgesUVE(warnings, script);
        DrawHierarchyVisibilityToggleUVE(entity);
    }
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
    const bool focusable = CanFocusEntityInViewportUVE(entity);
    if (ImGui::MenuItem("Focus in Viewport", "F", false, focusable)) {
        static_cast<void>(RequestViewportFocusUVE(entity));
    }
    if (!focusable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This node has no position in the scene to look at.");
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const bool hideable = entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity);
    const bool visible =
        hideable && entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visible;
    if (ImGui::MenuItem(hideable && !visible ? "Show" : "Hide", nullptr, false, authoring && hideable)) {
        static_cast<void>(SetEntityVisibleUVE(entity, !visible));
    }
    if (!hideable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This node draws nothing, so there is nothing to hide.");
    }
    ImGui::Separator();
    const std::vector<Scene::EntityUVE> children = m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity);
    const bool hasChildren = std::any_of(children.begin(), children.end(),
                                         [this](const Scene::EntityUVE child) { return IsDocumentEntityUVE(child); });
    if (ImGui::MenuItem("Expand Branch", nullptr, false, hasChildren)) {
        static_cast<void>(SetHierarchyBranchOpenUVE(entity, true));
    }
    if (ImGui::MenuItem("Collapse Branch", nullptr, false, hasChildren)) {
        static_cast<void>(SetHierarchyBranchOpenUVE(entity, false));
    }
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

bool EditorUVE::SetHierarchyBranchOpenUVE(const Scene::EntityUVE entity, const bool open) {
    if (!IsDocumentEntityUVE(entity)) {
        return false;
    }
    // A request left for a row that has since been deleted would never be drawn; drop those here,
    // so the map only ever holds live rows.
    std::erase_if(m_hierarchyPendingRowOpen,
                  [this](const auto& pending) { return !IsDocumentEntityUVE(pending.first); });

    // One pass over the parent links instead of a children query per node, which scans the whole
    // scene each time.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<std::pair<Scene::EntityUVE, Scene::EntityUVE>> links;
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [&links](const Scene::EntityUVE child, const Scene::HierarchyComponentUVE& hierarchy) {
            links.emplace_back(hierarchy.parent, child);
        });
    std::unordered_map<Scene::EntityUVE, std::vector<Scene::EntityUVE>> childrenOf;
    for (const auto& [parent, child] : links) {
        if (parent != Scene::kInvalidEntityUVE && IsDocumentEntityUVE(child)) {
            childrenOf[parent].push_back(child);
        }
    }

    // Only rows with children have an open state; a leaf has nothing to open. The visited set
    // keeps a malformed parent loop from walking forever.
    std::vector<Scene::EntityUVE> pending{entity};
    std::unordered_set<Scene::EntityUVE> visited;
    while (!pending.empty()) {
        const Scene::EntityUVE current = pending.back();
        pending.pop_back();
        const auto children = childrenOf.find(current);
        if (!visited.insert(current).second || children == childrenOf.end()) {
            continue;
        }
        m_hierarchyPendingRowOpen[current] = open;
        pending.insert(pending.end(), children->second.begin(), children->second.end());
    }
    return true;
}

std::optional<bool> EditorUVE::GetPendingHierarchyRowOpenUVE(const Scene::EntityUVE entity) const {
    const auto pending = m_hierarchyPendingRowOpen.find(entity);
    if (pending == m_hierarchyPendingRowOpen.end()) {
        return std::nullopt;
    }
    return pending->second;
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

void EditorUVE::DrawHierarchyVisibilityToggleUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Only nodes that can be hidden get an eye; the scene root and plain Nodes have no Visibility.
    if (!entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        return;
    }
    const Scene::VisibilityComponentUVE& visibility = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
    const float size = ImGui::GetFrameHeight();
    // Pinned to the row's right edge, so the eyes form one column however deep a row is nested.
    const float rightEdge = ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightEdge - size));
    ImGui::PushID("##visibility");
    const bool clicked = ImGui::InvisibleButton("##eye", ImVec2{size, ImGui::GetTextLineHeight()});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    // Bright when shown, dim when hidden, and dimmer still when shown but hidden by a parent - so
    // a node that is invisible only because of its parent does not look like it was switched off.
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    if (!visibility.visible) {
        color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    } else if (!visibility.visibleInHierarchy) {
        color = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6F);
    }
    if (hovered) {
        color = ImGui::GetColorU32(ImGuiCol_Text);
    }
    DrawEyeGlyphUVE(*ImGui::GetWindowDrawList(), ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F},
                    ImGui::GetTextLineHeight(), visibility.visible, color);
    if (hovered) {
        ImGui::SetTooltip(!visibility.visible            ? "Hidden - click to show"
                          : !visibility.visibleInHierarchy ? "Hidden by a parent - click to hide this node too"
                                                           : "Visible - click to hide");
    }
    if (clicked) {
        static_cast<void>(SetEntityVisibleUVE(entity, !visibility.visible));
    }
}

std::vector<std::string> EditorUVE::GetNodeWarningsUVE(const Scene::EntityUVE entity) const {
    std::vector<std::string> warnings;
    if (!IsDocumentEntityUVE(entity)) {
        return warnings;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Asset::IAssetDatabaseUVE& assets = m_services->GetAssetDatabaseUVE();
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !IsTransformFiniteUVE(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity))) {
        warnings.emplace_back("The transform holds a value that is not a number or is infinite.");
    }
    if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (!path.empty() && !Scene::IsScriptAssetPathValidUVE(path)) {
            warnings.emplace_back("The script path is not a valid project path.");
        }
    }
    if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        const Scene::MeshComponentUVE& mesh = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
        if (mesh.meshGuid == Asset::kInvalidAssetGuidUVE) {
            warnings.emplace_back("No mesh is assigned, so nothing is drawn.");
        } else if (!assets.HasGuidUVE(mesh.meshGuid)) {
            warnings.emplace_back("The assigned mesh is no longer in the project.");
        }
        if (mesh.materialGuid != Asset::kInvalidAssetGuidUVE && !assets.HasGuidUVE(mesh.materialGuid)) {
            warnings.emplace_back("The assigned material is no longer in the project.");
        }
    }
    if (entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(entity) &&
        entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(entity).skeletonAssetPath.empty()) {
        warnings.emplace_back("No source model is set, so the skeleton has no bones.");
    }
    return warnings;
}

std::optional<std::string> EditorUVE::GetNodeScriptPathUVE(const Scene::EntityUVE entity) const {
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return std::nullopt;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    return path.empty() ? std::nullopt : std::optional<std::string>{path};
}

void EditorUVE::DrawHierarchyRowBadgesUVE(const std::vector<std::string>& warnings,
                                          const std::optional<std::string>& script) {
    // Two fixed columns left of the eye - warning nearest it, then script - so the same badge
    // lines up down the whole tree and is found by scanning one column.
    const float size = ImGui::GetFrameHeight();
    const float rightEdge = ImGui::GetWindowContentRegionMax().x;
    const float lineHeight = ImGui::GetTextLineHeight();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const auto badge = [&](const char* id, const float columnFromRight, auto&& drawGlyph, auto&& tooltip) {
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightEdge - (size * columnFromRight)));
        ImGui::InvisibleButton(id, ImVec2{size, lineHeight});
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        drawGlyph(ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F}, lineHeight);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            tooltip();
            ImGui::EndTooltip();
        }
    };

    ImGui::PushID("##row-badges");
    if (script.has_value()) {
        badge("##script", 3.0F,
              [&](const ImVec2 center, const float glyphSize) {
                  DrawScriptGlyphUVE(drawList, center, glyphSize, IM_COL32(120, 170, 245, 255));
              },
              [&] {
                  ImGui::TextDisabled("Script");
                  ImGui::TextUnformatted(script->c_str());
              });
    }
    if (!warnings.empty()) {
        badge("##warning", 2.0F,
              [&](const ImVec2 center, const float glyphSize) { DrawWarningGlyphUVE(drawList, center, glyphSize); },
              [&] {
                  ImGui::TextDisabled(warnings.size() == 1U ? "1 problem" : "%zu problems", warnings.size());
                  for (const std::string& warning : warnings) {
                      ImGui::BulletText("%s", warning.c_str());
                  }
              });
    }
    ImGui::PopID();
}

} // namespace UVE::Editor
