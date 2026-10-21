// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The 3D viewport panel: the rendered image, the gizmo-mode overlay toolbar drawn on top of it,
// and the status bubbles along its edge.
//
// Split out of editor_uve.cpp as the fifth panel. The gizmo icons come with it rather than
// joining the shared icon header - they are used by nothing else, and the rule this sequence has
// followed throughout is that only helpers with callers on both sides of a split become shared.
// Putting single-caller code in a shared header would make the header the new dumping ground the
// split was meant to eliminate.
//
// Moved verbatim. Not one line of the three functions differs from what was in editor_uve.cpp.

#include "uve/editor/editor_uve.h"
#include "uve/math/scalar_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "editor_chrome_layout_uve.h"
#include "editor_fonts_uve.h"
#include "editor_object_icons_uve.h"

#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/editor/editor_settings_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Editor {
namespace {

// ---- Viewport overlay toolbar - procedurally-drawn gizmo-mode icons -------------------------
// Same "invisible hit-area button + custom ImDrawList paint" technique as DrawMenuBarUVE()'s own
// playback buttons (AddTriangleFilled/AddRectFilled for Play/Pause/Stop) - kept vector-drawn
// rather than adding new bitmap/SVG icon assets, both for consistency with that existing
// precedent and because it needs no asset-pipeline regeneration.
constexpr float kViewportBubbleIconRadiusUVE = 10.0F;

void DrawRotateIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float arcRadius = radius * 0.58F;
    constexpr float kStartAngle = -0.35F * Math::kPiUVE;
    constexpr float kEndAngle = 1.15F * Math::kPiUVE;
    drawList.PathArcTo(center, arcRadius, kStartAngle, kEndAngle, 24);
    drawList.PathStroke(color, ImDrawFlags_None, 1.5F);
    const ImVec2 tip{center.x + std::cos(kEndAngle) * arcRadius, center.y + std::sin(kEndAngle) * arcRadius};
    const ImVec2 tangent{-std::sin(kEndAngle), std::cos(kEndAngle)};
    const float headSize = radius * 0.28F;
    const ImVec2 outward{std::cos(kEndAngle), std::sin(kEndAngle)};
    const ImVec2 baseA{tip.x - tangent.x * headSize + outward.x * headSize * 0.5F,
                       tip.y - tangent.y * headSize + outward.y * headSize * 0.5F};
    const ImVec2 baseB{tip.x + tangent.x * headSize + outward.x * headSize * 0.5F,
                       tip.y + tangent.y * headSize + outward.y * headSize * 0.5F};
    drawList.AddTriangleFilled(tip, baseA, baseB, color);
}

void DrawScaleIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.42F;
    const ImVec2 topLeft{center.x - half, center.y - half};
    const ImVec2 bottomRight{center.x + half, center.y + half};
    drawList.AddRect(topLeft, bottomRight, color, 0.0F, 0, 1.4F);
    const float handle = radius * 0.20F;
    const std::array<ImVec2, 4> corners{topLeft, ImVec2{bottomRight.x, topLeft.y}, bottomRight,
                                        ImVec2{topLeft.x, bottomRight.y}};
    for (const ImVec2& corner : corners) {
        drawList.AddRectFilled(ImVec2{corner.x - handle * 0.5F, corner.y - handle * 0.5F},
                               ImVec2{corner.x + handle * 0.5F, corner.y + handle * 0.5F}, color);
    }
}

void DrawUniversalIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircle(center, radius * 0.62F, color, 20, 1.3F);
    const float half = radius * 0.22F;
    drawList.AddRectFilled(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half},
                           color);
}

void DrawGridIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.5F;
    const float third = half * 2.0F / 3.0F;
    drawList.AddRect(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half}, color,
                     0.0F, 0, 1.2F);
    for (int index = 1; index < 3; ++index) {
        const float offset = -half + third * static_cast<float>(index);
        drawList.AddLine(ImVec2{center.x - half, center.y + offset}, ImVec2{center.x + half, center.y + offset},
                         color, 1.0F);
        drawList.AddLine(ImVec2{center.x + offset, center.y - half}, ImVec2{center.x + offset, center.y + half},
                         color, 1.0F);
    }
}

// Draws one circular toggle button (filled background, blue-highlighted when `active`) at the
// cursor's current screen position and advances the cursor past it via ImGui::SameLine() - `drawIcon`
// paints whatever glyph belongs on top, already centered and radius-scaled.
template <typename DrawIconUVE>
[[nodiscard]] bool DrawViewportBubbleIconButtonUVE(const char* const id, const bool active,
                                                   DrawIconUVE&& drawIcon) {
    ImGui::PushID(id);
    const float diameter = kViewportBubbleIconRadiusUVE * 2.0F;
    const bool pressed = ImGui::InvisibleButton("##bubble-icon", ImVec2{diameter, diameter});
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImVec2 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F};
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImU32 backgroundColor = active ? IM_COL32(64, 132, 214, 235)
                                         : (hovered ? IM_COL32(255, 255, 255, 28) : IM_COL32(255, 255, 255, 10));
    drawList.AddCircleFilled(center, kViewportBubbleIconRadiusUVE, backgroundColor, 20);
    const ImU32 iconColor = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(214, 220, 230, 220);
    drawIcon(drawList, center, kViewportBubbleIconRadiusUVE, iconColor);
    ImGui::PopID();
    return pressed;
}
} // namespace

void EditorUVE::RenderOverlayUVE() {
    if (m_state != EditorStateUVE::Running || !m_uiInitialized) {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const bool resetLayoutRequested = ConsumeResetDockingLayoutRequestUVE();
    SetupMainDockSpaceUVE(mainViewport, resetLayoutRequested);
    // While playing, the panels lean toward the tint colour: what is edited now is lost at Stop.
    int tintedColors = 0;
    if (m_playModeState != EditorPlayModeStateUVE::Edit && m_playTintEnabled) {
        const ImGuiStyle& style = ImGui::GetStyle();
        for (const ImGuiCol slot : {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg,
                                    ImGuiCol_TitleBgActive, ImGuiCol_PopupBg}) {
            const ImVec4 base = style.Colors[slot];
            const float t = m_playTintStrength;
            ImGui::PushStyleColor(slot, ImVec4{base.x + ((m_playTintColor.r - base.x) * t),
                                               base.y + ((m_playTintColor.g - base.y) * t),
                                               base.z + ((m_playTintColor.b - base.z) * t), base.w});
            ++tintedColors;
        }
    }
    DrawMenuBarUVE();
    DrawPluginWindowUVE();
    DrawEditorPreferencesWindowUVE();
    DrawProjectSettingsWindowUVE();
    DrawInputMapWindowUVE();
    DrawKeyboardShortcutsWindowUVE();

    if (m_entityEditSession.has_value()) {
        // The main window rests while an entity is open in its own window.
        DrawEntityEditorPlaceholderUVE();
        DrawEntityEditorWindowUVE();
    } else if (m_retargetPreview.has_value()) {
        // The scene rests while the Retarget window shows its own world.
        DrawRetargetPlaceholderUVE();
    } else if (m_activeWorkspace == EditorWorkspaceUVE::Scripting) {
        DrawScriptingWorkspaceUVE();
    } else {
        DrawHierarchyPanelUVE();
        DrawViewportPanelUVE();
        DrawInspectorPanelUVE();
        DrawBottomDockContentUVE();
        DrawBottomDockTabBarUVE();
    }
    if (m_retargetWindow.has_value()) {
        DrawRetargetWindowUVE();
    }
    if (m_animationLibraryWindow.has_value()) {
        DrawAnimationLibraryWindowUVE();
    }
    if (m_filePicker.has_value()) {
        DrawFilePickerUVE();
    }
    // Last, so it floats over every panel.
    DrawCommandPaletteUVE();
    ImGui::PopStyleColor(tintedColors);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // Windows that live in their own OS window (the Entity Editor) are drawn and presented here;
    // the main window's context is current again afterwards for the host's own swap.
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* const mainContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(mainContext);
    }
}

void EditorUVE::SetViewportPanelRendererUVE(ViewportPanelRendererUVE renderer) {
    m_viewportPanelRenderer = std::move(renderer);
}

void EditorUVE::DrawViewportPanelUVE() {
    if (!m_viewportPanelVisible) {
        return;
    }
    // Same "fixed default position/size for a fresh session, never fought on later frames" shape
    // as DrawHierarchyPanelUVE()/DrawInspectorPanelUVE() (see their own comments) - this panel
    // fills the gap those two leave between them, matching the space actually visible on screen
    // rather than an arbitrary default ImGui would otherwise cascade this window into.
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth, m_inspectorPanelWidth);
    // In native ImGui docking, FirstUseEver sets the initial fallback position and size,
    // allowing the window to dock into the central node, tab, split, float, and persist in imgui.ini.
    ImGui::SetNextWindowPos(layout.viewportPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(layout.viewportSize, ImGuiCond_FirstUseEver);
    // Zero interior padding for the viewport window so the rendered 3D image fills the panel
    // edge-to-edge (the reference has the grid reaching all four edges with the toolbar floating on
    // top); with the theme's default WindowPadding the GL image is inset ~8px all around, leaving an
    // empty border band. Only this window opts out - the overlay bubbles still float on top since
    // they anchor off the image origin, which now sits flush in the panel corner.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    if (!ImGui::Begin(kPanelLabelViewportUVE, &m_viewportPanelVisible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }
    DrawViewportImageUVE(ViewportContextUVE::Main);
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorUVE::DrawViewportImageUVE(const ViewportContextUVE context) {
    ViewportOverlayStateUVE* state = &m_viewportOverlayState;
    if (context == ViewportContextUVE::EntityEditor) {
        state = &m_entityViewportOverlayState;
    } else if (context == ViewportContextUVE::Retarget) {
        state = &m_retargetViewportOverlayState;
    }

    const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    const ImVec2 viewportOrigin = ImGui::GetCursorScreenPos();
    if (m_viewportPanelRenderer && availableRegion.x > 0.0F && availableRegion.y > 0.0F) {
        state->gameWorkspaceActive = context == ViewportContextUVE::Main &&
                                     m_activeWorkspace == EditorWorkspaceUVE::Game;
        state->studioView = context == ViewportContextUVE::Retarget;
        state->bones.clear();
        if (!state->gameWorkspaceActive) {
            BuildSkeletonOverlayUVE(state->bones);
        }
        const Math::Vector2UVE available{availableRegion.x, availableRegion.y};
        Math::Vector2UVE used{0.0F, 0.0F};
        // Temporarily swap this view's overlay state into m_viewportOverlayState before running
        // the viewport panel renderer and overlay bubbles so RequestViewportViewUVE,
        // NotifyViewportOrbitedUVE, and SetViewportAxisColorsUVE inside the callback mutate the
        // active viewport's state (e.g., Entity Editor) instead of the main scene viewport.
        const bool auxiliaryState = state != &m_viewportOverlayState;
        if (auxiliaryState) {
            if (!state->axisColorsValid && m_viewportOverlayState.axisColorsValid) {
                state->axisColorsValid = true;
                state->axisColorX = m_viewportOverlayState.axisColorX;
                state->axisColorY = m_viewportOverlayState.axisColorY;
                state->axisColorZ = m_viewportOverlayState.axisColorZ;
            }
            std::swap(*state, m_viewportOverlayState);
        }
        const std::uint64_t textureId =
            m_viewportPanelRenderer(context, available, used, m_viewportOverlayState);
        if (textureId != 0U && used.x > 0.0F && used.y > 0.0F) {
            const ImVec2 cursorBeforeImage = ImGui::GetCursorScreenPos();
            // The viewport renderer's framebuffer texture is a normal OpenGL render target
            // (bottom-up texel origin), unlike the top-down icon textures DrawNativeIconLabelUVE
            // displays elsewhere in this file - flip the V axis so the image displays right-side up.
            ImGui::Image(static_cast<ImTextureID>(textureId), ImVec2{used.x, used.y}, ImVec2{0.0F, 1.0F},
                         ImVec2{1.0F, 0.0F});
            // An entity asset dropped on the view goes under the scene root.
            AcceptContentEntityDropUVE(Scene::kInvalidEntityUVE);
            // The projection/gizmo-mode overlay bubbles are editor-authoring chrome - hidden while
            // the Game workspace tab is active, matching Unity's own Scene/Game split where the
            // Game view previews what a player would see with no editor overlays on top.
            if (!m_viewportOverlayState.gameWorkspaceActive && !m_viewportOverlayState.studioView) {
                DrawViewportOverlayBubblesUVE(Math::Vector2UVE{cursorBeforeImage.x, cursorBeforeImage.y},
                                              Math::Vector2UVE{used.x, used.y});
                DrawEntityContextToolbarUVE(Math::Vector2UVE{cursorBeforeImage.x, cursorBeforeImage.y},
                                            Math::Vector2UVE{used.x, used.y});
                m_viewportOverlayState.pointerOverOverlay = ImGui::IsAnyItemHovered();
            } else {
                m_viewportOverlayState.pointerOverOverlay = false;
            }
        }
        if (auxiliaryState) {
            std::swap(*state, m_viewportOverlayState);
        }
    }
    if (availableRegion.x > 0.0F && availableRegion.y > 0.0F) {
        DrawEditorPlayBootSplashOverlayUVE(Math::Vector2UVE{viewportOrigin.x, viewportOrigin.y},
                                           Math::Vector2UVE{availableRegion.x, availableRegion.y});
    }
}

void EditorUVE::DrawViewportOverlayBubblesUVE(const Math::Vector2UVE imageOriginUVE,
                                              const Math::Vector2UVE imageSizeUVE) {
    // Floating "bubble" toolbars over the rendered image itself (Unreal's own modern viewport
    // overlay style), not a docked panel underneath - both bottom-anchored per the requested
    // layout. Drawn against the main window's draw list with SetCursorScreenPos rather than a
    // child window, so clicks land correctly without a second window stealing input focus from
    // the Viewport panel's own scroll/hover state. Takes plain Math::Vector2UVE (not ImVec2) at
    // the boundary since this is a private method declared in the public header - ImVec2 there
    // would force every consumer of editor_uve.h (including Test/Editor's own test executable,
    // which never links uve_editor_imgui) to have ImGui's include path just to parse the class.
    const ImVec2 imageOrigin{imageOriginUVE.x, imageOriginUVE.y};
    static_cast<void>(imageSizeUVE);
    constexpr float kBubbleMarginUVE = 10.0F;
    constexpr float kBubblePaddingXUVE = 6.0F;
    constexpr float kBubblePaddingYUVE = 4.0F;
    constexpr float kBubbleSpacingUVE = 3.0F;
    // Gap between the gizmo-mode bubble and the projection pill, now that both sit side by side
    // at the top of the viewport (matching Godot's own top-left toolbar-strip placement) instead
    // of scattered bottom-left/bottom-center - a single visual row reads as one toolbar, not two.
    constexpr float kBubbleGapUVE = 8.0F;
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const float rulerInset =
        (m_viewportOverlayState.is2DMode && m_viewportOverlayState.viewport2DShowRulers) ? 24.0F : 0.0F;
    const float topY = imageOrigin.y + kBubbleMarginUVE + rulerInset;

    // A vertical-dots glyph (U+22EE) drawn as text came out as "?" - the current base UI font
    // has no glyph for it. Drawn procedurally instead, matching every other icon in this toolbar -
    // reliable regardless of font coverage.
    // "Perspective", "Orthographic", or the named view with its projection ("Top - Ortho").
    const char* const projectionName = m_viewportOverlayState.orthographic ? "Orthographic" : "Perspective";
    const std::string projectionText =
        m_viewportOverlayState.view == ViewportViewUVE::User
            ? std::string{projectionName}
            : std::string{GetViewportViewNameUVE(m_viewportOverlayState.view)} +
                  (m_viewportOverlayState.orthographic ? " - Ortho" : " - Persp");
    const char* const projectionLabel = projectionText.c_str();
    const ImVec2 textSize = ImGui::CalcTextSize(projectionLabel);
    constexpr float kDotsWidthUVE = 10.0F;
    constexpr float kDotsToTextGapUVE = 5.0F;
    const float pillWidth = kDotsWidthUVE + kDotsToTextGapUVE + textSize.x + kBubblePaddingXUVE * 2.0F;

    constexpr int kButtonCount = 6;
    const float diameter = kViewportBubbleIconRadiusUVE * 2.0F;
    const float gizmoBubbleWidth = kBubblePaddingXUVE * 2.0F + diameter * static_cast<float>(kButtonCount) +
                                   kBubbleSpacingUVE * static_cast<float>(kButtonCount - 1);

    // Both bubbles share one height - the taller of the two contents (icon diameter vs. text) plus
    // padding - so the pair reads as a single consistent toolbar strip rather than two mismatched
    // pills; this is also what naturally makes the (previously shorter) projection pill bigger.
    const float unifiedHeight =
        std::max(kBubblePaddingYUVE * 2.0F + diameter, kBubblePaddingYUVE * 2.0F + textSize.y);

    // ---- gizmo-mode + snap + grid bubble, top-left ------------------------------------------
    const ImVec2 gizmoBubbleMin{imageOrigin.x + kBubbleMarginUVE + rulerInset, topY};
    const ImVec2 gizmoBubbleMax{gizmoBubbleMin.x + gizmoBubbleWidth, gizmoBubbleMin.y + unifiedHeight};
    {
        drawList->AddRectFilled(gizmoBubbleMin, gizmoBubbleMax, IM_COL32(18, 21, 28, 200), unifiedHeight * 0.5F);
        drawList->AddRect(gizmoBubbleMin, gizmoBubbleMax, IM_COL32(255, 255, 255, 24), unifiedHeight * 0.5F);

        const float iconY = gizmoBubbleMin.y + (unifiedHeight - diameter) * 0.5F;
        ImGui::SetCursorScreenPos(ImVec2{gizmoBubbleMin.x + kBubblePaddingXUVE, iconY});
        const auto drawGizmoModeButton = [this](const char* const id, const ViewportGizmoModeUVE mode,
                                                const auto& drawIcon) {
            if (DrawViewportBubbleIconButtonUVE(id, m_viewportOverlayState.gizmoMode == mode, drawIcon)) {
                m_viewportOverlayState.gizmoMode = mode;
            }
            ImGui::SameLine(0.0F, kBubbleSpacingUVE);
        };
        drawGizmoModeButton("##viewport-gizmo-move", ViewportGizmoModeUVE::Move, DrawMoveIconUVE);
        drawGizmoModeButton("##viewport-gizmo-rotate", ViewportGizmoModeUVE::Rotate, DrawRotateIconUVE);
        drawGizmoModeButton("##viewport-gizmo-scale", ViewportGizmoModeUVE::Scale, DrawScaleIconUVE);
        drawGizmoModeButton("##viewport-gizmo-universal", ViewportGizmoModeUVE::Universal, DrawUniversalIconUVE);

        const std::uintptr_t snapIconTextureId = m_uiAssets.GetGeneralIconTextureIdUVE("snap");
        if (DrawViewportBubbleIconButtonUVE(
                "##viewport-snap", m_viewportOverlayState.snapEnabled,
                [snapIconTextureId](ImDrawList& list, const ImVec2 center, const float radius, const ImU32) {
                    if (snapIconTextureId == 0U) {
                        return;
                    }
                    const float half = radius * 0.55F;
                    list.AddImage(static_cast<ImTextureID>(snapIconTextureId),
                                  ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half});
                })) {
            // The bubble used to be inert: snapEnabled was written here and read only by the
            // bubble's own highlight, so the button lit up and changed nothing. It is the editor's
            // real snapping setting that decides whether a transform quantises, so route it there
            // and keep the bubble reflecting what actually took effect - if the setting refuses
            // the change (a gesture is in flight, say), the bubble must not claim otherwise.
            EditorTransformSnappingSettingsUVE snapping = GetTransformSnappingSettingsUVE();
            snapping.enabled = !m_viewportOverlayState.snapEnabled;
            if (SetTransformSnappingSettingsUVE(snapping)) {
                m_viewportOverlayState.snapEnabled = snapping.enabled;
            }
        }
        ImGui::SameLine(0.0F, kBubbleSpacingUVE);

        if (DrawViewportBubbleIconButtonUVE("##viewport-grid", m_viewportOverlayState.gridVisible,
                                            DrawGridIconUVE)) {
            static_cast<void>(SetViewportGridUVE(!m_viewportOverlayState.gridVisible,
                                                 m_viewportOverlayState.gridOpacity));
        }
        // Click toggles; right-click opens the grid's options, so the toolbar stays one button wide.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Grid - right-click for options");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##viewport-grid-options");
        }
        if (ImGui::BeginPopup("##viewport-grid-options")) {
            ImGui::TextDisabled("Grid");
            ImGui::Separator();
            bool visible = m_viewportOverlayState.gridVisible;
            float opacityPercent = m_viewportOverlayState.gridOpacity * 100.0F;
            bool changed = ImGui::Checkbox("Show grid", &visible);
            ImGui::BeginDisabled(!visible);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
            changed |= ImGui::SliderFloat("Opacity", &opacityPercent, kMinimumViewportGridOpacityUVE * 100.0F, 100.0F,
                                          "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            if (changed) {
                static_cast<void>(SetViewportGridUVE(visible, opacityPercent / 100.0F));
            }
            // Round sizes only: a slider would hand out 0.2371 m, which nobody lays a scene out in.
            // A size loaded from a hand-edited file still shows, as itself, in the preview.
            static constexpr std::array<float, 7> kCellSizesUVE{0.1F, 0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F};
            const auto cellSizeLabel = [](const float size) {
                std::array<char, 32> label{};
                std::snprintf(label.data(), label.size(), "%g m", static_cast<double>(size));
                return std::string(label.data());
            };
            const float cellSize = m_viewportOverlayState.gridCellSize;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
            if (ImGui::BeginCombo("Cell size", cellSizeLabel(cellSize).c_str())) {
                for (const float size : kCellSizesUVE) {
                    const bool current = size == cellSize;
                    if (ImGui::Selectable(cellSizeLabel(size).c_str(), current)) {
                        static_cast<void>(SetViewportGridCellSizeUVE(size));
                    }
                    if (current) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("The smallest grid square. Zooming out still steps the grid up in tens.");
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }

    // ---- projection mode pill, top-left (right after the gizmo bubble) ---------------------
    const ImVec2 pillMin{gizmoBubbleMax.x + kBubbleGapUVE, topY};
    const ImVec2 pillMax{pillMin.x + pillWidth, pillMin.y + unifiedHeight};
    {
        ImGui::SetCursorScreenPos(pillMin);
        ImGui::PushID("##viewport-projection-toggle");
        const bool pressed = ImGui::InvisibleButton("##pill", ImVec2{pillWidth, unifiedHeight});
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        drawList->AddRectFilled(pillMin, pillMax, hovered ? IM_COL32(30, 34, 44, 220) : IM_COL32(18, 21, 28, 200),
                                unifiedHeight * 0.5F);
        drawList->AddRect(pillMin, pillMax, IM_COL32(255, 255, 255, 24), unifiedHeight * 0.5F);
        const float pillCenterY = (pillMin.y + pillMax.y) * 0.5F;
        const float dotsCenterX = pillMin.x + kBubblePaddingXUVE + kDotsWidthUVE * 0.5F;
        constexpr float kDotRadiusUVE = 1.4F;
        constexpr float kDotSpacingUVE = 5.0F;
        const ImU32 dotColor = IM_COL32(224, 228, 236, 255);
        for (int index = -1; index <= 1; ++index) {
            drawList->AddCircleFilled(ImVec2{dotsCenterX, pillCenterY + static_cast<float>(index) * kDotSpacingUVE},
                                      kDotRadiusUVE, dotColor, 8);
        }
        drawList->AddText(ImVec2{pillMin.x + kBubblePaddingXUVE + kDotsWidthUVE + kDotsToTextGapUVE,
                                 pillCenterY - textSize.y * 0.5F},
                          dotColor, projectionLabel);
        if (pressed) {
            ImGui::OpenPopup("##viewport-view-menu");
        }
        if (hovered && !ImGui::IsPopupOpen("##viewport-view-menu")) {
            ImGui::SetTooltip("Projection and view");
        }
        ImGui::SetNextWindowPos(ImVec2{pillMin.x, pillMax.y + 4.0F}, ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 10.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0F, 4.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0F);
        if (ImGui::BeginPopup("##viewport-view-menu")) {
            // One control per decision: a two-way segmented switch for the projection, then the six
            // named views as a 3x2 grid laid out by opposites (Top over Bottom, Front over Back,
            // Right over Left). The active option is filled; the rest are quiet.
            const float cellWidth = ImGui::GetFontSize() * 4.6F;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const ImVec4 activeFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            const ImVec4 quietFill = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
            const auto option = [&](const char* label, const bool active, const float width) {
                ImGui::PushStyleColor(ImGuiCol_Button, active ? activeFill : quietFill);
                const bool clicked = ImGui::Button(label, ImVec2{width, 0.0F});
                ImGui::PopStyleColor();
                return clicked;
            };

            ImGui::TextDisabled("PROJECTION");
            const float halfWidth = (cellWidth * 3.0F + spacing * 2.0F - spacing) * 0.5F;
            if (option("Perspective", !m_viewportOverlayState.orthographic, halfWidth)) {
                SetViewportOrthographicUVE(false);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Toggle: Numpad 5  /  Alt+5");
            }
            ImGui::SameLine();
            if (option("Orthographic", m_viewportOverlayState.orthographic, halfWidth)) {
                SetViewportOrthographicUVE(true);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Toggle: Numpad 5  /  Alt+5");
            }

            ImGui::Dummy(ImVec2{0.0F, 4.0F});
            ImGui::TextDisabled("VIEW");
            constexpr std::array<ViewportViewUVE, 6> kViews{ViewportViewUVE::Top,    ViewportViewUVE::Front,
                                                            ViewportViewUVE::Right,  ViewportViewUVE::Bottom,
                                                            ViewportViewUVE::Back,   ViewportViewUVE::Left};
            for (std::size_t index = 0U; index < kViews.size(); ++index) {
                const ViewportViewUVE view = kViews[index];
                if (index % 3U != 0U) {
                    ImGui::SameLine();
                }
                if (option(GetViewportViewNameUVE(view), m_viewportOverlayState.view == view, cellWidth)) {
                    RequestViewportViewUVE(view);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", GetViewportViewShortcutUVE(view));
                }
            }
            ImGui::Dummy(ImVec2{0.0F, 2.0F});
            ImGui::TextDisabled("Orbit to return to a free view. Numpad or Alt+digit.");
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
    }

    // ---- Shading Mode pill -----------------------------------------------------------------
    static constexpr std::array<const char*, 5> kShadingNames{"Lit", "Unshaded", "Wireframe", "Lighting", "Normals"};
    const char* const shadingLabel =
        kShadingNames[std::clamp(static_cast<std::size_t>(m_viewportOverlayState.shadingMode), std::size_t{0U},
                                 kShadingNames.size() - 1U)];
    const ImVec2 shadingTextSize = ImGui::CalcTextSize(shadingLabel);
    const float shadingPillWidth = shadingTextSize.x + kBubblePaddingXUVE * 2.0F + 8.0F;
    const ImVec2 shadingPillMin{pillMax.x + kBubbleGapUVE, topY};
    const ImVec2 shadingPillMax{shadingPillMin.x + shadingPillWidth, shadingPillMin.y + unifiedHeight};
    {
        ImGui::SetCursorScreenPos(shadingPillMin);
        const bool pressedShading =
            ImGui::InvisibleButton("##shading-mode-btn", ImVec2{shadingPillWidth, unifiedHeight});
        const bool hoveredShading = ImGui::IsItemHovered();
        drawList->AddRectFilled(shadingPillMin, shadingPillMax,
                                hoveredShading ? IM_COL32(30, 34, 44, 220) : IM_COL32(18, 21, 28, 200),
                                unifiedHeight * 0.5F);
        drawList->AddRect(shadingPillMin, shadingPillMax, IM_COL32(255, 255, 255, 24), unifiedHeight * 0.5F);
        drawList->AddText(ImVec2{shadingPillMin.x + kBubblePaddingXUVE + 4.0F,
                                 shadingPillMin.y + (unifiedHeight - shadingTextSize.y) * 0.5F},
                          IM_COL32(224, 228, 236, 255), shadingLabel);
        if (pressedShading) {
            ImGui::OpenPopup("##viewport-shading-menu");
        }
        if (hoveredShading && !ImGui::IsPopupOpen("##viewport-shading-menu")) {
            ImGui::SetTooltip("Viewport Shading Mode");
        }
        if (ImGui::BeginPopup("##viewport-shading-menu")) {
            ImGui::TextDisabled("VIEWPORT SHADING");
            ImGui::Separator();
            for (std::size_t i = 0U; i < kShadingNames.size(); ++i) {
                const bool current = m_viewportOverlayState.shadingMode == static_cast<int>(i);
                if (ImGui::MenuItem(kShadingNames[i], nullptr, current)) {
                    m_viewportOverlayState.shadingMode = static_cast<int>(i);
                }
            }
            ImGui::EndPopup();
        }
    }

    // ---- Physics Simulation Toggle pill ----------------------------------------------------
    const bool physicsPaused =
        m_simulationControl != nullptr
            ? m_simulationControl->GetSimulationExecutionModeUVE() == Core::SimulationExecutionModeUVE::Paused
            : m_viewportOverlayState.physicsPaused;
    const char* const physLabel = physicsPaused ? "Physics: OFF" : "Physics: ON";
    const ImVec2 physTextSize = ImGui::CalcTextSize(physLabel);
    constexpr float kIndicatorRadius = 3.5F;
    const float physPillWidth = physTextSize.x + kBubblePaddingXUVE * 2.0F + kIndicatorRadius * 2.0F + 8.0F;
    const ImVec2 physPillMin{shadingPillMax.x + kBubbleGapUVE, topY};
    const ImVec2 physPillMax{physPillMin.x + physPillWidth, physPillMin.y + unifiedHeight};
    {
        ImGui::SetCursorScreenPos(physPillMin);
        const bool pressedPhys =
            ImGui::InvisibleButton("##physics-toggle-btn", ImVec2{physPillWidth, unifiedHeight});
        const bool hoveredPhys = ImGui::IsItemHovered();
        drawList->AddRectFilled(physPillMin, physPillMax,
                                hoveredPhys ? IM_COL32(30, 34, 44, 220) : IM_COL32(18, 21, 28, 200),
                                unifiedHeight * 0.5F);
        drawList->AddRect(physPillMin, physPillMax, IM_COL32(255, 255, 255, 24), unifiedHeight * 0.5F);
        const float dotY = (physPillMin.y + physPillMax.y) * 0.5F;
        const float dotX = physPillMin.x + kBubblePaddingXUVE + kIndicatorRadius + 2.0F;
        const ImU32 dotColor = physicsPaused ? IM_COL32(236, 172, 52, 255) : IM_COL32(76, 217, 100, 255);
        drawList->AddCircleFilled(ImVec2{dotX, dotY}, kIndicatorRadius, dotColor, 12);
        drawList->AddText(
            ImVec2{dotX + kIndicatorRadius + 6.0F, physPillMin.y + (unifiedHeight - physTextSize.y) * 0.5F},
            IM_COL32(224, 228, 236, 255), physLabel);
        if (pressedPhys) {
            const bool newPaused = !physicsPaused;
            m_viewportOverlayState.physicsPaused = newPaused;
            if (m_simulationControl != nullptr) {
                static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(
                    newPaused ? Core::SimulationExecutionModeUVE::Paused : Core::SimulationExecutionModeUVE::Running));
            }
        }
        if (hoveredPhys) {
            ImGui::SetTooltip(physicsPaused ? "Resume physics and fixed simulation steps"
                                            : "Pause physics simulation in the viewport");
        }
    }

    // ---- Preview Sun & Environment (Godot-style Preview Toolbar Button) ---------------------
    const bool previewActive = m_viewportOverlayState.previewSun || m_viewportOverlayState.previewEnvironment;
    const char* const previewLabel = "Preview \xe2\x96\xbe";
    const ImVec2 previewTextSize = ImGui::CalcTextSize(previewLabel);
    const float previewPillWidth = previewTextSize.x + kBubblePaddingXUVE * 2.0F + 6.0F;
    const ImVec2 previewPillMin{physPillMax.x + kBubbleGapUVE, topY};
    const ImVec2 previewPillMax{previewPillMin.x + previewPillWidth, previewPillMin.y + unifiedHeight};
    {
        ImGui::SetCursorScreenPos(previewPillMin);
        const bool pressedPreview =
            ImGui::InvisibleButton("##preview-sun-env-btn", ImVec2{previewPillWidth, unifiedHeight});
        const bool hoveredPreview = ImGui::IsItemHovered();
        drawList->AddRectFilled(previewPillMin, previewPillMax,
                                previewActive
                                    ? IM_COL32(45, 75, 120, 230)
                                    : (hoveredPreview ? IM_COL32(30, 34, 44, 220) : IM_COL32(18, 21, 28, 200)),
                                unifiedHeight * 0.5F);
        drawList->AddRect(previewPillMin, previewPillMax,
                          previewActive ? IM_COL32(90, 160, 245, 180) : IM_COL32(255, 255, 255, 24),
                          unifiedHeight * 0.5F);
        drawList->AddText(
            ImVec2{previewPillMin.x + kBubblePaddingXUVE, previewPillMin.y + (unifiedHeight - previewTextSize.y) * 0.5F},
            previewActive ? IM_COL32(240, 245, 255, 255) : IM_COL32(224, 228, 236, 255), previewLabel);
        if (pressedPreview) {
            ImGui::OpenPopup("##preview-sun-env-popup");
        }
        if (hoveredPreview) {
            ImGui::SetTooltip("Preview Sun and Environment settings (Godot-style)");
        }

        if (ImGui::BeginPopup("##preview-sun-env-popup")) {
            ImGui::TextDisabled("PREVIEW SUN");
            ImGui::Checkbox("Preview Sun##chk", &m_viewportOverlayState.previewSun);
            if (m_viewportOverlayState.previewSun) {
                ImGui::SliderFloat("Azimuth##sun", &m_viewportOverlayState.previewSunAzimuth, -180.0F, 180.0F, "%.1f deg");
                ImGui::SliderFloat("Altitude##sun", &m_viewportOverlayState.previewSunAltitude, -90.0F, 90.0F, "%.1f deg");
                ImGui::SliderFloat("Energy##sun", &m_viewportOverlayState.previewSunEnergy, 0.0F, 10.0F, "%.2f");
                ImGui::ColorEdit3("Color##sun", m_viewportOverlayState.previewSunColor.data());
            }
            if (ImGui::Button("Add Sun to Scene")) {
                Scene::EntityUVE sunEntity =
                    FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
                if (sunEntity == Scene::kInvalidEntityUVE) {
                    sunEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
                }
                if (sunEntity != Scene::kInvalidEntityUVE) {
                    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                    constexpr float kDegToRad = 0.01745329251994329577F;
                    const float pitch = m_viewportOverlayState.previewSunAltitude * kDegToRad;
                    const float yaw = m_viewportOverlayState.previewSunAzimuth * kDegToRad;
                    Math::QuaternionUVE rot{};
                    if (Math::TryMakeEulerUVE(Math::Vector3UVE{pitch, yaw, 0.0F}, rot)) {
                        if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(sunEntity)) {
                            auto tf = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(sunEntity);
                            tf.localRotation = rot;
                            m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, sunEntity, tf);
                        }
                    }
                    if (entityManager.HasComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity)) {
                        auto& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity);
                        emitter.color = Math::ColorUVE{m_viewportOverlayState.previewSunColor[0],
                                                       m_viewportOverlayState.previewSunColor[1],
                                                       m_viewportOverlayState.previewSunColor[2]};
                        emitter.energy = m_viewportOverlayState.previewSunEnergy;
                        emitter.shadowEnabled = true;
                    }
                    m_sceneDirty = true;
                    SelectEntityUVE(sunEntity);
                    m_contentStatusMessage = "Configured and added DirectionalLight3D to the scene.";
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("PREVIEW ENVIRONMENT");
            ImGui::Checkbox("Preview Environment##chk", &m_viewportOverlayState.previewEnvironment);
            if (m_viewportOverlayState.previewEnvironment) {
                ImGui::ColorEdit3("Sky Color##env", m_viewportOverlayState.previewSkyColor.data());
                ImGui::ColorEdit3("Horizon Color##env", m_viewportOverlayState.previewHorizonColor.data());
                ImGui::ColorEdit3("Ground Color##env", m_viewportOverlayState.previewGroundColor.data());
            }
            if (ImGui::Button("Add Environment to Scene")) {
                Scene::EntityUVE envEntity =
                    FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
                if (envEntity == Scene::kInvalidEntityUVE) {
                    envEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
                }
                if (envEntity != Scene::kInvalidEntityUVE) {
                    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity)) {
                        auto& env = entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity);
                        env.ambientSource = Scene::WorldEnvironmentAmbientSourceUVE::Sky;
                        env.skyColor = Math::Vector3UVE{m_viewportOverlayState.previewSkyColor[0],
                                                        m_viewportOverlayState.previewSkyColor[1],
                                                        m_viewportOverlayState.previewSkyColor[2]};
                        env.horizonColor = Math::Vector3UVE{m_viewportOverlayState.previewHorizonColor[0],
                                                            m_viewportOverlayState.previewHorizonColor[1],
                                                            m_viewportOverlayState.previewHorizonColor[2]};
                        env.groundColor = Math::Vector3UVE{m_viewportOverlayState.previewGroundColor[0],
                                                           m_viewportOverlayState.previewGroundColor[1],
                                                           m_viewportOverlayState.previewGroundColor[2]};
                        env.ambientEnergy = 1.0F;
                        env.postProcessingEnabled = true;
                    }
                    m_sceneDirty = true;
                    SelectEntityUVE(envEntity);
                    m_contentStatusMessage = "Configured and added WorldEnvironment to the scene.";
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Add to Scene", ImVec2{-1.0F, 26.0F})) {
                Scene::EntityUVE sunEntity =
                    FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
                if (sunEntity == Scene::kInvalidEntityUVE) {
                    sunEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
                }
                if (sunEntity != Scene::kInvalidEntityUVE) {
                    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                    constexpr float kDegToRad = 0.01745329251994329577F;
                    const float pitch = m_viewportOverlayState.previewSunAltitude * kDegToRad;
                    const float yaw = m_viewportOverlayState.previewSunAzimuth * kDegToRad;
                    Math::QuaternionUVE rot{};
                    if (Math::TryMakeEulerUVE(Math::Vector3UVE{pitch, yaw, 0.0F}, rot)) {
                        if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(sunEntity)) {
                            auto tf = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(sunEntity);
                            tf.localRotation = rot;
                            m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, sunEntity, tf);
                        }
                    }
                    if (entityManager.HasComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity)) {
                        auto& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity);
                        emitter.color = Math::ColorUVE{m_viewportOverlayState.previewSunColor[0],
                                                       m_viewportOverlayState.previewSunColor[1],
                                                       m_viewportOverlayState.previewSunColor[2]};
                        emitter.energy = m_viewportOverlayState.previewSunEnergy;
                        emitter.shadowEnabled = true;
                    }
                }

                Scene::EntityUVE envEntity =
                    FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
                if (envEntity == Scene::kInvalidEntityUVE) {
                    envEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
                }
                if (envEntity != Scene::kInvalidEntityUVE) {
                    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity)) {
                        auto& env = entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity);
                        env.ambientSource = Scene::WorldEnvironmentAmbientSourceUVE::Sky;
                        env.skyColor = Math::Vector3UVE{m_viewportOverlayState.previewSkyColor[0],
                                                        m_viewportOverlayState.previewSkyColor[1],
                                                        m_viewportOverlayState.previewSkyColor[2]};
                        env.horizonColor = Math::Vector3UVE{m_viewportOverlayState.previewHorizonColor[0],
                                                            m_viewportOverlayState.previewHorizonColor[1],
                                                            m_viewportOverlayState.previewHorizonColor[2]};
                        env.groundColor = Math::Vector3UVE{m_viewportOverlayState.previewGroundColor[0],
                                                           m_viewportOverlayState.previewGroundColor[1],
                                                           m_viewportOverlayState.previewGroundColor[2]};
                        env.ambientEnergy = 1.0F;
                        env.postProcessingEnabled = true;
                    }
                }

                m_sceneDirty = true;
                if (sunEntity != Scene::kInvalidEntityUVE) {
                    SelectEntityUVE(sunEntity);
                } else if (envEntity != Scene::kInvalidEntityUVE) {
                    SelectEntityUVE(envEntity);
                }
                m_contentStatusMessage = "Configured and added DirectionalLight3D and WorldEnvironment3D to the scene.";
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ---- 2D / 3D Mode Toggle & 2D Viewport Options Pill -------------------------------------
    const char* const mode2DLabel = m_viewportOverlayState.is2DMode ? "2D \xe2\x96\xbe" : "3D";
    const ImVec2 mode2DTextSize = ImGui::CalcTextSize(mode2DLabel);
    const float mode2DPillWidth = mode2DTextSize.x + kBubblePaddingXUVE * 2.0F + 8.0F;
    const ImVec2 mode2DPillMin{previewPillMax.x + kBubbleGapUVE, topY};
    const ImVec2 mode2DPillMax{mode2DPillMin.x + mode2DPillWidth, mode2DPillMin.y + unifiedHeight};
    {
        ImGui::SetCursorScreenPos(mode2DPillMin);
        const bool pressedMode2D =
            ImGui::InvisibleButton("##viewport-2d-mode-btn", ImVec2{mode2DPillWidth, unifiedHeight});
        const bool rightClickedMode2D = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const bool hoveredMode2D = ImGui::IsItemHovered();
        drawList->AddRectFilled(
            mode2DPillMin, mode2DPillMax,
            m_viewportOverlayState.is2DMode
                ? IM_COL32(64, 132, 214, 235)
                : (hoveredMode2D ? IM_COL32(30, 34, 44, 220) : IM_COL32(18, 21, 28, 200)),
            unifiedHeight * 0.5F);
        drawList->AddRect(mode2DPillMin, mode2DPillMax,
                          m_viewportOverlayState.is2DMode ? IM_COL32(120, 185, 255, 200) : IM_COL32(255, 255, 255, 24),
                          unifiedHeight * 0.5F);
        drawList->AddText(ImVec2{mode2DPillMin.x + kBubblePaddingXUVE + 4.0F,
                                 mode2DPillMin.y + (unifiedHeight - mode2DTextSize.y) * 0.5F},
                          IM_COL32(240, 245, 255, 255), mode2DLabel);
        if (pressedMode2D) {
            m_viewportOverlayState.is2DMode = !m_viewportOverlayState.is2DMode;
        }
        if (rightClickedMode2D && m_viewportOverlayState.is2DMode) {
            ImGui::OpenPopup("##viewport-2d-options-popup");
        }
        if (hoveredMode2D && !ImGui::IsPopupOpen("##viewport-2d-options-popup")) {
            ImGui::SetTooltip(m_viewportOverlayState.is2DMode
                                  ? "2D Viewport active (click for 3D, right-click for 2D options)"
                                  : "Switch to 2D Viewport");
        }
        if (ImGui::BeginPopup("##viewport-2d-options-popup")) {
            ImGui::TextDisabled("2D VIEWPORT TOOL");
            if (ImGui::RadioButton("Select / Transform (Q/W)", !m_viewportOverlayState.viewport2DPanTool)) {
                m_viewportOverlayState.viewport2DPanTool = false;
            }
            if (ImGui::RadioButton("Hand / Pan (H / Space)", m_viewportOverlayState.viewport2DPanTool)) {
                m_viewportOverlayState.viewport2DPanTool = true;
            }
            ImGui::Checkbox("Edit Pivot Offset", &m_viewportOverlayState.viewport2DPivotEditing);
            ImGui::Separator();
            ImGui::TextDisabled("2D SNAPPING");
            ImGui::Checkbox("Pixel Snap (1 px)", &m_viewportOverlayState.viewport2DPixelSnap);
            ImGui::Checkbox("Grid Snap", &m_viewportOverlayState.viewport2DGridSnap);
            if (m_viewportOverlayState.viewport2DGridSnap) {
                ImGui::SliderFloat("Grid Step (px)", &m_viewportOverlayState.viewport2DGridStep, 1.0F, 256.0F, "%.0f px");
            }
            ImGui::Checkbox("Smart Guide Snap", &m_viewportOverlayState.viewport2DGuideSnap);
            ImGui::Checkbox("Rotation Snap", &m_viewportOverlayState.viewport2DRotationSnap);
            if (m_viewportOverlayState.viewport2DRotationSnap) {
                ImGui::SliderFloat("Angle Step", &m_viewportOverlayState.viewport2DRotationStepDeg, 1.0F, 90.0F, "%.0f deg");
            }
            ImGui::Separator();
            ImGui::TextDisabled("2D OVERLAYS & VIEW");
            ImGui::Checkbox("Show Rulers", &m_viewportOverlayState.viewport2DShowRulers);
            ImGui::Checkbox("Show Guides", &m_viewportOverlayState.viewport2DShowGuides);
            ImGui::Checkbox("Show 2D Gizmos & Colliders", &m_viewportOverlayState.viewport2DShowCollision);
            ImGui::Checkbox("Show Coordinates HUD", &m_viewportOverlayState.viewport2DShowHud);
            if (ImGui::Button("Zoom 100% (1:1)")) {
                ++m_viewportOverlayState.viewport2DZoom100Serial;
            }
            ImGui::SameLine();
            if (ImGui::Button("Fit Canvas")) {
                ++m_viewportOverlayState.viewport2DZoomFitSerial;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Guides")) {
                ++m_viewportOverlayState.viewport2DClearGuidesSerial;
            }
            ImGui::EndPopup();
        }
    }
}

void EditorUVE::SetEntityContextToolbarAnchorUVE(const Scene::EntityUVE entity, const float pixelX,
                                                 const float pixelY) {
    m_viewportOverlayState.entityContextToolbarOpen = true;
    m_viewportOverlayState.entityContextToolbarEntity = entity;
    m_viewportOverlayState.entityContextToolbarPixelX = pixelX;
    m_viewportOverlayState.entityContextToolbarPixelY = pixelY;
}

void EditorUVE::ClearEntityContextToolbarUVE() noexcept {
    m_viewportOverlayState.entityContextToolbarOpen = false;
    m_viewportOverlayState.entityContextToolbarEntity = Scene::kInvalidEntityUVE;
}

namespace {

[[nodiscard]] bool IsViewportAxisColorValidUVE(const EditorUVE::ViewportAxisColorUVE& color) {
    const auto channelValid = [](const float channel) {
        // NaN fails both comparisons, so it is refused here rather than surviving as a colour.
        return channel >= 0.0F && channel <= 1.0F;
    };
    return channelValid(color.r) && channelValid(color.g) && channelValid(color.b);
}

} // namespace

void EditorUVE::SetViewportOrthographicUVE(const bool orthographic) noexcept {
    m_viewportOverlayState.orthographic = orthographic;
    m_viewportOrthographicIsAutomatic = false;
}

bool EditorUVE::CanFocusEntityInViewportUVE(const Scene::EntityUVE entity) const {
    return IsDocumentEntityUVE(entity) && (ComposeMarker3DFocusBookmarkUVE(entity).has_value() ||
                                           ResolveEntityFocusTargetUVE(entity).has_value());
}

bool EditorUVE::RequestViewportFocusUVE(const Scene::EntityUVE entity) {
    if (!CanFocusEntityInViewportUVE(entity)) {
        return false;
    }
    m_viewportOverlayState.focusEntity = entity;
    ++m_viewportOverlayState.focusRequestSerial;
    return true;
}

void EditorUVE::RequestViewportViewUVE(const ViewportViewUVE view) noexcept {
    m_viewportOverlayState.view = view;
    ++m_viewportOverlayState.viewRequestSerial;
    // An axis view reads best flat; switch to orthographic unless the author already chose it.
    if (view != ViewportViewUVE::User && !m_viewportOverlayState.orthographic) {
        m_viewportOverlayState.orthographic = true;
        m_viewportOrthographicIsAutomatic = true;
    }
}

void EditorUVE::NotifyViewportOrbitedUVE() noexcept {
    if (m_viewportOverlayState.view == ViewportViewUVE::User) {
        return;
    }
    m_viewportOverlayState.view = ViewportViewUVE::User;
    if (m_viewportOrthographicIsAutomatic) {
        m_viewportOverlayState.orthographic = false;
        m_viewportOrthographicIsAutomatic = false;
    }
}

const char* EditorUVE::GetViewportViewNameUVE(const ViewportViewUVE view) noexcept {
    switch (view) {
        case ViewportViewUVE::Top: return "Top";
        case ViewportViewUVE::Bottom: return "Bottom";
        case ViewportViewUVE::Front: return "Front";
        case ViewportViewUVE::Back: return "Back";
        case ViewportViewUVE::Right: return "Right";
        case ViewportViewUVE::Left: return "Left";
        case ViewportViewUVE::User: break;
    }
    return "User";
}

std::optional<EditorUVE::ViewportViewUVE> EditorUVE::GetViewportViewForKeypadDigitUVE(const int digit,
                                                                                    const bool opposite) noexcept {
    switch (digit) {
        case 7: return opposite ? ViewportViewUVE::Bottom : ViewportViewUVE::Top;
        case 1: return opposite ? ViewportViewUVE::Back : ViewportViewUVE::Front;
        case 3: return opposite ? ViewportViewUVE::Left : ViewportViewUVE::Right;
        default: return std::nullopt;
    }
}

const char* EditorUVE::GetViewportViewShortcutUVE(const ViewportViewUVE view) noexcept {
    switch (view) {
        case ViewportViewUVE::Top: return "Numpad 7  /  Alt+7";
        case ViewportViewUVE::Bottom: return "Ctrl+Numpad 7  /  Ctrl+Alt+7";
        case ViewportViewUVE::Front: return "Numpad 1  /  Alt+1";
        case ViewportViewUVE::Back: return "Ctrl+Numpad 1  /  Ctrl+Alt+1";
        case ViewportViewUVE::Right: return "Numpad 3  /  Alt+3";
        case ViewportViewUVE::Left: return "Ctrl+Numpad 3  /  Ctrl+Alt+3";
        case ViewportViewUVE::User: break;
    }
    return "";
}

bool EditorUVE::SetViewportGridUVE(const bool visible, const float opacity) {
    if (!std::isfinite(opacity) || opacity < kMinimumViewportGridOpacityUVE || opacity > 1.0F) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.gridVisible;
    const float previousOpacity = m_viewportOverlayState.gridOpacity;
    m_viewportOverlayState.gridVisible = visible;
    m_viewportOverlayState.gridOpacity = opacity;
    namespace Id = EditorSettingIdUVE;
    NotifyEditorSettingChangedUVE(Id::kGridVisibleUVE, previousVisible, visible);
    NotifyEditorSettingChangedUVE(Id::kGridOpacityUVE, static_cast<double>(previousOpacity),
                                  static_cast<double>(opacity));
    return true;
}

bool EditorUVE::SetViewportGridCellSizeUVE(const float cellSize) {
    if (!std::isfinite(cellSize) || cellSize < kMinimumViewportGridCellSizeUVE ||
        cellSize > kMaximumViewportGridCellSizeUVE) {
        return false;
    }
    const float previousCellSize = m_viewportOverlayState.gridCellSize;
    m_viewportOverlayState.gridCellSize = cellSize;
    NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kGridCellSizeUVE,
                                  static_cast<double>(previousCellSize), static_cast<double>(cellSize));
    return true;
}

bool EditorUVE::SetViewportSelectionOutlineUVE(const bool visible, const ViewportAxisColorUVE color,
                                               const float thickness) {
    if (!IsViewportAxisColorValidUVE(color) || !std::isfinite(thickness) ||
        thickness < kMinimumSelectionOutlineThicknessUVE || thickness > kMaximumSelectionOutlineThicknessUVE) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.selectionOutlineVisible;
    const ViewportAxisColorUVE previousColor = m_viewportOverlayState.selectionOutlineColor;
    const float previousThickness = m_viewportOverlayState.selectionOutlineThickness;
    m_viewportOverlayState.selectionOutlineVisible = visible;
    m_viewportOverlayState.selectionOutlineColor = color;
    m_viewportOverlayState.selectionOutlineThickness = thickness;
    namespace Id = EditorSettingIdUVE;
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineVisibleUVE, previousVisible, visible);
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineColorUVE,
                                  Config::SettingColorUVE{previousColor.r, previousColor.g, previousColor.b},
                                  Config::SettingColorUVE{color.r, color.g, color.b});
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineThicknessUVE, static_cast<double>(previousThickness),
                                  static_cast<double>(thickness));
    return true;
}

bool EditorUVE::SetViewportAxisColorsUVE(const ViewportAxisColorUVE x, const ViewportAxisColorUVE y,
                                         const ViewportAxisColorUVE z) {
    // All three or none: a half-applied palette would leave one axis in a colour the author never
    // chose, which is worse than refusing the whole change.
    if (!IsViewportAxisColorValidUVE(x) || !IsViewportAxisColorValidUVE(y) ||
        !IsViewportAxisColorValidUVE(z)) {
        return false;
    }
    m_viewportOverlayState.axisColorX = x;
    m_viewportOverlayState.axisColorY = y;
    m_viewportOverlayState.axisColorZ = z;
    m_viewportOverlayState.axisColorsValid = true;
    return true;
}

bool EditorUVE::AreViewportAxisColorsSetUVE() const noexcept {
    return m_viewportOverlayState.axisColorsValid;
}

void EditorUVE::ResetViewportAxisColorsUVE() noexcept {
    m_viewportOverlayState.axisColorsValid = false;
    m_viewportOverlayState.axisColorX = ViewportAxisColorUVE{};
    m_viewportOverlayState.axisColorY = ViewportAxisColorUVE{};
    m_viewportOverlayState.axisColorZ = ViewportAxisColorUVE{};
}

EditorUVE::ViewportAxisColorUVE EditorUVE::GetViewportAxisColorUVE(const int axisIndex) const {
    switch (axisIndex) {
        case 0: return m_viewportOverlayState.axisColorX;
        case 1: return m_viewportOverlayState.axisColorY;
        case 2: return m_viewportOverlayState.axisColorZ;
        default: return ViewportAxisColorUVE{};
    }
}

// The right-click "Scripting" bubble, anchored at the entity's projected screen position rather
// than the panel's own fixed corner (contrast DrawViewportOverlayBubblesUVE's gizmo/projection
// bubbles above). Same InvisibleButton + manual ImDrawList paint idiom, not ImGui::BeginPopup -
// a popup is a second ImGui window and would steal hover from camera orbit/pan exactly the way
// pointerOverOverlay exists to prevent for viewport-anchored chrome (see its own doc comment).
void EditorUVE::DrawEntityContextToolbarUVE(const Math::Vector2UVE imageOriginUVE,
                                            const Math::Vector2UVE imageSizeUVE) {
    if (!m_viewportOverlayState.entityContextToolbarOpen || m_services == nullptr) {
        return;
    }
    const Scene::EntityUVE entity = m_viewportOverlayState.entityContextToolbarEntity;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // The button offers nothing an entity can't use - no silent "add a Script component for you".
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return;
    }

    const char* const kLabel = "Scripting";
    const ImVec2 textSize = ImGui::CalcTextSize(kLabel);
    constexpr float kPaddingXUVE = 8.0F;
    constexpr float kPaddingYUVE = 5.0F;
    const float pillWidth = textSize.x + kPaddingXUVE * 2.0F;
    const float pillHeight = textSize.y + kPaddingYUVE * 2.0F;

    // Anchor centered under the entity's projected pixel, clamped inside the rendered image so a
    // point near an edge never draws the toolbar half off the panel.
    const float anchorX = imageOriginUVE.x + m_viewportOverlayState.entityContextToolbarPixelX;
    const float anchorY = imageOriginUVE.y + m_viewportOverlayState.entityContextToolbarPixelY;
    const float minX = imageOriginUVE.x;
    const float maxX = imageOriginUVE.x + imageSizeUVE.x - pillWidth;
    const float minY = imageOriginUVE.y;
    const float maxY = imageOriginUVE.y + imageSizeUVE.y - pillHeight;
    const ImVec2 pillMin{std::clamp(anchorX - pillWidth * 0.5F, minX, std::max(minX, maxX)),
                         std::clamp(anchorY + 12.0F, minY, std::max(minY, maxY))};
    const ImVec2 pillMax{pillMin.x + pillWidth, pillMin.y + pillHeight};

    ImGui::SetCursorScreenPos(pillMin);
    ImGui::PushID("##viewport-entity-context-toolbar");
    const bool pressed = ImGui::InvisibleButton("##scripting-pill", ImVec2{pillWidth, pillHeight});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(pillMin, pillMax, hovered ? IM_COL32(64, 132, 214, 235) : IM_COL32(18, 21, 28, 220),
                            pillHeight * 0.5F);
    drawList->AddRect(pillMin, pillMax, IM_COL32(255, 255, 255, 32), pillHeight * 0.5F);
    drawList->AddText(ImVec2{pillMin.x + kPaddingXUVE, pillMin.y + kPaddingYUVE}, IM_COL32(240, 243, 248, 255),
                      kLabel);

    if (pressed) {
        static_cast<void>(OpenScriptGraphForEntityUVE(entity));
        m_viewportOverlayState.entityContextToolbarOpen = false;
    }
}

} // namespace UVE::Editor

