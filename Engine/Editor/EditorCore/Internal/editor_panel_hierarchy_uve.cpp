// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scene Hierarchy panel: the outliner tree, its filter, inline/dialog rename, and the
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
// commands as the keyboard shortcuts, so both paths share one set of rules (the Object, for
// one, can be renamed but never duplicated, deleted or dragged).

#include "uve/editor/editor_uve.h"
#include "uve/editor/editor_settings_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_object_icons_uve.h"
#include "editor_text_search_uve.h"

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_file_system_uve.h"
#include "uve/asset/i_project_file_index_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/object/scene_folder_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/objects/3d/level_streamer_3d_uve.h"
#include "uve/objects/3d/lod_group_3d_uve.h"
#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/object/scene_object_registry_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {

namespace {

/// An eye, drawn rather than taken from a font so it never depends on the editor font's glyphs.
/// Open when the object is shown; closed (a lid line with lashes) when it is hidden.
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

// A red error circle with a contrasting cross, distinct from the amber warning triangle.
void DrawErrorGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size) {
    const float radius = size * 0.4F;
    drawList.AddCircleFilled(center, radius, IM_COL32(220, 72, 76, 255), 12);
    const float halfMark = size * 0.16F;
    const float stroke = std::max(1.0F, size * 0.1F);
    const ImU32 mark = IM_COL32(255, 255, 255, 255);
    drawList.AddLine(ImVec2{center.x - halfMark, center.y - halfMark},
                     ImVec2{center.x + halfMark, center.y + halfMark}, mark, stroke);
    drawList.AddLine(ImVec2{center.x + halfMark, center.y - halfMark},
                     ImVec2{center.x - halfMark, center.y + halfMark}, mark, stroke);
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

// A small editor-only padlock. The scene does not carry this state; the column and row menu toggle it.
void DrawLockGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const bool locked,
                      const ImU32 color) {
    constexpr float kPi = 3.14159265F;
    const float stroke = std::max(1.0F, size * 0.09F);
    const float bodyTop = center.y + (size * 0.02F);
    const float bodyBottom = center.y + (size * 0.38F);
    const float bodyHalfWidth = size * 0.28F;
    const float shackleRadius = size * 0.17F;
    if (locked) {
        drawList.PathArcTo(ImVec2{center.x, bodyTop}, shackleRadius, kPi, 2.0F * kPi, 8);
    } else {
        drawList.PathArcTo(ImVec2{center.x, bodyTop}, shackleRadius, kPi, 1.72F * kPi, 8);
        drawList.PathLineTo(ImVec2{center.x + (shackleRadius * 1.35F), bodyTop - (shackleRadius * 0.88F)});
    }
    drawList.PathStroke(color, 0, stroke);
    drawList.AddRectFilled(ImVec2{center.x - bodyHalfWidth, bodyTop},
                           ImVec2{center.x + bodyHalfWidth, bodyBottom}, color, size * 0.04F);
    if (locked) {
        const ImVec2 keyhole{center.x, bodyTop + ((bodyBottom - bodyTop) * 0.38F)};
        const ImU32 keyholeColor = IM_COL32(35, 28, 17, 220);
        drawList.AddCircleFilled(keyhole, size * 0.045F, keyholeColor, 8);
        drawList.AddLine(keyhole, ImVec2{keyhole.x, keyhole.y + (size * 0.09F)}, keyholeColor, stroke);
    }
}

} // namespace

void EditorUVE::DrawHierarchyPanelUVE() {
    if (!m_scenePanelVisible) {
        m_hierarchyShownOrder.clear();
        m_hierarchyShownOrderPrevious.clear();
        m_hierarchyBoxSelecting = false;
        m_hierarchyBoxSelectionBefore.clear();
        m_hierarchyBoxActiveBefore = Scene::kInvalidEntityUVE;
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
    m_hierarchyShownOrderPrevious.swap(m_hierarchyShownOrder);
    m_hierarchyShownOrder.clear();
    m_hierarchySelectionRowBounds.clear();
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Plain text follows Filter Mode. Use type:Camera or component:Script to search one field.");
    }
    DrawObjectPickerUVE();
    DrawChangeTypePickerUVE();
    DrawReparentPickerUVE();
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
        if (IsHierarchyFilterActiveUVE() && !m_hierarchyView.filterKeepAncestors) {
            // Without ancestor context, render only the matching rows as a flat list; walking only
            // document roots would hide matches whose nonmatching parents are not drawn.
            for (const Scene::EntityUVE match : m_cachedHierarchyVisibleEntities) {
                DrawHierarchyObjectUVE(match, true);
            }
        } else {
            // In the Entity Editor the entity is the top of the tree; the Object is not shown.
            const Scene::EntityUVE entityRoot =
                m_entityEditSession.has_value() ? GetEntityEditorRootUVE() : Scene::kInvalidEntityUVE;
            if (entityRoot != Scene::kInvalidEntityUVE) {
                DrawHierarchyObjectUVE(entityRoot);
            } else if (GetDocumentViewportUVE() != Scene::kInvalidEntityUVE) {
                // The level: the Object is not shown; its Viewport, sun and environment are the top.
                const Scene::EntityUVE rootObject = GetDocumentObjectUVE();
                for (const Scene::EntityUVE top : GetHierarchyChildrenInViewOrderUVE(rootObject)) {
                    DrawHierarchyObjectUVE(top);
                }
            } else {
                for (const Scene::EntityUVE root : SortHierarchyRowsUVE(GetDocumentRootsUVE())) {
                    DrawHierarchyObjectUVE(root);
                }
            }
        }
        ImGui::PopStyleVar();
        const bool canDropToTop = m_hierarchyView.dragToReparent && !GetDocumentRootsUVE().empty();
        if (canDropToTop) {
            // The hint is for an empty scene only; once there are objects the rest of the panel is still
            // the drop area for "move to the top", just without words in the way.
            const Scene::EntityUVE rootObject = GetDocumentObjectUVE();
            const bool sceneEmpty = rootObject == Scene::kInvalidEntityUVE ||
                                    m_services->GetSceneGraphUVE()
                                        .GetChildrenUVE(m_services->GetEntityManagerUVE(), rootObject)
                                        .empty();
            if (sceneEmpty && !m_entityEditSession.has_value()) {
                ImGui::Separator();
                ImGui::TextDisabled("Drop entity here to make it a root");
            }
        }
        const ImVec2 blankSpace = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##hierarchy-selection-background",
                               ImVec2{std::max(1.0F, blankSpace.x), std::max(8.0F, blankSpace.y)});
        const bool selectionBackgroundHovered =
            ImGui::IsItemHovered() || (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered());
        if (canDropToTop) {
            AcceptHierarchyDropTargetUVE(Scene::kInvalidEntityUVE);
        }

        const ImGuiIO& hierarchyIo = ImGui::GetIO();
        if (!m_hierarchyBoxSelecting && selectionBackgroundHovered && IsAuthoringCommandAllowedUVE() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_hierarchyBoxSelecting = true;
            m_hierarchyBoxAdditive = hierarchyIo.KeyCtrl || hierarchyIo.KeySuper;
            m_hierarchyBoxStartX = hierarchyIo.MousePos.x;
            m_hierarchyBoxStartY = hierarchyIo.MousePos.y;
            m_hierarchyBoxSelectionBefore = m_selectedEntities;
            m_hierarchyBoxActiveBefore = m_selectedEntity;
        }
        if (m_hierarchyBoxSelecting) {
            if (!IsAuthoringCommandAllowedUVE() || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_hierarchyBoxSelecting = false;
                m_hierarchyBoxSelectionBefore.clear();
                m_hierarchyBoxActiveBefore = Scene::kInvalidEntityUVE;
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 start{m_hierarchyBoxStartX, m_hierarchyBoxStartY};
                const ImVec2 current = hierarchyIo.MousePos;
                const ImVec2 minimum{std::min(start.x, current.x), std::min(start.y, current.y)};
                const ImVec2 maximum{std::max(start.x, current.x), std::max(start.y, current.y)};
                ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, IM_COL32(102, 168, 220, 32));
                ImGui::GetWindowDrawList()->AddRect(minimum, maximum, IM_COL32(120, 188, 236, 190));
            } else {
                ApplyHierarchyBoxSelectionUVE(m_hierarchySelectionRowBounds,
                                              m_hierarchyBoxStartX, m_hierarchyBoxStartY,
                                              hierarchyIo.MousePos.x, hierarchyIo.MousePos.y,
                                              m_hierarchyBoxAdditive, m_hierarchyBoxSelectionBefore,
                                              m_hierarchyBoxActiveBefore);
                m_hierarchyBoxSelecting = false;
                m_hierarchyBoxSelectionBefore.clear();
                m_hierarchyBoxActiveBefore = Scene::kInvalidEntityUVE;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
    }
    DrawHierarchyRenameDialogUVE();
    DrawUVScriptCreationDialogUVE();
    DrawHierarchyScriptAttachDialogUVE();
    DrawHierarchyReparentConfirmationUVE();
    DrawHierarchyDeleteConfirmationUVE();
    DrawHierarchySaveDefaultConfirmationUVE();
}

void EditorUVE::DrawHierarchyReparentConfirmationUVE() {
    constexpr const char* kPopupId = "Confirm Large Subtree Reparent###hierarchy-reparent-confirmation";
    if (m_hierarchyReparentPopupRequested) {
        m_hierarchyReparentPopupRequested = false;
        if (m_pendingHierarchyReparent.has_value()) {
            ImGui::OpenPopup(kPopupId);
            m_hierarchyReparentPopupWasOpened = true;
        }
    }
    if (!m_hierarchyReparentPopupWasOpened) {
        return;
    }

    bool modalOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &modalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!modalOpen || !m_pendingHierarchyReparent.has_value()) {
            CancelHierarchyReparentUVE();
            m_hierarchyReparentPopupWasOpened = false;
        }
        return;
    }
    if (!modalOpen || !m_pendingHierarchyReparent.has_value()) {
        CancelHierarchyReparentUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyReparentPopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    const PendingHierarchyReparentUVE pending = *m_pendingHierarchyReparent;
    if (!m_hierarchyView.dragToReparent || !m_hierarchyView.confirmLargeSubtreeReparent ||
        !IsLifecycleCommandAllowedUVE() || !IsDocumentEntityUVE(pending.entity) ||
        IsStructuralRootUVE(pending.entity) || !IsReparentableObjectUVE(pending.entity) ||
        (pending.newParent != Scene::kInvalidEntityUVE && !IsDocumentEntityUVE(pending.newParent))) {
        CancelHierarchyReparentUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyReparentPopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelHierarchyReparentUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyReparentPopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Reparent %s?", GetEntityDisplayLabelUVE(pending.entity).c_str());
    ImGui::TextWrapped("Its hierarchy subtree contains at least %zu entities. Continue?",
                       pending.subtreeEntityCount);
    if (ImGui::Button("Reparent", ImVec2{120.0F, 0.0F})) {
        static_cast<void>(ConfirmHierarchyReparentUVE());
        ImGui::CloseCurrentPopup();
        m_hierarchyReparentPopupWasOpened = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2{120.0F, 0.0F})) {
        CancelHierarchyReparentUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyReparentPopupWasOpened = false;
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

bool EditorUVE::RequestHierarchySaveDefaultUVE(const Scene::EntityUVE entity) {
    CancelHierarchySaveDefaultUVE();
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() || entity != m_selectedEntity ||
        IsStructuralRootUVE(entity) || IsEntityLockedUVE(entity)) {
        return false;
    }
    std::optional<ObjectDefaultExtrasPreviewUVE> preview =
        ComputeObjectDefaultExtrasForEntityUVE(entity);
    if (!preview.has_value()) {
        return false;
    }
    m_pendingHierarchySaveDefault = PendingHierarchySaveDefaultUVE{entity, std::move(*preview)};
    m_hierarchySaveDefaultPopupRequested = true;
    return true;
}

bool EditorUVE::ConfirmHierarchySaveDefaultUVE() {
    if (!m_pendingHierarchySaveDefault.has_value() || !IsAuthoringCommandAllowedUVE() ||
        !IsDocumentEntityUVE(m_pendingHierarchySaveDefault->entity)) {
        CancelHierarchySaveDefaultUVE();
        return false;
    }
    // Recomputed, not replayed: the node may have changed while the modal stood open.
    const std::optional<ObjectDefaultExtrasPreviewUVE> preview =
        ComputeObjectDefaultExtrasForEntityUVE(m_pendingHierarchySaveDefault->entity);
    if (!preview.has_value()) {
        CancelHierarchySaveDefaultUVE();
        return false;
    }
    const std::string id = EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE(
        Scene::Objects::GetSceneObjectTypeIdUVE(preview->kind));
    const bool saved = SetEditorSettingUVE(id, Config::SettingValueUVE{preview->extras});
    CancelHierarchySaveDefaultUVE();
    return saved;
}

void EditorUVE::CancelHierarchySaveDefaultUVE() noexcept {
    m_pendingHierarchySaveDefault.reset();
    m_hierarchySaveDefaultPopupRequested = false;
    m_hierarchySaveDefaultPopupWasOpened = false;
}

void EditorUVE::DrawHierarchySaveDefaultConfirmationUVE() {
    constexpr const char* kPopupId = "Save Node as Default###hierarchy-save-default-confirmation";
    if (m_hierarchySaveDefaultPopupRequested) {
        m_hierarchySaveDefaultPopupRequested = false;
        if (m_pendingHierarchySaveDefault.has_value()) {
            ImGui::OpenPopup(kPopupId);
            m_hierarchySaveDefaultPopupWasOpened = true;
        }
    }
    if (!m_hierarchySaveDefaultPopupWasOpened) {
        return;
    }

    bool modalOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &modalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!modalOpen || !m_pendingHierarchySaveDefault.has_value()) {
            CancelHierarchySaveDefaultUVE();
        }
        return;
    }
    const auto close = [&] {
        CancelHierarchySaveDefaultUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    };
    if (!modalOpen || !m_pendingHierarchySaveDefault.has_value()) {
        close();
        return;
    }
    const PendingHierarchySaveDefaultUVE pending = *m_pendingHierarchySaveDefault;
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(pending.entity) ||
        IsStructuralRootUVE(pending.entity) || IsEntityLockedUVE(pending.entity) ||
        m_selectedEntity != pending.entity) {
        close();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close();
        return;
    }

    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(pending.preview.kind);
    const std::string kindName = descriptor != nullptr ? std::string(descriptor->displayName) : "Unknown";
    const std::optional<Config::SettingValueUVE> currentValue = GetEditorSettingUVE(
        EditorSettingIdUVE::GetObjectDefaultExtrasSettingIdUVE(
            Scene::Objects::GetSceneObjectTypeIdUVE(pending.preview.kind)));
    const Config::SettingStringListUVE current =
        currentValue.has_value() ? std::get<Config::SettingStringListUVE>(*currentValue)
                                 : Config::SettingStringListUVE{};
    const auto bulletList = [](const Config::SettingStringListUVE& ids) {
        constexpr std::size_t kShown = 8U;
        for (std::size_t index = 0U; index < ids.size() && index < kShown; ++index) {
            ImGui::BulletText("%s", ids[index].c_str());
        }
        if (ids.size() > kShown) {
            ImGui::BulletText("...and %zu more.", ids.size() - kShown);
        }
    };
    ImGui::Text("Save %s as the %s default?", GetEntityDisplayLabelUVE(pending.entity).c_str(),
                kindName.c_str());
    if (!current.empty()) {
        ImGui::TextWrapped("Current %s defaults (replaced):", kindName.c_str());
        bulletList(current);
    }
    if (pending.preview.extras.empty()) {
        ImGui::TextWrapped("This node carries nothing beyond its recipe, so saving clears the %s defaults.",
                           kindName.c_str());
    } else {
        ImGui::TextWrapped("New %s objects would be born with:", kindName.c_str());
        bulletList(pending.preview.extras);
    }
    const std::size_t skippedTotal = pending.preview.skipped.size() + pending.preview.skippedUnnamed;
    if (skippedTotal != 0U) {
        ImGui::TextDisabled("%zu of this node's components cannot be extras and are skipped.",
                            skippedTotal);
        if (ImGui::IsItemHovered() && !pending.preview.skipped.empty()) {
            std::string names;
            for (const std::string& id : pending.preview.skipped) {
                names += (names.empty() ? "" : "\n") + id;
            }
            ImGui::SetTooltip("%s", names.c_str());
        }
    }
    ImGui::TextDisabled("Values are not captured: extras attach with engine defaults.");
    const bool unchanged = current == pending.preview.extras;
    if (unchanged) {
        ImGui::TextDisabled("The defaults already match this node.");
    }
    ImGui::BeginDisabled(unchanged);
    if (ImGui::Button("Save as Default")) {
        static_cast<void>(ConfirmHierarchySaveDefaultUVE());
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        close();
        return;
    }
    ImGui::EndPopup();
}

void EditorUVE::DrawHierarchyDeleteConfirmationUVE() {
    constexpr const char* kPopupId = "Confirm Delete Subtree###hierarchy-delete-confirmation";
    if (m_hierarchyDeletePopupRequested) {
        m_hierarchyDeletePopupRequested = false;
        if (m_pendingHierarchyDelete.has_value()) {
            ImGui::OpenPopup(kPopupId);
            m_hierarchyDeletePopupWasOpened = true;
        }
    }
    if (!m_hierarchyDeletePopupWasOpened) {
        return;
    }

    bool modalOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &modalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!modalOpen || !m_pendingHierarchyDelete.has_value()) {
            CancelHierarchyDeleteUVE();
            m_hierarchyDeletePopupWasOpened = false;
        }
        return;
    }
    if (!modalOpen || !m_pendingHierarchyDelete.has_value()) {
        CancelHierarchyDeleteUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyDeletePopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    const PendingHierarchyDeleteUVE pending = *m_pendingHierarchyDelete;
    if (!m_hierarchyView.confirmDeleteSubtree || !IsLifecycleCommandAllowedUVE() ||
        !IsDocumentEntityUVE(pending.entity) || IsStructuralRootUVE(pending.entity) ||
        m_selectedEntity != pending.entity) {
        CancelHierarchyDeleteUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyDeletePopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelHierarchyDeleteUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyDeletePopupWasOpened = false;
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Delete %s?", GetEntityDisplayLabelUVE(pending.entity).c_str());
    if (pending.subtreeEntityCount >= kHierarchyLargeSubtreeReparentThresholdUVE) {
        ImGui::TextWrapped("Its subtree holds at least %zu objects, all of which will go. Continue?",
                           pending.subtreeEntityCount);
    } else {
        ImGui::TextWrapped("Its subtree holds %zu objects, all of which will go. Continue?",
                           pending.subtreeEntityCount);
    }
    ImGui::TextDisabled("Undo restores the deleted objects.");
    if (ImGui::Button("Delete", ImVec2{120.0F, 0.0F})) {
        static_cast<void>(ConfirmHierarchyDeleteUVE());
        ImGui::CloseCurrentPopup();
        m_hierarchyDeletePopupWasOpened = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2{120.0F, 0.0F})) {
        CancelHierarchyDeleteUVE();
        ImGui::CloseCurrentPopup();
        m_hierarchyDeletePopupWasOpened = false;
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

void EditorUVE::DrawHierarchyRenameDialogUVE() {
    constexpr const char* kPopupId = "Rename Object###hierarchy-rename-dialog";
    if (m_hierarchyRenameDialogOpenRequested) {
        m_hierarchyRenameDialogOpenRequested = false;
        ImGui::OpenPopup(kPopupId);
    }
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (m_hierarchyRenameEntity == Scene::kInvalidEntityUVE || !m_hierarchyRenameUsesDialogUVE ||
        !IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        m_selectedEntity != m_hierarchyRenameEntity || !IsDocumentEntityUVE(m_hierarchyRenameEntity) ||
        IsEntityLockedUVE(m_hierarchyRenameEntity)) {
        CancelHierarchyRenameUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Rename %s", GetEntityDisplayLabelUVE(m_hierarchyRenameEntity).c_str());
    std::array<char, kMaximumEntityNameBytesUVE + 1U> renameBuffer{};
    m_hierarchyRenameBuffer.copy(renameBuffer.data(), renameBuffer.size() - 1U);
    ImGui::SetNextItemWidth(320.0F);
    if (m_hierarchyRenameFocusRequested) {
        ImGui::SetKeyboardFocusHere();
        m_hierarchyRenameFocusRequested = false;
    }
    const bool committedByEnter = ImGui::InputText("##hierarchy-rename-dialog", renameBuffer.data(), renameBuffer.size(),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    m_hierarchyRenameBuffer = renameBuffer.data();
    const bool committedByButton = ImGui::Button("Rename");
    ImGui::SameLine();
    const bool cancelledByButton = ImGui::Button("Cancel");
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || cancelledByButton) {
        CancelHierarchyRenameUVE();
        ImGui::CloseCurrentPopup();
    } else if (committedByEnter || committedByButton) {
        if (SetHierarchyEntityNameUVE(m_hierarchyRenameEntity, m_hierarchyRenameBuffer)) {
            InvalidateHierarchyFilterCacheUVE();
        }
        CancelHierarchyRenameUVE();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorUVE::DrawUVScriptCreationDialogUVE() {
    constexpr const char* kPopupId = "Create Script###hierarchy-create-script";
    if (m_uvScriptCreationDialogOpenRequested) {
        m_uvScriptCreationDialogOpenRequested = false;
        ImGui::OpenPopup(kPopupId);
        m_uvScriptCreationDialogWasOpened = true;
    }
    if (!m_uvScriptCreationDialogWasOpened) {
        return;
    }

    bool modalOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &modalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!modalOpen) {
            CancelUVScriptCreationDialogUVE();
        }
        return;
    }
    if (!modalOpen || !IsAuthoringCommandAllowedUVE() ||
        !CanCreateUVScriptForEntityUVE(m_uvScriptCreationTarget)) {
        CancelUVScriptCreationDialogUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelUVScriptCreationDialogUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("Create a script for an object");
    ImGui::Separator();
    const std::vector<Scene::EntityUVE> candidates = GetUVScriptTargetCandidatesUVE();
    const std::string targetLabel = GetEntityDisplayLabelUVE(m_uvScriptCreationTarget);
    ImGui::SetNextItemWidth(320.0F);
    if (ImGui::BeginCombo("Parent", targetLabel.c_str())) {
        for (const Scene::EntityUVE candidate : candidates) {
            const bool available = CanCreateUVScriptForEntityUVE(candidate);
            const std::string name = GetEntityDisplayLabelUVE(candidate);
            std::string itemLabel = name;
            if (!available) {
                const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                const bool locked = IsEntityLockedUVE(candidate);
                const bool scriptAttached = entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(candidate) &&
                                            !entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(candidate)
                                                 .scriptAssetPath.empty();
                const bool folder = entityManager.HasComponentUVE<Scene::FolderComponentUVE>(candidate);
                const char* const availability = locked          ? "  (locked)"
                                                : scriptAttached ? "  (script attached)"
                                                : folder         ? "  (folder)"
                                                                 : "  (unavailable)";
                itemLabel += availability;
            }
            const std::string candidateId =
                std::to_string(candidate.index) + ":" + std::to_string(candidate.generation);
            ImGui::PushID(candidateId.c_str());
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(itemLabel.c_str(), candidate == m_uvScriptCreationTarget)) {
                static_cast<void>(ChooseUVScriptCreationTargetUVE(candidate));
            }
            ImGui::EndDisabled();
            if (candidate == m_uvScriptCreationTarget) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::Text("Script type");
    ImGui::SameLine(120.0F);
    ImGui::TextDisabled("UVScript (text)");
    ImGui::Text("File name (without .uvs)");
    std::array<char, 65U> fileNameBuffer{};
    m_uvScriptCreationFileName.copy(fileNameBuffer.data(), fileNameBuffer.size() - 1U);
    ImGui::SetNextItemWidth(320.0F);
    if (ImGui::InputText("##uvscript-create-file-name", fileNameBuffer.data(), fileNameBuffer.size())) {
        SetUVScriptCreationFileNameUVE(fileNameBuffer.data());
    }
    if (m_uvScriptCreationFileNameEdited) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Parent Name")) {
            m_uvScriptCreationFileNameEdited = false;
            m_uvScriptCreationFileName = GetEntityDisplayLabelUVE(m_uvScriptCreationTarget);
            m_uvScriptCreationPath = GetUniqueUVScriptPathUVE(m_uvScriptCreationFileName);
            m_uvScriptCreationProblem.clear();
        }
    }
    if (!m_uvScriptCreationPath.empty()) {
        ImGui::TextDisabled("Path: %s", m_uvScriptCreationPath.c_str());
    } else if (m_uvScriptCreationProblem.empty()) {
        ImGui::TextColored(ImVec4{0.95F, 0.48F, 0.32F, 1.0F}, "Enter a valid script file name.");
    }
    if (!m_uvScriptCreationProblem.empty()) {
        ImGui::TextColored(ImVec4{0.95F, 0.48F, 0.32F, 1.0F}, "%s", m_uvScriptCreationProblem.c_str());
    }

    const bool canCreate = CanCreateUVScriptForEntityUVE(m_uvScriptCreationTarget) &&
                           !m_uvScriptCreationFileName.empty() && !m_uvScriptCreationPath.empty();
    ImGui::BeginDisabled(!canCreate);
    const bool createPressed = ImGui::Button("Create & Open");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelPressed = ImGui::Button("Cancel");
    if (createPressed && ConfirmUVScriptCreationDialogUVE()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (cancelPressed) {
        CancelUVScriptCreationDialogUVE();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorUVE::DrawHierarchyScriptAttachDialogUVE() {
    constexpr const char* kPopupId = "Attach Existing Script###hierarchy-attach-script";
    if (m_hierarchyScriptAttachDialogOpenRequested) {
        m_hierarchyScriptAttachDialogOpenRequested = false;
        ImGui::OpenPopup(kPopupId);
        m_hierarchyScriptAttachDialogWasOpened = true;
    }
    if (!m_hierarchyScriptAttachDialogWasOpened) {
        return;
    }

    bool modalOpen = true;
    if (!ImGui::BeginPopupModal(kPopupId, &modalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!modalOpen) {
            CancelHierarchyScriptAttachUVE();
        }
        return;
    }
    if (!modalOpen || !IsAuthoringCommandAllowedUVE() ||
        !CanCreateUVScriptForEntityUVE(m_hierarchyScriptAttachTarget)) {
        CancelHierarchyScriptAttachUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelHierarchyScriptAttachUVE();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextDisabled("Attach an existing project UVScript");
    ImGui::Text("Object: %s", GetEntityDisplayLabelUVE(m_hierarchyScriptAttachTarget).c_str());
    ImGui::Separator();
    ImGui::Text("Project-relative path (.uvs)");
    std::array<char, Scene::kMaximumScriptAssetPathBytesUVE + 1U> pathBuffer{};
    m_hierarchyScriptAttachPath.copy(pathBuffer.data(), pathBuffer.size() - 1U);
    ImGui::SetNextItemWidth(420.0F);
    if (ImGui::InputTextWithHint("##hierarchy-script-attach-path", "scripts/player.uvs", pathBuffer.data(),
                                 pathBuffer.size())) {
        SetHierarchyScriptAttachPathUVE(pathBuffer.data());
    }

    ImGui::Text("Known project scripts");
    std::array<char, 128U> filterBuffer{};
    m_hierarchyScriptAttachFilter.copy(filterBuffer.data(), filterBuffer.size() - 1U);
    ImGui::SetNextItemWidth(420.0F);
    if (ImGui::InputTextWithHint("##hierarchy-script-attach-filter", "Filter scripts", filterBuffer.data(),
                                 filterBuffer.size())) {
        m_hierarchyScriptAttachFilter = filterBuffer.data();
    }
    ImGui::BeginChild("##hierarchy-script-attach-candidates", ImVec2{420.0F, 132.0F}, true);
    std::size_t shown = 0U;
    for (const std::string& candidate : m_hierarchyScriptAttachCandidates) {
        const bool matches = m_hierarchyScriptAttachFilter.empty() ||
                             std::search(candidate.cbegin(), candidate.cend(),
                                         m_hierarchyScriptAttachFilter.cbegin(), m_hierarchyScriptAttachFilter.cend(),
                                         [](const char left, const char right) {
                                             return std::tolower(static_cast<unsigned char>(left)) ==
                                                    std::tolower(static_cast<unsigned char>(right));
                                         }) != candidate.cend();
        if (!matches) {
            continue;
        }
        ++shown;
        if (ImGui::Selectable(candidate.c_str(), candidate == m_hierarchyScriptAttachPath)) {
            SetHierarchyScriptAttachPathUVE(candidate);
        }
    }
    if (shown == 0U) {
        ImGui::TextDisabled(m_hierarchyScriptAttachFilter.empty() ? "No known scripts; enter a path above."
                                                                  : "No script matches.");
    }
    ImGui::EndChild();

    if (m_hierarchyScriptAttachProblem.empty()) {
        ImGui::TextDisabled("Ready to attach.");
    } else {
        ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s", m_hierarchyScriptAttachProblem.c_str());
    }
    ImGui::Separator();
    const bool canAttach = CanCreateUVScriptForEntityUVE(m_hierarchyScriptAttachTarget) &&
                           !m_hierarchyScriptAttachPath.empty() && m_hierarchyScriptAttachProblem.empty();
    ImGui::BeginDisabled(!canAttach);
    const bool attachPressed = ImGui::Button("Attach Script");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelPressed = ImGui::Button("Cancel");
    if (attachPressed && ConfirmHierarchyScriptAttachUVE()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (cancelPressed) {
        CancelHierarchyScriptAttachUVE();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorUVE::DrawHierarchyObjectUVE(const Scene::EntityUVE entity, const bool filterFlat) {
    if (!IsHierarchyEntityVisibleUVE(entity)) {
        return;
    }
    m_hierarchyShownOrder.push_back(entity);
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> children = GetHierarchyChildrenInViewOrderUVE(entity);
    // The arrow always opens and closes a row. A double-click does too only when that is the chosen
    // double-click action; otherwise it renames or focuses (below). Frame padding makes the row's
    // hit target match the author's selected height without changing horizontal alignment.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
    if (filterFlat || children.empty()) {
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

    const bool renameTarget = entity == m_hierarchyRenameEntity;
    const bool renaming = renameTarget && !m_hierarchyRenameUsesDialogUVE;
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
    // The row's right-hand columns (eye, lock, diagnostic, script) are fixed; the name gives way to
    // them. A name that would run under the leftmost used column is cut short with "..." and shown
    // in full on hover, instead of being painted over by the controls or badges.
    const std::vector<HierarchyDiagnosticUVE> diagnostics = GetObjectDiagnosticsUVE(entity);
    const std::optional<std::string> script =
        m_hierarchyView.showComponentBadges ? GetObjectScriptPathUVE(entity) : std::nullopt;
    // Keep the lock column on every row. The eye slot is also reserved globally unless hidden, so
    // toggling a row's visibility state never shifts either control or the name column.
    const float eyeColumns = m_hierarchyView.visibilityColumn == HierarchyVisibilityColumnUVE::Hidden ? 0.0F : 1.0F;
    float usedColumns = eyeColumns + 1.0F;
    if (!diagnostics.empty()) {
        usedColumns = eyeColumns + 3.0F;
    }
    if (script.has_value()) {
        usedColumns = eyeColumns + 4.0F;
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
    const float rowFramePaddingY =
        std::max(0.0F, (m_hierarchyView.rowHeight - ImGui::GetTextLineHeight()) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, rowFramePaddingY});
    // Before the row's own label, so the band sits behind the name and the bar behind the arrow and
    // icon - drawn over either, the bar would look like a clipped glyph instead of an edge marker.
    const HierarchyStructuralRowStyleUVE rootStyle =
        GetHierarchyStructuralRowStyleUVE(IsStructuralRootUVE(entity), selected);
    if (rootStyle.structuralRoot) {
        const ImVec2 bandMin = ImGui::GetCursorScreenPos();
        const ImVec2 bandMax{bandMin.x + ImGui::GetWindowWidth(),
                             bandMin.y + ImGui::GetTextLineHeight() + (2.0F * rowFramePaddingY)};
        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        if (rootStyle.bandAlpha != 0U) {
            drawList.AddRectFilled(bandMin, bandMax,
                                   IM_COL32(rootStyle.band.red, rootStyle.band.green, rootStyle.band.blue,
                                            rootStyle.bandAlpha));
        }
        drawList.AddRectFilled(bandMin, ImVec2{bandMin.x + kHierarchyStructuralRowBarWidthUVE, bandMax.y},
                               IM_COL32(rootStyle.bar.red, rootStyle.bar.green, rootStyle.bar.blue, 255));
    }
    const bool open = ImGui::TreeNodeEx(objectLabel.c_str(), flags);
    ImGui::PopStyleVar();
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
    const ImVec2 rowWindowPos = ImGui::GetWindowPos();
    m_hierarchySelectionRowBounds.push_back(HierarchySelectionRowBoundsUVE{
        entity, rowWindowPos.x, rowMin.y, rowWindowPos.x + ImGui::GetWindowWidth(), rowMax.y});
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
        const Scene::Objects::SceneObjectKindUVE kind = Scene::ResolveSceneObjectKindUVE(entityManager, entity);
        const std::uintptr_t icon = m_uiAssets.GetObjectIconTextureIdUVE(kind);
        if (icon != 0U) {
            const ImVec2 iconMin{std::floor(rowMin.x + ImGui::GetTreeNodeToLabelSpacing()),
                                 std::floor(((rowMin.y + rowMax.y) - kHierarchyObjectIconSizeUVE) * 0.5F)};
            const ImVec2 iconMax{iconMin.x + kHierarchyObjectIconSizeUVE, iconMin.y + kHierarchyObjectIconSizeUVE};
            if (m_hierarchyView.colorCodeIcons) {
                const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
                    Scene::Objects::FindSceneObjectDescriptorUVE(kind);
                const std::string_view category = descriptor == nullptr ? std::string_view{} : descriptor->category;
                const HierarchyIconAccentUVE accent = GetHierarchyIconAccentUVE(category);
                // One extra pixel at each side leaves a visible, low-alpha category accent without
                // changing the existing multicolour icon texture or growing the row vertically.
                const ImVec2 plateMin{iconMin.x - 1.0F, iconMin.y};
                const ImVec2 plateMax{iconMax.x + 1.0F, iconMax.y};
                ImGui::GetWindowDrawList()->AddRectFilled(
                    plateMin, plateMax, IM_COL32(accent.red, accent.green, accent.blue, 72), 3.0F);
            }
            ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(icon), iconMin, iconMax);
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
    if (ImGui::IsItemClicked() && !renameTarget) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift) {
            SelectHierarchyRangeUVE(entity, m_hierarchyShownOrderPrevious, io.KeyCtrl || io.KeySuper);
        } else if (io.KeyCtrl || io.KeySuper) {
            ToggleEntitySelectionUVE(entity);
        } else {
            SelectEntityUVE(entity);
        }
    }
    if (!renameTarget) {
        DrawHierarchyObjectContextMenuUVE(entity);
    }
    // F2 renames the selected row. A double-click does what the Double-Click preference says:
    // rename, focus the object in the viewport, or open and close the row (handled by the tree object).
    const bool doubleClicked = !renameTarget && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubleClicked && m_hierarchyView.doubleClick == HierarchyDoubleClickUVE::FocusInViewport &&
        CanFocusEntityInViewportUVE(entity)) {
        static_cast<void>(RequestViewportFocusUVE(entity));
    }
    const bool canRenameSelected = !renameTarget && entity == m_selectedEntity && IsAuthoringCommandAllowedUVE() &&
                                   !IsEntityLockedUVE(entity);
    if (canRenameSelected &&
        (ImGui::IsKeyPressed(ImGuiKey_F2) ||
         (doubleClicked && m_hierarchyView.doubleClick == HierarchyDoubleClickUVE::Rename))) {
        BeginHierarchyRenameUVE(entity);
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
            if (SetHierarchyEntityNameUVE(m_hierarchyRenameEntity, m_hierarchyRenameBuffer)) {
                InvalidateHierarchyFilterCacheUVE();
            }
            CancelHierarchyRenameUVE();
        }
    }
    // The Object is the document itself: it has no parent to leave, so it is never a drag source.
    if (!renameTarget && m_hierarchyView.dragToReparent && IsLifecycleCommandAllowedUVE() &&
        IsDocumentEntityUVE(entity) && !IsEntityLockedUVE(entity) && !IsStructuralRootUVE(entity) &&
        ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kHierarchyEntityPayloadUVE, &entity, sizeof(entity));
        ImGui::Text("Move %s", GetEntityDisplayLabelUVE(entity).c_str());
        ImGui::EndDragDropSource();
    }
    AcceptHierarchyDropTargetUVE(entity);
    if (!renaming) {
        DrawHierarchyRowBadgesUVE(diagnostics, script, eyeColumns);
        DrawHierarchyLockToggleUVE(entity, rowHovered);
        DrawHierarchyVisibilityToggleUVE(entity, rowHovered);
    }
    if (open) {
        if (!filterFlat) {
            for (const Scene::EntityUVE child : children) {
                DrawHierarchyObjectUVE(child);
            }
        }
        ImGui::TreePop();
    }
}

void EditorUVE::DrawHierarchyObjectContextMenuUVE(const Scene::EntityUVE entity) {
    // Right-click selects an unselected row first unless it is locked. Rename can still target this
    // row directly when it is already selected as part of a children-follow group.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsEntitySelectedUVE(entity)) {
        SelectEntityUVE(entity);
    }
    if (!ImGui::BeginPopupContextItem("##hierarchy-object-context")) {
        return;
    }
    const bool authoring = IsAuthoringCommandAllowedUVE();
    const bool lifecycle = IsLifecycleCommandAllowedUVE();
    const bool single = HasSingleDocumentSelectionUVE() && entity == m_selectedEntity;
    const bool rootObject = IsStructuralRootUVE(entity);
    const bool locked = IsEntityLockedUVE(entity);
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();

    ImGui::TextDisabled("%s", GetEntityDisplayLabelUVE(entity).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(!authoring || !single);
    // Opens the same searchable picker as the + button. New objects go under the single selection,
    // which the right-click has just made this row.
    if (ImGui::MenuItem("Add Child Object...")) {
        m_objectPickerOpenRequested = true;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!authoring || IsEntityLockedUVE(entity));
    if (ImGui::MenuItem("Rename", "F2")) {
        BeginHierarchyRenameUVE(entity);
    }
    ImGui::EndDisabled();
    {
        // Recomputed every frame the menu is open, so the item enables exactly when a legal
        // target exists; the picker itself re-validates when it draws.
        const std::vector<Scene::Objects::SceneObjectKindUVE> changeTargets =
            (authoring && single && !rootObject && !locked) ? GetSceneObjectKindChangeTargetsUVE(entity)
                                                            : std::vector<Scene::Objects::SceneObjectKindUVE>{};
        ImGui::BeginDisabled(changeTargets.empty());
        if (ImGui::MenuItem("Change Type...")) {
            m_changeTypePickerEntity = entity;
            m_changeTypePickerFilter.clear();
            m_changeTypePickerOpenRequested = true;
        }
        ImGui::EndDisabled();
    }
    {
        // Enabled exactly when the row resolves to a kind that owns a defaults setting; the label
        // names the kind so the modal's target is never a surprise.
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
            Scene::Objects::FindSceneObjectDescriptorUVE(
                Scene::ResolveSceneObjectKindUVE(entityManager, entity));
        const bool savable = authoring && single && !rootObject && !locked && descriptor != nullptr &&
                             descriptor->libraryCreatable;
        const std::string label =
            descriptor != nullptr ? "Save as " + std::string(descriptor->displayName) + " Default"
                                  : "Save as Default";
        ImGui::BeginDisabled(!savable);
        if (ImGui::MenuItem(label.c_str())) {
            static_cast<void>(RequestHierarchySaveDefaultUVE(entity));
        }
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    // Cut, copy and paste act on the selection, which the right-click has just made this row; the
    // shortcut text and the enabled state come from the one command registry, so a rebind shows here.
    DrawCommandMenuItemUVE("edit.cut");
    DrawCommandMenuItemUVE("edit.copy");
    DrawCommandMenuItemUVE("edit.paste");
    const bool hasScriptComponent = entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity);
    const bool isFolder = entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity);
    if (!isFolder) {
        std::string scriptPath;
        if (hasScriptComponent) {
            scriptPath = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        }
        const bool hasAttachedScript = !scriptPath.empty();
        const bool canAssignScript = CanCreateUVScriptForEntityUVE(entity);
        if (hasAttachedScript) {
            const std::string scriptProblem = DescribeScriptAssetProblemUVE(scriptPath);
            const bool wouldDiscardUnsavedText = m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE() &&
                                                 m_openUVScript->path != scriptPath;
            const bool canOpenScript = authoring && !locked && scriptProblem.empty() && !wouldDiscardUnsavedText;
            if (ImGui::MenuItem("Open Script", nullptr, false, canOpenScript)) {
                static_cast<void>(OpenScriptGraphForEntityUVE(entity));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                const char* tooltip = "Open this object's script in Scripting.";
                if (!authoring) {
                    tooltip = "Scripts can only be opened while editing.";
                } else if (locked) {
                    tooltip = "Unlock this object before opening its script.";
                } else if (wouldDiscardUnsavedText) {
                    tooltip = "Save or close the other script before switching.";
                } else if (!scriptProblem.empty()) {
                    tooltip = scriptProblem.c_str();
                }
                ImGui::SetTooltip("%s", tooltip);
            }
            if (ImGui::MenuItem("Detach Script", nullptr, false, authoring && !locked)) {
                static_cast<void>(AssignScriptToEntityUVE(entity, {}));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Undo restores the attachment; the script file is left in the project.");
            }
        } else {
            if (ImGui::MenuItem("Attach Existing Script...", nullptr, false, canAssignScript)) {
                static_cast<void>(OpenHierarchyScriptAttachDialogUVE(entity));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                const char* const reason = !authoring ? "Scripts can only be attached while editing."
                                           : locked ? "Unlock this object before attaching a script."
                                                    : "Choose an existing project-relative .uvs file.";
                ImGui::SetTooltip("%s", reason);
            }
            if (ImGui::MenuItem("Create Script...", nullptr, false, canAssignScript)) {
                static_cast<void>(OpenUVScriptCreationDialogUVE(entity));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                const char* const reason = !authoring ? "Scripts can only be created while editing."
                                           : locked ? "Unlock this object before creating its script."
                                                    : "Creates a script for this hierarchy object.";
                ImGui::SetTooltip("%s", reason);
            }
        }
    }
    if (ImGui::MenuItem(locked ? "Unlock Object" : "Lock Object", nullptr, locked, authoring)) {
        static_cast<void>(SetEntityLockedUVE(entity, !locked));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(locked ? "This object cannot be selected. The lock lasts for the current editor session."
                                 : "Prevent this object from being selected for the current editor session.");
    }
    // The way back for everything a session of clicking lock columns left behind. Selecting is not
    // possible on a locked row, so this cannot be reached through the selection at all; it is
    // offered only while something is locked, and then on any row, locked or not.
    if (HasLockedEntitiesUVE()) {
        DrawCommandMenuItemUVE("edit.unlockAll");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Unlocks every locked object in this editor session (%zu now).",
                              GetLockedEntityCountUVE());
        }
    }
    ImGui::Separator();
    const bool focusable = CanFocusEntityInViewportUVE(entity);
    if (ImGui::MenuItem("Focus in Viewport", "F", false, focusable)) {
        static_cast<void>(RequestViewportFocusUVE(entity));
    }
    if (!focusable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This object has no position in the scene to look at.");
    }
    // Framing takes the whole selection, so it sits on a row like Duplicate and Delete do; the two
    // alignments are about this row's own axes, like the focus above. The toolbar-size forms of the
    // same three live in the File menu and the command palette.
    if (ImGui::MenuItem("Frame Selection", nullptr, false, CanFrameSelectionInViewportUVE())) {
        static_cast<void>(RequestViewportFrameSelectionUVE());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Move the pivot to the selection's bounds and pull back until they fit.");
    }
    const bool canAlignView = CanAlignViewToEntityUVE(entity);
    if (ImGui::MenuItem("Align View to Node", nullptr, false, canAlignView)) {
        static_cast<void>(RequestViewportAlignViewToEntityUVE(entity));
    }
    if (!canAlignView && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This object has no rotation to look along.");
    }
    const bool canAlignObject = CanAlignSelectedEntityToViewUVE();
    if (ImGui::MenuItem("Align Node to View", nullptr, false, canAlignObject)) {
        static_cast<void>(AlignSelectedEntityToViewUVE());
    }
    if (!canAlignObject && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select one object to turn to the way the camera is looking.");
    }
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
    // Place among its siblings; locked rows are not selected, so these commands must stay disabled.
    ImGui::BeginDisabled(!authoring || !single);
    DrawCommandMenuItemUVE("edit.moveUp");
    DrawCommandMenuItemUVE("edit.moveDown");
    DrawCommandMenuItemUVE("edit.moveToTop");
    DrawCommandMenuItemUVE("edit.moveToBottom");
    ImGui::EndDisabled();
    ImGui::Separator();
    {
        // Like the change-type targets above: recomputed every frame the menu is open, so the
        // item enables exactly when a legal parent exists; the picker re-validates when it draws.
        const std::vector<Scene::EntityUVE> parents =
            (lifecycle && single && !rootObject && !locked) ? GetEligibleReparentParentsUVE(entity)
                                                            : std::vector<Scene::EntityUVE>{};
        ImGui::BeginDisabled(parents.empty());
        if (ImGui::MenuItem("Reparent To...")) {
            m_reparentPickerEntity = entity;
            m_reparentPickerFilter.clear();
            m_reparentPickerOpenRequested = true;
        }
        ImGui::EndDisabled();
    }
    {
        // Session toggle, deliberately unpersisted: a transform-rewriting default must stay an
        // explicit per-session opt-in, never a surprise carried over from last week.
        bool keepWorld = GetReparentTransformModeUVE() == EditorReparentTransformModeUVE::KeepWorld;
        if (ImGui::MenuItem("Keep Global Transform", nullptr, &keepWorld, IsReparentModeChangeAllowedUVE())) {
            static_cast<void>(SetReparentTransformModeUVE(keepWorld ? EditorReparentTransformModeUVE::KeepWorld
                                                                    : EditorReparentTransformModeUVE::KeepLocal));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("On, a moved object stays visually put - its local transform is recomputed, "
                              "and the move is refused when the new parent would shear it. Off, the local "
                              "transform is kept and the object jumps with its new parent. Applies to drags "
                              "and Reparent To... alike.");
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!lifecycle || !single || rootObject);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        static_cast<void>(DuplicateSelectedEntityUVE());
    }
    if (ImGui::MenuItem("Delete", "Del")) {
        // Through the confirmation request: a branch asks first when the preference is on.
        static_cast<void>(RequestHierarchyDeleteUVE());
    }
    ImGui::EndDisabled();
    if (rootObject && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("The Object root holds the whole scene and cannot be duplicated or deleted.");
    }
    ImGui::Separator();
    // Read-only and row-targeted: a locked row is not selected, but its path and its handle are
    // still worth copying out.
    const bool copyable = IsDocumentEntityUVE(entity);
    if (ImGui::MenuItem("Copy Node Path", nullptr, false, copyable)) {
        static_cast<void>(CopyEntityNodePathUVE(entity));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("The Object root's name down to this row, slash-separated.");
    }
    if (ImGui::MenuItem("Copy Node Identifier", nullptr, false, copyable)) {
        static_cast<void>(CopyEntityIdentifierUVE(entity));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This object's index:generation handle, meaningful for this session only.");
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


void EditorUVE::DrawObjectPickerUVE() {
    // The Scene panel's "+" is small on purpose: the level's own furniture - a folder to organise it,
    // the sun and the sky. Everything else is made in the Content Browser ("+ Add" or right-click)
    // and dragged into the level, so the world is built from assets instead of loose objects.
    constexpr const char* kPopupId = "##object-picker";
    if (m_objectPickerOpenRequested) {
        m_objectPickerOpenRequested = false;
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
    }
    ImGui::Separator();
    const std::string folderLabel =
        levelLayout ? "New Folder in " + GetEntityDisplayLabelUVE(ResolveNewObjectParentForUVE(Scene::Objects::SceneObjectKindUVE::Folder))
                    : std::string{"New Folder"};
    const auto item = [this](const Scene::Objects::SceneObjectKindUVE kind, const char* const label,
                             const char* const shortcut, const char* const tooltip) {
        DrawObjectPickerIconUVE(m_uiAssets.GetObjectIconTextureIdUVE(kind));
        const bool picked = ImGui::MenuItem(label, shortcut);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return picked;
    };
    if (item(Scene::Objects::SceneObjectKindUVE::Folder, folderLabel.c_str(), nullptr,
             "Groups objects in this panel. It has no position, so nothing moves in the world.")) {
        static_cast<void>(CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::Folder));
    }
    ImGui::Separator();
    // The level has one of each; once it is there, it is no longer offered.
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
    ImGui::Separator();
    ImGui::TextDisabled("Meshes, characters and the rest:");
    ImGui::TextDisabled("Content Browser > + Add or right-click.");
    ImGui::EndPopup();
}

void EditorUVE::DrawChangeTypePickerUVE() {
    constexpr const char* kPopupId = "##change-type-picker";
    if (m_changeTypePickerOpenRequested) {
        m_changeTypePickerOpenRequested = false;
        ImGui::OpenPopup(kPopupId);
    }
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }
    if (!IsDocumentEntityUVE(m_changeTypePickerEntity)) {
        ImGui::TextDisabled("That object is gone.");
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("Change %s into", GetEntityDisplayLabelUVE(m_changeTypePickerEntity).c_str());
    ImGui::Separator();
    std::array<char, 256> filterBuffer{};
    m_changeTypePickerFilter.copy(filterBuffer.data(), filterBuffer.size() - 1U);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##change-type-filter", "Filter types", filterBuffer.data(),
                                 filterBuffer.size())) {
        m_changeTypePickerFilter = filterBuffer.data();
    }
    ImGui::Separator();
    const auto matches = [](const std::string_view label, const std::string_view filter) {
        return std::search(label.begin(), label.end(), filter.begin(), filter.end(),
                           [](const char left, const char right) {
                               return std::tolower(static_cast<unsigned char>(left)) ==
                                      std::tolower(static_cast<unsigned char>(right));
                           }) != label.end();
    };
    // Re-validated every frame, so an undo behind the open popup cannot offer a stale list.
    const std::vector<Scene::Objects::SceneObjectKindUVE> targets =
        GetSceneObjectKindChangeTargetsUVE(m_changeTypePickerEntity);
    bool anyShown = false;
    for (const Scene::Objects::SceneObjectKindUVE kind : targets) {
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
            Scene::Objects::FindSceneObjectDescriptorUVE(kind);
        const std::string label =
            descriptor != nullptr ? std::string{descriptor->displayName} : std::string{"Object"};
        if (!m_changeTypePickerFilter.empty() && !matches(label, m_changeTypePickerFilter)) {
            continue;
        }
        DrawObjectPickerIconUVE(m_uiAssets.GetObjectIconTextureIdUVE(kind));
        if (ImGui::MenuItem(label.c_str())) {
            static_cast<void>(ChangeDocumentSceneObjectKindUVE(m_changeTypePickerEntity, kind));
            ImGui::CloseCurrentPopup();
        }
        anyShown = true;
    }
    if (!anyShown) {
        ImGui::TextDisabled("%s", targets.empty() ? "Nothing it can become right now." : "No type matches.");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Shared components keep their values.");
    ImGui::EndPopup();
}

void EditorUVE::DrawReparentPickerUVE() {
    constexpr const char* kPopupId = "##reparent-picker";
    if (m_reparentPickerOpenRequested) {
        m_reparentPickerOpenRequested = false;
        ImGui::OpenPopup(kPopupId);
    }
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }
    if (!IsDocumentEntityUVE(m_reparentPickerEntity)) {
        ImGui::TextDisabled("That object is gone.");
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("Move %s into", GetEntityDisplayLabelUVE(m_reparentPickerEntity).c_str());
    ImGui::Separator();
    std::array<char, 256> filterBuffer{};
    m_reparentPickerFilter.copy(filterBuffer.data(), filterBuffer.size() - 1U);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##reparent-filter", "Filter parents", filterBuffer.data(),
                                 filterBuffer.size())) {
        m_reparentPickerFilter = filterBuffer.data();
    }
    ImGui::Separator();
    const auto matches = [](const std::string_view label, const std::string_view filter) {
        return std::search(label.begin(), label.end(), filter.begin(), filter.end(),
                           [](const char left, const char right) {
                               return std::tolower(static_cast<unsigned char>(left)) ==
                                      std::tolower(static_cast<unsigned char>(right));
                           }) != label.end();
    };
    // Re-validated every frame, so an undo behind the open popup cannot offer a stale list. The
    // request carries the large-subtree confirmation, so a huge move still asks first.
    const std::vector<Scene::EntityUVE> parents = GetEligibleReparentParentsUVE(m_reparentPickerEntity);
    bool anyShown = false;
    for (const Scene::EntityUVE parent : parents) {
        const std::string label = GetHierarchyCandidateLabelUVE(parent);
        if (!m_reparentPickerFilter.empty() && !matches(label, m_reparentPickerFilter)) {
            continue;
        }
        if (ImGui::MenuItem(label.c_str())) {
            static_cast<void>(RequestHierarchyReparentUVE(m_reparentPickerEntity, parent));
            ImGui::CloseCurrentPopup();
        }
        anyShown = true;
    }
    if (!anyShown) {
        ImGui::TextDisabled("%s", parents.empty() ? "Nowhere it can go right now." : "No parent matches.");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Keep Global Transform decides whether it jumps or stays put.");
    ImGui::EndPopup();
}

bool EditorUVE::IsEntityLockedUVE(const Scene::EntityUVE entity) const noexcept {
    return IsDocumentEntityUVE(entity) && m_lockedHierarchyEntities.contains(entity);
}

bool EditorUVE::SetEntityLockedUVE(const Scene::EntityUVE entity, const bool locked) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity)) {
        return false;
    }
    const bool wasLocked = m_lockedHierarchyEntities.contains(entity);
    if (wasLocked == locked) {
        return false;
    }
    if (locked) {
        m_lockedHierarchyEntities.insert(entity);
        PruneSelectionUVE();
    } else {
        m_lockedHierarchyEntities.erase(entity);
    }
    return true;
}

bool EditorUVE::HasLockedEntitiesUVE() const noexcept {
    return !m_lockedHierarchyEntities.empty();
}

std::size_t EditorUVE::GetLockedEntityCountUVE() const noexcept {
    return m_lockedHierarchyEntities.size();
}

bool EditorUVE::CanLockSelectionUVE() const noexcept {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    return std::any_of(m_selectedEntities.begin(), m_selectedEntities.end(), [this](const Scene::EntityUVE entity) {
        return IsDocumentEntityUVE(entity) && !m_lockedHierarchyEntities.contains(entity);
    });
}

bool EditorUVE::LockSelectionUVE() {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    bool changed = false;
    // A copy: locking a row prunes it from the selection being walked.
    for (const Scene::EntityUVE entity : std::vector<Scene::EntityUVE>(m_selectedEntities)) {
        if (IsDocumentEntityUVE(entity) && m_lockedHierarchyEntities.insert(entity).second) {
            changed = true;
        }
    }
    if (changed) {
        PruneSelectionUVE();
    }
    return changed;
}

bool EditorUVE::UnlockAllEntitiesUVE() {
    if (!IsAuthoringCommandAllowedUVE() || m_lockedHierarchyEntities.empty()) {
        return false;
    }
    m_lockedHierarchyEntities.clear();
    return true;
}

void EditorUVE::DrawHierarchyLockToggleUVE(const Scene::EntityUVE entity, const bool rowHovered) {
    const bool locked = IsEntityLockedUVE(entity);
    const float size = ImGui::GetFrameHeight();
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rightEdge = ImGui::GetWindowContentRegionMax().x;
    const float eyeColumns = m_hierarchyView.visibilityColumn == HierarchyVisibilityColumnUVE::Hidden ? 0.0F : 1.0F;
    // The lock sits immediately left of the eye; when eyes are hidden it takes the rightmost column.
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightEdge - (size * (eyeColumns + 1.0F))));
    ImGui::PushID("##hierarchy-lock-column");
    const bool clicked = ImGui::InvisibleButton("##lock", ImVec2{size, lineHeight});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (ShouldDrawHierarchyLockGlyphUVE(locked, rowHovered)) {
        const ImU32 color = locked ? IM_COL32(224, 174, 82, 255)
                                   : ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        DrawLockGlyphUVE(*ImGui::GetWindowDrawList(),
                         ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F}, lineHeight, locked, color);
    }
    if (hovered) {
        ImGui::SetTooltip(locked ? "Locked - click to unlock" : "Unlocked - click to lock");
    }
    if (clicked) {
        static_cast<void>(SetEntityLockedUVE(entity, !locked));
    }
}

void EditorUVE::DrawHierarchyVisibilityToggleUVE(const Scene::EntityUVE entity, const bool rowHovered) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Only objects that can be hidden get an eye; the Object and plain Objects have no Visibility.
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

namespace {

/// Whether a path-valued component reference still names a file the project has. Scripts, model
/// sources, sky textures, levels and decal or fog materials are all stored as strings and opened
/// by path rather than by GUID, so "is it broken?" is a question about the project's files, not
/// about the asset registry.
///
/// Two of the three rules a reference may resolve by live here; the third, the project's content
/// tree, is the caller's to fetch because only the editor knows whether it holds a current one:
///   1. the virtual file system - the mount runtime script and asset reads go through;
///   2. a real file at the path itself - what the scene serializer, the texture loader and the
///      decal bridge open directly;
///   3. the project's content tree - a model source and a decal or fog material are authored
///      relative to the content root (that is how the Content Browser names them), so the entry
///      the browser lists IS the file.
///
/// An empty path is "no asset", which each component already states in its own message, and is
/// never a broken reference.
[[nodiscard]] bool ProjectPathResolvesAsFileUVE(const Asset::IFileSystemUVE& fileSystem,
                                                const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (fileSystem.HasFileUVE(path)) {
        return true;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::path{path}, error);
}

/// Rule three: is `path` - named relative to the content root, as the Content Browser names it -
/// one of the files the project's content tree lists?
[[nodiscard]] bool ProjectContentTreeListsPathUVE(const Asset::ProjectFileSnapshotUVE& contentTree,
                                                  const std::string& path) {
    const std::filesystem::path wanted = std::filesystem::path{path}.lexically_normal();
    return std::any_of(contentTree.entries.cbegin(), contentTree.entries.cend(),
                       [&wanted](const Asset::ProjectFileEntryUVE& entry) {
                           return entry.kind == Asset::ProjectFileEntryKindUVE::File &&
                                  entry.relativePath == wanted;
                       });
}

} // namespace

std::vector<HierarchyDiagnosticUVE> EditorUVE::GetObjectDiagnosticsUVE(const Scene::EntityUVE entity) const {
    std::vector<HierarchyDiagnosticUVE> diagnostics;
    if (!IsDocumentEntityUVE(entity)) {
        return diagnostics;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const Asset::IAssetDatabaseUVE& assets = m_services->GetAssetDatabaseUVE();
    const Asset::IFileSystemUVE& fileSystem = m_services->GetFileSystemUVE();
    // The content tree is a copy, so it is taken at most once per call, and only for a reference
    // that resolved as neither a mounted nor a real file. A row with no path-valued reference on
    // it - the great majority - never asks for it at all, and no reference at all is judged while
    // the editor holds no current tree: an index it never refreshed, or whose last refresh failed,
    // says nothing, so the badge waits for the editor to have read the project rather than
    // guessing about one.
    std::optional<Asset::ProjectFileSnapshotUVE> contentTree;
    const auto pathIsMissing = [&](const std::string& path) {
        if (path.empty() || ProjectPathResolvesAsFileUVE(fileSystem, path)) {
            return false;
        }
        if (!m_projectFileSnapshotInitialized || !m_projectFileLastRefreshSucceeded) {
            return false;
        }
        if (!contentTree.has_value()) {
            contentTree = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        }
        return !ProjectContentTreeListsPathUVE(*contentTree, path);
    };
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !IsTransformFiniteUVE(entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{
            HierarchyDiagnosticSeverityUVE::Error,
            "The transform holds a value that is not a number or is infinite."});
    }
    if (entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity) &&
        !Scene::IsPrimitiveMeshComponentValidUVE(
            entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The primitive mesh settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity) &&
        !Scene::IsColliderComponentValidUVE(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The collider settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(entity) &&
        !Scene::IsRigid3DComponentValidUVE(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The rigid-body settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity) &&
        !Scene::IsCameraComponentValidUVE(entityManager.GetComponentUVE<Scene::CameraComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The camera projection settings are invalid."});
    }
    if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(entity) &&
        !Scene::IsLightComponentValidUVE(entityManager.GetComponentUVE<Scene::LightComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The light settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity) &&
        !Scene::IsAudioSourceComponentValidUVE(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The audio source settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity) &&
        !Scene::IsWorldEnvironment3DObjectComponentValidUVE(
            entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity))) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The world environment settings contain an invalid value."});
    }
    if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
        if (!path.empty() && !Scene::IsScriptAssetPathValidUVE(path)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The script path is not a valid project path."});
        } else if (pathIsMissing(path)) {
            // Valid but gone: the runtime reads the script through the project mount, so a file
            // that is not there means the object silently keeps its old behaviour.
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The attached script is no longer in the project."});
        }
    }
    if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        const Scene::MeshComponentUVE& mesh = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
        if (mesh.meshGuid == Asset::kInvalidAssetGuidUVE) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Warning,
                                                         "No mesh is assigned, so nothing is drawn."});
        } else if (!assets.HasGuidUVE(mesh.meshGuid)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The assigned mesh is no longer in the project."});
        }
        if (mesh.materialGuid != Asset::kInvalidAssetGuidUVE && !assets.HasGuidUVE(mesh.materialGuid)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The assigned material is no longer in the project."});
        }
    }
    if (entityManager.HasComponentUVE<Scene::LodGroup3DComponentUVE>(entity)) {
        const Scene::LodGroup3DComponentUVE& lod = entityManager.GetComponentUVE<Scene::LodGroup3DComponentUVE>(entity);
        const std::size_t checkedLevels = std::min<std::size_t>(lod.levelCount, lod.lodMeshGuids.size());
        for (std::size_t index = 0U; index < checkedLevels; ++index) {
            const Asset::AssetGuidUVE meshGuid = lod.lodMeshGuids[index];
            if (meshGuid != Asset::kInvalidAssetGuidUVE && !assets.HasGuidUVE(meshGuid)) {
                diagnostics.push_back(HierarchyDiagnosticUVE{
                    HierarchyDiagnosticSeverityUVE::Error,
                    "LOD level " + std::to_string(index + 1U) + " references a mesh no longer in the project."});
            }
        }
    }
    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(entity)) {
        const std::string& source =
            entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(entity).skeletonAssetPath;
        if (source.empty()) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Warning,
                                                         "No source model is set, so the skeleton has no bones."});
        } else if (pathIsMissing(source)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The source model is no longer in the project."});
        }
    }
    // The remaining path-valued references, each judged by the same rule: what the runtime opens,
    // and what the project's content tree lists. They are grouped here rather than beside each
    // component's whole-value check because they answer one question - is the file still there.
    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity) &&
        pathIsMissing(entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(entity).skyAssetPath)) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The sky texture is no longer in the project."});
    }
    if (entityManager.HasComponentUVE<Scene::Decal3DComponentUVE>(entity) &&
        pathIsMissing(entityManager.GetComponentUVE<Scene::Decal3DComponentUVE>(entity).materialAssetPath)) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The decal material is no longer in the project."});
    }
    if (entityManager.HasComponentUVE<Scene::FogVolume3DComponentUVE>(entity) &&
        pathIsMissing(entityManager.GetComponentUVE<Scene::FogVolume3DComponentUVE>(entity).materialAssetPath)) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The fog material is no longer in the project."});
    }
    if (entityManager.HasComponentUVE<Scene::LevelStreamer3DComponentUVE>(entity) &&
        pathIsMissing(entityManager.GetComponentUVE<Scene::LevelStreamer3DComponentUVE>(entity).levelPath)) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The level file is no longer in the project."});
    }
    // The baked navigation mesh a region loads. Nothing consumes it at runtime yet - the bake that
    // will read it is still to come - but the path is authored against the content root the same
    // way a level is, so an author who moved or deleted the mesh should hear about it from the row
    // rather than from a bake that quietly re-rasterizes instead of loading what was authored.
    if (entityManager.HasComponentUVE<Scene::NavMeshVolume3DComponentUVE>(entity) &&
        pathIsMissing(
            entityManager.GetComponentUVE<Scene::NavMeshVolume3DComponentUVE>(entity).navigationMeshAssetPath)) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The navigation mesh is no longer in the project."});
    }

    // A drawable's material override or overlay is authored the same way a decal or fog material is,
    // and takes effect the next time the mesh draws; a path the project no longer holds means the
    // surface silently keeps its own material instead of the one the author named.
    if (entityManager.HasComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity)) {
        const Scene::SurfaceInstanceComponentUVE& surface =
            entityManager.GetComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity);
        if (pathIsMissing(surface.materialOverridePath)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The material override is no longer in the project."});
        }
        if (pathIsMissing(surface.materialOverlayPath)) {
            diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                         "The material overlay is no longer in the project."});
        }
    }

    // Most specialized scene components register their existing whole-value rule here. Reuse that
    // metadata so new validated node types automatically receive the same hierarchy error badge;
    // the explicit checks above remain for components whose editor path also resolves external assets.
    std::vector<std::string> invalidComponentNames;
    for (const std::type_index componentType : entityManager.GetComponentTypesUVE(entity)) {
        const Core::TypeMetadataEntryUVE* const metadata = Scene::FindSceneComponentMetadataUVE(componentType);
        if (metadata == nullptr || metadata->isInstanceValid == nullptr) {
            continue;
        }
        const void* const instance = entityManager.GetComponentPointerUVE(entity, componentType);
        if (instance != nullptr && !metadata->isInstanceValid(instance)) {
            invalidComponentNames.push_back(metadata->displayName);
        }
    }
    std::sort(invalidComponentNames.begin(), invalidComponentNames.end());
    for (const std::string& componentName : invalidComponentNames) {
        diagnostics.push_back(HierarchyDiagnosticUVE{HierarchyDiagnosticSeverityUVE::Error,
                                                     "The " + componentName + " component has invalid settings."});
    }
    return diagnostics;
}

std::optional<std::string> EditorUVE::GetObjectScriptPathUVE(const Scene::EntityUVE entity) const {
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return std::nullopt;
    }
    const std::string& path = entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
    return path.empty() ? std::nullopt : std::optional<std::string>{path};
}

void EditorUVE::DrawHierarchyRowBadgesUVE(const std::vector<HierarchyDiagnosticUVE>& diagnostics,
                                          const std::optional<std::string>& script, const float eyeColumns) {
    // Fixed columns left of the lock and eye controls: diagnostic nearest, then script. With the eyes
    // turned off (`eyeColumns` 0) they shift right without overlapping the lock column.
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
        badge("##script", eyeColumns + 3.0F,
              [&](const ImVec2 center, const float glyphSize) {
                  DrawScriptGlyphUVE(drawList, center, glyphSize, IM_COL32(120, 170, 245, 255));
              },
              [&] {
                  ImGui::TextDisabled("Script");
                  ImGui::TextUnformatted(script->c_str());
              });
    }
    if (!diagnostics.empty()) {
        const bool hasErrors = HasHierarchyErrorDiagnosticsUVE(diagnostics);
        const std::size_t errorCount = static_cast<std::size_t>(std::count_if(
            diagnostics.begin(), diagnostics.end(), [](const HierarchyDiagnosticUVE& diagnostic) {
                return diagnostic.severity == HierarchyDiagnosticSeverityUVE::Error;
            }));
        const std::size_t warningCount = diagnostics.size() - errorCount;
        badge("##diagnostic", eyeColumns + 2.0F,
              [&](const ImVec2 center, const float glyphSize) {
                  if (hasErrors) {
                      DrawErrorGlyphUVE(drawList, center, glyphSize);
                  } else {
                      DrawWarningGlyphUVE(drawList, center, glyphSize);
                  }
              },
              [&] {
                  if (errorCount > 0U && warningCount > 0U) {
                      ImGui::TextDisabled("%zu error%s, %zu warning%s", errorCount,
                                          errorCount == 1U ? "" : "s", warningCount,
                                          warningCount == 1U ? "" : "s");
                  } else if (errorCount > 0U) {
                      if (errorCount == 1U) {
                          ImGui::TextDisabled("1 error");
                      } else {
                          ImGui::TextDisabled("%zu errors", errorCount);
                      }
                  } else if (warningCount == 1U) {
                      ImGui::TextDisabled("1 warning");
                  } else {
                      ImGui::TextDisabled("%zu warnings", warningCount);
                  }
                  for (const HierarchyDiagnosticUVE& diagnostic : diagnostics) {
                      const char* const severity = diagnostic.severity == HierarchyDiagnosticSeverityUVE::Error
                                                       ? "Error"
                                                       : "Warning";
                      ImGui::BulletText("%s: %s", severity, diagnostic.message.c_str());
                  }
              });
    }
    ImGui::PopID();
}

} // namespace UVE::Editor
