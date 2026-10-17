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
#include "uve/math/scalar_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_object_icons_uve.h"
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
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"

namespace UVE::Editor {

namespace {

/// An eye, drawn rather than taken from a font so it never depends on the editor font's glyphs.
/// Open when the object is shown; closed (a lid line with lashes) when it is hidden.
void DrawEyeGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const bool open, const ImU32 color) {
    const float halfWidth = size * 0.42F;
    const float halfHeight = size * 0.24F;
    constexpr int kSegments = 10;
    const float thickness = std::max(1.0F, size * 0.08F);
    const auto lidPoint = [&](const float t, const float lift) {
        return ImVec2{center.x - halfWidth + (2.0F * halfWidth * t), center.y + (lift * std::sin(t * Math::kPiUVE))};
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
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight);
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
    // No title row: the Outliner's name sits in the header row beside the logo.
    ImGui::Begin(kPanelLabelSceneUVE, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
    DrawHierarchyBodyUVE();
    ImGui::End();
}

void EditorUVE::DrawHierarchyBodyUVE() {
    std::array<char, 256> filterBuffer{};
    m_hierarchyFilter.copy(filterBuffer.data(), filterBuffer.size() - 1U);
    const float addObjectButtonWidth = ImGui::GetFrameHeight();
    const bool canCreateObject = IsAuthoringCommandAllowedUVE();
    ImGui::PushID("scene-add-object");
    ImGui::BeginDisabled(!canCreateObject);
    if (ImGui::Button("+", ImVec2{addObjectButtonWidth, addObjectButtonWidth})) {
        m_objectPickerOpenRequested = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Add Object");
    }
    ImGui::PopID();
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
    // No "Script" shortcut button here anymore - it duplicated the already-existing Scripting
    // workspace tab (Scene / Scripting / Game) and only added clutter/clipping risk to this row.
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##hierarchy-filter", "Search Objects", filterBuffer.data(), filterBuffer.size())) {
        m_hierarchyFilter = filterBuffer.data();
        InvalidateHierarchyFilterCacheUVE();
    }
    DrawObjectPickerUVE();
    RebuildHierarchyFilterCacheUVE();
    if (m_selectedEntity != m_hierarchyRevealedEntity) {
        m_hierarchyRevealedEntity = m_selectedEntity;
        m_hierarchyRevealAncestors.clear();
        m_hierarchyRevealPending = m_hierarchyView.revealSelection && IsDocumentEntityUVE(m_selectedEntity);
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
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, m_hierarchyView.indentWidth);
        // In the Entity Editor the entity is the top of the tree; the scene root is not shown.
        if (m_entityEditSession.has_value()) {
            const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
            for (const Scene::EntityUVE top :
                 m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), sceneRoot)) {
                DrawHierarchyObjectUVE(top);
            }
        } else if (GetDocumentViewportUVE() != Scene::kInvalidEntityUVE) {
            // The level: the scene root is not shown; its Viewport, sun and environment are the top.
            const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
            for (const Scene::EntityUVE top :
                 m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), sceneRoot)) {
                DrawHierarchyObjectUVE(top);
            }
        } else {
            for (const Scene::EntityUVE root : GetDocumentRootsUVE()) {
                DrawHierarchyObjectUVE(root);
            }
        }
        ImGui::PopStyleVar();
        if (m_hierarchyView.dragToReparent && !GetDocumentRootsUVE().empty()) {
            // The hint is for an empty scene only; once there are objects the rest of the panel is still
            // the drop area for "move to the top", just without words in the way.
            const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
            const bool sceneEmpty = sceneRoot == Scene::kInvalidEntityUVE ||
                                    m_services->GetSceneGraphUVE()
                                        .GetChildrenUVE(m_services->GetEntityManagerUVE(), sceneRoot)
                                        .empty();
            if (sceneEmpty && !m_entityEditSession.has_value()) {
                ImGui::Separator();
                ImGui::TextDisabled("Drop entity here to make it a root");
            } else {
                ImGui::Dummy(ImVec2{ImGui::GetContentRegionAvail().x, std::max(8.0F, ImGui::GetContentRegionAvail().y)});
            }
            AcceptHierarchyDropTargetUVE(Scene::kInvalidEntityUVE);
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
    }
}

void EditorUVE::DrawHierarchyObjectUVE(const Scene::EntityUVE entity) {
    if (!IsHierarchyEntityVisibleUVE(entity)) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> children =
        m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity);
    // The arrow always opens and closes a row. A double-click does too only when that is the chosen
    // double-click action; otherwise it renames or focuses (below).
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    } else if (m_hierarchyView.doubleClick == HierarchyDoubleClickUVE::ExpandCollapse) {
        flags |= ImGuiTreeNodeFlags_OpenOnDoubleClick;
    }
    switch (m_hierarchyView.treeLines) {
        case HierarchyTreeLinesUVE::None:
            flags |= ImGuiTreeNodeFlags_DrawLinesNone;
            break;
        case HierarchyTreeLinesUVE::ToEachChild:
            flags |= ImGuiTreeNodeFlags_DrawLinesToNodes;
            break;
        case HierarchyTreeLinesUVE::FullHeight:
            flags |= ImGuiTreeNodeFlags_DrawLinesFull;
            break;
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
    } else if (entity == GetDocumentViewportUVE()) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once); // the level's folders show from the start
    }

    const bool renaming = entity == m_hierarchyRenameEntity;
    // Just enough leading space for the object icon drawn into it (see below) plus
    // a small gap - was 4 spaces, which (combined with TreeNodeEx's own arrow-toggle spacing that
    // every row reserves, leaf or not) pushed the icon+name noticeably right of the panel's left
    // edge instead of hugging it.
    // The gap is measured, not guessed: as many spaces as it takes to clear the icon plus a gap,
    // in the current font. A fixed two spaces was narrower than the icon, which then sat on top of
    // the name's first letter.
    const float spaceWidth = std::max(1.0F, ImGui::CalcTextSize(" ").x);
    const auto gapSpaces = m_hierarchyView.showIcons
                               ? static_cast<std::size_t>(
                                     std::ceil((kHierarchyObjectIconSizeUVE + 6.0F) / spaceWidth))
                               : std::size_t{0U};
    // The row's right-hand columns (eye, warning, script) are fixed; the name gives way to them.
    // A name that would run under the leftmost column this row uses is cut short with "..." and
    // shown in full on hover, instead of being painted over by the badges.
    const std::vector<std::string> warnings = GetObjectWarningsUVE(entity);
    const std::optional<std::string> script = GetObjectScriptPathUVE(entity);
    // With the eyes hidden their column is given back, and the badges move over into it.
    const float eyeColumns = m_hierarchyView.visibilityColumn == HierarchyVisibilityColumnUVE::Hidden ? 0.0F : 1.0F;
    float usedColumns = entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) ? eyeColumns : 0.0F;
    if (!warnings.empty()) {
        usedColumns = eyeColumns + 1.0F;
    }
    if (script.has_value()) {
        usedColumns = eyeColumns + 2.0F;
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
    // renaming an object, or narrowing the panel until its name was cut short, gave the row a new
    // ID and it forgot it was open.
    const std::string objectLabel = visibleLabel + "###entity-" + std::to_string(entity.index) + ":" +
                                  std::to_string(entity.generation);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(66, 84, 101, 235));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(101, 130, 154, 245));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(88, 112, 133, 240));
    }
    const bool open = ImGui::TreeNodeEx(objectLabel.c_str(), flags);
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
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    // The whole row, not just its label, so a badge or eye at the far edge counts as on the row.
    const bool rowHovered =
        ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2{ImGui::GetWindowPos().x, rowMin.y},
                                                               ImVec2{ImGui::GetWindowPos().x + ImGui::GetWindowWidth(),
                                                                      rowMax.y},
                                                               false);
    // The type goes after the name, dimmed, only when all of it fits in the room the name and the
    // right-hand columns leave: a type cut to "C..." says nothing. Whatever does not fit is in the
    // row's tooltip instead.
    const std::string_view typeName = GetObjectTypeNameUVE(entity);
    const std::string_view typeHint = GetHierarchyTypeHintUVE(fullName, typeName);
    bool typeHintShown = false;
    if (!renaming && m_hierarchyView.showTypeName && !nameTruncated && !typeHint.empty()) {
        const std::string hint(typeHint);
        const float hintStart = rowMin.x + ImGui::GetTreeNodeToLabelSpacing() +
                                ImGui::CalcTextSize(visibleLabel.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
        const float hintLimit = ImGui::GetWindowPos().x - ImGui::GetScrollX() + labelLimit;
        if (ImGui::CalcTextSize(hint.c_str()).x <= hintLimit - hintStart) {
            ImGui::GetWindowDrawList()->AddText(ImVec2{hintStart, (rowMin.y + rowMax.y - ImGui::GetTextLineHeight()) * 0.5F},
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), hint.c_str());
            typeHintShown = true;
        }
    }
    if (!renaming && m_hierarchyView.showIcons) {
        // Draws into the gap the row's own label prefix reserves before the name, so the icon lines
        // up with the name without a second ImGui column or child window just for one picture.
        // Whole pixels, so the texture's texels land on screen pixels and stay sharp.
        const std::uintptr_t icon =
            m_uiAssets.GetObjectIconTextureIdUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
        if (icon != 0U) {
            const ImVec2 iconMin{std::floor(rowMin.x + ImGui::GetTreeNodeToLabelSpacing()),
                                 std::floor(((rowMin.y + rowMax.y) - kHierarchyObjectIconSizeUVE) * 0.5F)};
            ImU32 iconTint = IM_COL32_WHITE;
            if (m_extendedPreferences.hierarchyColorCodeIcons) {
                using Kind = Scene::Objects::SceneObjectKindUVE;
                switch (Scene::ResolveSceneObjectKindUVE(entityManager, entity)) {
                    case Kind::MeshInstance3D:
                    case Kind::BoxMesh3D:
                    case Kind::SphereMesh3D:
                    case Kind::PlaneMesh3D:
                        iconTint = IM_COL32(100, 180, 255, 255);
                        break;
                    case Kind::Light3D:
                    case Kind::DirectionalLight3D:
                    case Kind::WorldEnvironment3D:
                    case Kind::ReflectionProbe3D:
                    case Kind::FogVolume3D:
                        iconTint = IM_COL32(255, 215, 80, 255);
                        break;
                    case Kind::Camera3D:
                    case Kind::Camera2D:
                    case Kind::AudioSource3D:
                        iconTint = IM_COL32(95, 220, 130, 255);
                        break;
                    case Kind::Static3D:
                    case Kind::Kinematic3D:
                    case Kind::Rigid3D:
                    case Kind::Collider3D:
                    case Kind::Area3D:
                    case Kind::Hitbox3D:
                    case Kind::Hurtbox3D:
                    case Kind::Projectile3D:
                        iconTint = IM_COL32(240, 110, 110, 255);
                        break;
                    case Kind::Canvas:
                    case Kind::UIText:
                    case Kind::UIImage:
                    case Kind::UIButton:
                        iconTint = IM_COL32(75, 225, 240, 255);
                        break;
                    case Kind::Skeleton3D:
                    case Kind::BoneAttachment3D:
                    case Kind::AnimationGraph:
                    case Kind::AnimationSequencer:
                    case Kind::TwoBoneIK3D:
                        iconTint = IM_COL32(215, 130, 255, 255);
                        break;
                    case Kind::NavMeshVolume3D:
                    case Kind::NavSeeker3D:
                        iconTint = IM_COL32(255, 150, 70, 255);
                        break;
                    case Kind::ParticleEmitter3D:
                    case Kind::Decal3D:
                        iconTint = IM_COL32(255, 130, 190, 255);
                        break;
                    default:
                        iconTint = IM_COL32(255, 255, 255, 255);
                        break;
                }
            }
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(icon), iconMin,
                ImVec2{iconMin.x + kHierarchyObjectIconSizeUVE, iconMin.y + kHierarchyObjectIconSizeUVE},
                ImVec2{0.0F, 0.0F}, ImVec2{1.0F, 1.0F}, iconTint);
        }
    }
    const bool typeHidden = m_hierarchyView.showTypeName && !typeHint.empty() && !typeHintShown;
    if ((nameTruncated || typeHidden) && !renaming && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(fullName.c_str());
        if (!typeName.empty() && typeName != fullName) {
            ImGui::TextDisabled("%.*s", static_cast<int>(typeName.size()), typeName.data());
        }
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked() && !renaming) {
        if (ImGui::GetIO().KeyCtrl) {
            ToggleEntitySelectionUVE(entity);
        } else {
            SelectEntityUVE(entity);
        }
    }
    if (!renaming) {
        DrawHierarchyObjectContextMenuUVE(entity);
    }
    if (!IsDocumentEntityUVE(entity)) {
        if (open) {
            ImGui::TreePop();
        }
        return;
    }
    // F2 renames the selected row. A double-click does what the Double-Click preference says:
    // rename, focus the object in the viewport, or open and close the row (handled by the tree object).
    const bool doubleClicked = !renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubleClicked && m_hierarchyView.doubleClick == HierarchyDoubleClickUVE::FocusInViewport &&
        CanFocusEntityInViewportUVE(entity)) {
        static_cast<void>(RequestViewportFocusUVE(entity));
    }
    const bool canRenameSelected =
        !renaming && HasSingleDocumentSelectionUVE() && entity == m_selectedEntity && IsAuthoringCommandAllowedUVE();
    if (canRenameSelected &&
        (ImGui::IsKeyPressed(ImGuiKey_F2) ||
         (doubleClicked && m_hierarchyView.doubleClick == HierarchyDoubleClickUVE::Rename))) {
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
    if (m_hierarchyView.dragToReparent && IsLifecycleCommandAllowedUVE() && IsDocumentEntityUVE(entity) &&
        !IsStructuralRootUVE(entity) && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kHierarchyEntityPayloadUVE, &entity, sizeof(entity));
        ImGui::Text("Move %s", GetEntityDisplayLabelUVE(entity).c_str());
        ImGui::EndDragDropSource();
    }
    AcceptHierarchyDropTargetUVE(entity);
    if (!renaming) {
        DrawHierarchyRowBadgesUVE(warnings, script, eyeColumns);
        DrawHierarchyVisibilityToggleUVE(entity, rowHovered);
    }
    if (open) {
        for (const Scene::EntityUVE child : children) {
            DrawHierarchyObjectUVE(child);
        }
        ImGui::TreePop();
    }
}

void EditorUVE::DrawHierarchyObjectContextMenuUVE(const Scene::EntityUVE entity) {
    // Right-click acts on the row under the cursor: it becomes the selection first (unless it is
    // already part of it), so every command below targets what the user pointed at.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsEntitySelectedUVE(entity)) {
        SelectEntityUVE(entity);
    }
    if (!ImGui::BeginPopupContextItem("##hierarchy-object-context")) {
        return;
    }
    const bool authoring = IsAuthoringCommandAllowedUVE();
    const bool lifecycle = IsLifecycleCommandAllowedUVE();
    const bool single = HasSingleDocumentSelectionUVE() && entity == m_selectedEntity;
    const bool sceneRoot = IsStructuralRootUVE(entity);

    ImGui::TextDisabled("%s", GetEntityDisplayLabelUVE(entity).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(!authoring || !single);
    // Opens the same searchable picker as the + button. New objects go under the single selection,
    // which the right-click has just made this row.
    if (ImGui::MenuItem("Add Child Object...")) {
        m_objectPickerOpenRequested = true;
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
        ImGui::SetTooltip("This object has no position in the scene to look at.");
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const bool hideable = entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity);
    const bool visible =
        hideable && entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visible;
    if (ImGui::MenuItem(hideable && !visible ? "Show" : "Hide", nullptr, false, authoring && hideable)) {
        static_cast<void>(SetEntityVisibleUVE(entity, !visible));
    }
    if (!hideable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This object draws nothing, so there is nothing to hide.");
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
    // Place among its siblings; the right-click has already made this row the selection.
    DrawCommandMenuItemUVE("edit.moveUp");
    DrawCommandMenuItemUVE("edit.moveDown");
    DrawCommandMenuItemUVE("edit.moveToTop");
    DrawCommandMenuItemUVE("edit.moveToBottom");
    ImGui::Separator();
    ImGui::BeginDisabled(!lifecycle || !single || sceneRoot);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        static_cast<void>(DuplicateSelectedEntityUVE());
    }
    if (ImGui::MenuItem("Delete", "Del")) {
        static_cast<void>(DeleteSelectedEntityUVE());
        ImGui::CloseCurrentPopup();
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

    // One pass over the parent links instead of a children query per object, which scans the whole
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
    std::vector<std::pair<Scene::EntityUVE, std::int64_t>> pending{{entity, 0}};
    std::unordered_set<Scene::EntityUVE> visited;
    const std::int64_t depthLimit = m_extendedPreferences.hierarchyExpandDepthLimit > 0
                                        ? m_extendedPreferences.hierarchyExpandDepthLimit
                                        : 16;
    while (!pending.empty()) {
        const auto [current, depth] = pending.back();
        pending.pop_back();
        if (open && depth >= depthLimit) {
            continue;
        }
        const auto children = childrenOf.find(current);
        if (!visited.insert(current).second || children == childrenOf.end()) {
            continue;
        }
        m_hierarchyPendingRowOpen[current] = open;
        for (const Scene::EntityUVE child : children->second) {
            pending.emplace_back(child, depth + 1);
        }
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


void EditorUVE::DrawObjectPickerUVE() {
    // The Scene panel's "+" is small on purpose: the level's own furniture - a folder to organise it,
    // the sun and the sky. Everything else is made in the Content Browser ("+ Add" or right-click)
    // and dragged into the level, so the world is built from assets instead of loose objects.
    constexpr const char* kPopupId = "##object-picker";
    static char s_filter[64] = "";
    if (m_objectPickerOpenRequested) {
        m_objectPickerOpenRequested = false;
        s_filter[0] = '\0';
        ImGui::OpenPopup(kPopupId);
    }
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }
    const Scene::EntityUVE parent =
        (m_selectedEntity != Scene::kInvalidEntityUVE && IsDocumentEntityUVE(m_selectedEntity) &&
         HasSingleDocumentSelectionUVE())
            ? m_selectedEntity
            : Scene::kInvalidEntityUVE;
    // In a level the three entries go to different places, so the folder names its own.
    const bool levelLayout = IsOutlinerLayoutActiveUVE() && GetDocumentViewportUVE() != Scene::kInvalidEntityUVE;
    if (levelLayout) {
        ImGui::TextDisabled("Add to the level");
    } else {
        ImGui::TextDisabled("%s", parent != Scene::kInvalidEntityUVE
                                      ? ("Add to " + GetEntityDisplayLabelUVE(parent)).c_str()
                                      : "Add to the scene");
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##picker_search", "Search objects & components...", s_filter, sizeof(s_filter));
    }
    ImGui::Separator();
    const auto item = [this](const Scene::Objects::SceneObjectKindUVE kind, const char* const label,
                             const char* const shortcut, const char* const tooltip) {
        DrawObjectPickerIconUVE(m_uiAssets.GetObjectIconTextureIdUVE(kind));
        const bool picked = ImGui::MenuItem(label, shortcut);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return picked;
    };
    if (levelLayout) {
        const std::string folderLabel =
            "New Folder in " + GetEntityDisplayLabelUVE(ResolveNewObjectParentForUVE(Scene::Objects::SceneObjectKindUVE::Folder));
        if (item(Scene::Objects::SceneObjectKindUVE::Folder, folderLabel.c_str(), nullptr,
                 "Groups objects in this panel. It has no position, so nothing moves in the world.")) {
            static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Folder));
        }
        ImGui::Separator();
        const bool hasSun = FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D) != Scene::kInvalidEntityUVE;
        const bool hasEnvironment =
            FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D) != Scene::kInvalidEntityUVE;
        if (!hasSun && item(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D, "DirectionalLight3D", nullptr,
                            "The sun: a light from far away that falls on the whole level and casts its shadows.")) {
            static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D));
        }
        if (!hasEnvironment && item(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D, "WorldEnvironment", nullptr,
                                    "The sky, ambient light and fog of the level.")) {
            static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D));
        }
    } else {
        struct PickerEntryUVE {
            Scene::Objects::SceneObjectKindUVE kind;
            const char* label;
            const char* category;
            const char* tooltip;
        };
        static constexpr std::array<PickerEntryUVE, 29> kPickerEntries{{
            {Scene::Objects::SceneObjectKindUVE::MeshInstance3D, "MeshInstance3D", "3D Visuals & Geometry", "A 3D mesh instance."},
            {Scene::Objects::SceneObjectKindUVE::BoxMesh3D, "BoxMesh3D", "3D Visuals & Geometry", "A 3D box primitive."},
            {Scene::Objects::SceneObjectKindUVE::SphereMesh3D, "SphereMesh3D", "3D Visuals & Geometry", "A 3D sphere primitive."},
            {Scene::Objects::SceneObjectKindUVE::PlaneMesh3D, "PlaneMesh3D", "3D Visuals & Geometry", "A 3D plane primitive."},
            {Scene::Objects::SceneObjectKindUVE::Object3D, "Object3D", "Structure & Organization", "An empty 3D node / container."},
            {Scene::Objects::SceneObjectKindUVE::Marker3D, "Marker3D", "Structure & Organization", "A spatial marker or transform anchor."},
            {Scene::Objects::SceneObjectKindUVE::Folder, "Folder", "Structure & Organization", "A folder grouping."},
            {Scene::Objects::SceneObjectKindUVE::Camera3D, "Camera3D", "Camera", "A 3D perspective or orthographic camera."},
            {Scene::Objects::SceneObjectKindUVE::SpringArm3D, "SpringArm3D", "Camera", "A boom arm for third-person cameras."},
            {Scene::Objects::SceneObjectKindUVE::Skeleton3D, "Skeleton3D", "Animation & Rigging", "A skeletal bone hierarchy."},
            {Scene::Objects::SceneObjectKindUVE::AnimationSequencer, "AnimationSequencer", "Animation & Rigging", "Plays animation clips."},
            {Scene::Objects::SceneObjectKindUVE::AnimationGraph, "AnimationGraph", "Animation & Rigging", "Animation state machine and blending graph."},
            {Scene::Objects::SceneObjectKindUVE::BoneAttachment3D, "BoneAttachment3D", "Animation & Rigging", "Attaches objects to a specific bone."},
            {Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D, "TwoBoneIK3D", "Animation & Rigging", "Two-bone inverse kinematics solver constraint."},
            {Scene::Objects::SceneObjectKindUVE::DirectionalLight3D, "DirectionalLight3D", "Lighting", "A directional light source."},
            {Scene::Objects::SceneObjectKindUVE::Light3D, "Light3D (Omni / Spot)", "Lighting", "An omni or spot light."},
            {Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D, "ReflectionProbe3D", "Lighting", "Captures reflections for the area around it."},
            {Scene::Objects::SceneObjectKindUVE::FogVolume3D, "FogVolume3D", "Lighting", "Local volumetric fog."},
            {Scene::Objects::SceneObjectKindUVE::Character3D, "Character3D", "Physics & Collision", "A kinematic character controller body."},
            {Scene::Objects::SceneObjectKindUVE::Rigid3D, "Rigid3D", "Physics & Collision", "A dynamic simulated physics body."},
            {Scene::Objects::SceneObjectKindUVE::Static3D, "Static3D", "Physics & Collision", "A static collidable body."},
            {Scene::Objects::SceneObjectKindUVE::Collider3D, "Collider3D", "Physics & Collision", "A collision shape."},
            {Scene::Objects::SceneObjectKindUVE::Area3D, "Area3D", "Physics & Collision", "Trigger volume."},
            {Scene::Objects::SceneObjectKindUVE::RayCast3D, "RayCast3D", "Physics & Collision", "Raycast query."},
            {Scene::Objects::SceneObjectKindUVE::Hitbox3D, "Hitbox3D", "Physics & Collision", "A combat attack hitbox."},
            {Scene::Objects::SceneObjectKindUVE::Hurtbox3D, "Hurtbox3D", "Physics & Collision", "A combat damage receiver."},
            {Scene::Objects::SceneObjectKindUVE::AudioSource3D, "AudioSource3D", "Audio & Effects", "Spatial 3D audio emitter."},
            {Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D, "ParticleEmitter3D", "Audio & Effects", "3D particle effect system."},
            {Scene::Objects::SceneObjectKindUVE::Decal3D, "Decal3D", "Audio & Effects", "Projects a texture onto geometry."}
        }};

        if (s_filter[0] != '\0') {
            const std::string_view query{s_filter};
            int matched = 0;
            for (const auto& entry : kPickerEntries) {
                if (ContainsCaseInsensitiveUVE(entry.label, query) ||
                    ContainsCaseInsensitiveUVE(entry.category, query) ||
                    ContainsCaseInsensitiveUVE(entry.tooltip, query)) {
                    matched++;
                    if (item(entry.kind, entry.label, nullptr, entry.tooltip)) {
                        static_cast<void>(CreateDocumentSceneObjectUVE(entry.kind, parent));
                    }
                }
            }
            if (matched == 0) {
                ImGui::TextDisabled("No matching objects");
            }
        } else {
            if (ImGui::BeginMenu("3D Visuals & Geometry")) {
                if (item(Scene::Objects::SceneObjectKindUVE::MeshInstance3D, "MeshInstance3D", nullptr, "A 3D mesh instance.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::MeshInstance3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::BoxMesh3D, "BoxMesh3D", nullptr, "A 3D box primitive.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoxMesh3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::SphereMesh3D, "SphereMesh3D", nullptr, "A 3D sphere primitive.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::SphereMesh3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::PlaneMesh3D, "PlaneMesh3D", nullptr, "A 3D plane primitive.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::PlaneMesh3D, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Structure & Organization")) {
                if (item(Scene::Objects::SceneObjectKindUVE::Object3D, "Object3D", nullptr, "An empty 3D node / container.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Object3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Marker3D, "Marker3D", nullptr, "A spatial marker or transform anchor.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Marker3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Folder, "Folder", nullptr, "A folder grouping.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Folder, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Camera")) {
                if (item(Scene::Objects::SceneObjectKindUVE::Camera3D, "Camera3D", nullptr, "A 3D perspective or orthographic camera.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Camera3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::SpringArm3D, "SpringArm3D", nullptr, "A boom arm for third-person cameras.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::SpringArm3D, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Animation & Rigging")) {
                if (item(Scene::Objects::SceneObjectKindUVE::Skeleton3D, "Skeleton3D", nullptr, "A skeletal bone hierarchy.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Skeleton3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::AnimationSequencer, "AnimationSequencer", nullptr, "Plays animation clips.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationSequencer, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::AnimationGraph, "AnimationGraph", nullptr, "Animation state machine and blending graph.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AnimationGraph, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::BoneAttachment3D, "BoneAttachment3D", nullptr, "Attaches objects to a specific bone.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::BoneAttachment3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D, "TwoBoneIK3D", nullptr, "Two-bone inverse kinematics solver constraint.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lighting")) {
                if (item(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D, "DirectionalLight3D", nullptr, "A directional light source.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Light3D, "Light3D (Omni / Spot)", nullptr, "An omni or spot light.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Light3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D, "ReflectionProbe3D", nullptr, "Captures reflections for the area around it.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::FogVolume3D, "FogVolume3D", nullptr, "Local volumetric fog.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::FogVolume3D, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Physics & Collision")) {
                if (item(Scene::Objects::SceneObjectKindUVE::Character3D, "Character3D", nullptr, "A kinematic character controller body.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Character3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Rigid3D, "Rigid3D", nullptr, "A dynamic simulated physics body.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Rigid3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Static3D, "Static3D", nullptr, "A static collidable body.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Static3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Collider3D, "Collider3D", nullptr, "A collision shape.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Collider3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Area3D, "Area3D", nullptr, "Trigger volume.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Area3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::RayCast3D, "RayCast3D", nullptr, "Raycast query.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::RayCast3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Hitbox3D, "Hitbox3D", nullptr, "A combat attack hitbox.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Hitbox3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Hurtbox3D, "Hurtbox3D", nullptr, "A combat damage receiver.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Hurtbox3D, parent));
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Audio & Effects")) {
                if (item(Scene::Objects::SceneObjectKindUVE::AudioSource3D, "AudioSource3D", nullptr, "Spatial 3D audio emitter.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::AudioSource3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D, "ParticleEmitter3D", nullptr, "3D particle effect system.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D, parent));
                }
                if (item(Scene::Objects::SceneObjectKindUVE::Decal3D, "Decal3D", nullptr, "Projects a texture onto geometry.")) {
                    static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Decal3D, parent));
                }
                ImGui::EndMenu();
            }
        }
    }
    ImGui::EndPopup();
}

void EditorUVE::DrawHierarchyVisibilityToggleUVE(const Scene::EntityUVE entity, const bool rowHovered) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Only objects that can be hidden get an eye; the scene root and plain Objects have no Visibility.
    if (!entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        return;
    }
    const Scene::VisibilityComponentUVE& visibility = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
    if (!ShouldDrawHierarchyEyeUVE(m_hierarchyView.visibilityColumn, rowHovered, visibility.visible)) {
        return;
    }
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
    // an object that is invisible only because of its parent does not look like it was switched off.
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
                          : !visibility.visibleInHierarchy ? "Hidden by a parent - click to hide this object too"
                                                           : "Visible - click to hide");
    }
    if (clicked) {
        static_cast<void>(SetEntityVisibleUVE(entity, !visibility.visible));
    }
}

std::vector<std::string> EditorUVE::GetObjectWarningsUVE(const Scene::EntityUVE entity) const {
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
    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(entity) &&
        entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(entity).skeletonAssetPath.empty()) {
        warnings.emplace_back("No source model is set, so the skeleton has no bones.");
    }
    return warnings;
}

std::optional<std::string> EditorUVE::GetObjectScriptPathUVE(const Scene::EntityUVE entity) const {
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return std::nullopt;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    return path.empty() ? std::nullopt : std::optional<std::string>{path};
}

void EditorUVE::DrawHierarchyRowBadgesUVE(const std::vector<std::string>& warnings,
                                          const std::optional<std::string>& script, const float eyeColumns) {
    // Two fixed columns left of the eye - warning nearest it, then script - so the same badge
    // lines up down the whole tree and is found by scanning one column. With the eyes turned off
    // (`eyeColumns` 0) the badges take the right edge.
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
        badge("##script", eyeColumns + 2.0F,
              [&](const ImVec2 center, const float glyphSize) {
                  DrawScriptGlyphUVE(drawList, center, glyphSize, IM_COL32(120, 170, 245, 255));
              },
              [&] {
                  ImGui::TextDisabled("Script");
                  ImGui::TextUnformatted(script->c_str());
              });
    }
    if (!warnings.empty()) {
        badge("##warning", eyeColumns + 1.0F,
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
