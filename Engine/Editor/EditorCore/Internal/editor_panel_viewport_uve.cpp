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

#include "editor_color_field_uve.h"
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
#include "uve/math/color_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/preview_lighting_uve.h"
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

/// The 2D/3D switch's mark: a square seen flat, with a horizon through it. Reads as a canvas
/// rather than as a cube, because the button says which view you are IN.
void DrawViewportModeIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.72F;
    drawList->AddRect(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half}, color,
                      1.5F, 0, 1.4F);
    drawList->AddLine(ImVec2{center.x - half, center.y}, ImVec2{center.x + half, center.y}, color, 1.2F);
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

    // No dockspace. Every panel below computes its rect from ComputeEditorChromeLayoutUVE() and
    // pins itself there with ImGuiCond_Always; there is deliberately no second layout authority that
    // could disagree with it. The editor frame - top bar, Outliner, viewport, Inspector, dock - is
    // fixed, and the only things that move are the seams and the two hidable side panels.
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
        // Last, so the grips sit over the seam between two panels and the restore strips sit over
        // the edge a hidden panel left behind. Drawn first, they would be buried under both.
        DrawEditorChromeHandlesUVE();
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
    if (m_montage.has_value()) {
        DrawMontageUVE();
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
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible, m_inspectorPanelVisible);
    // Always: this rect is recomputed from the chrome layout every frame and the viewport never
    // floats, so there is nothing for ImGui to remember and nothing a stale imgui.ini may override.
    ImGui::SetNextWindowPos(layout.viewportPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.viewportSize, ImGuiCond_Always);
    // Zero interior padding for the viewport window so the rendered 3D image fills the panel
    // edge-to-edge (the reference has the grid reaching all four edges with the toolbar floating on
    // top); with the theme's default WindowPadding the GL image is inset ~8px all around, leaving an
    // empty border band. Only this window opts out - the overlay bubbles still float on top since
    // they anchor off the image origin, which now sits flush in the panel corner.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    // No title row and no close button. The viewport is the canvas: the header strip above already
    // names it and carries the transport, so a second title row cost a line of image and - worse -
    // put a close X directly under the top bar, where it read as an X on the top bar itself. The
    // viewport is fixed, not hidable; there is no state in which hiding the canvas is useful.
    if (!ImGui::Begin(kPanelLabelViewportUVE, nullptr,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
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
        // Which overlays this view is entitled to, decided by the caller that knows what it is
        // drawing rather than guessed from state that is swapped underneath it.
        state->entityAuthoringView = context == ViewportContextUVE::EntityEditor;
        state->bones.clear();
        state->colliderLines.clear();
        if (!state->gameWorkspaceActive) {
            BuildSkeletonOverlayUVE(state->bones);
            BuildColliderOverlayUVE(state->colliderLines);
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
            ImGui::Dummy(ImVec2{0.0F, 4.0F});
            ImGui::TextDisabled("SHOW");
            // The diagnostic overlays, one checkbox each. Collision shapes live here rather than in
            // Preferences because it is a question about THIS view of THIS scene ("is the capsule
            // where I think it is?"), asked for a minute and then turned off.
            bool collisionShapes = m_viewportOverlayState.collisionShapesVisible;
            if (ImGui::Checkbox("Collision shapes", &collisionShapes)) {
                static_cast<void>(SetViewportCollisionShapesUVE(collisionShapes));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Draw every collider as its real shape: box, sphere, capsule, cylinder.");
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

    // ---- No physics pill ----------------------------------------------------------------
    // There used to be a "Physics: ON / OFF" pill here, and it was the wrong shape for the job in
    // three separate ways.
    //
    // It was a *global* switch wearing a *per-viewport* face: it wrote the one engine-wide
    // simulation mode, so pressing it in the Entity Editor also froze the level viewport behind
    // it and the running game itself. It also lied about which way was safe - the editor used to
    // open with the simulation already Running, so the pill showed "ON" and the first thing an
    // author saw was their character falling before they had touched anything. And because
    // nothing in edit mode kept a snapshot, that fall was permanent: Play has PlayModeSessionUVE
    // to restore, the editor had nothing, and a simulation-driven transform is not the kind of
    // change undo can own.
    //
    // The answer is not a better pill. It is that an editor does not simulate. The clock is off in
    // edit (EditorUVE::Init), Play turns it on, Stop turns it back off - see StopPlayModeUVE,
    // which now restores Paused rather than the Running it used to. With no state for an author to
    // get wrong, there is no control to offer here, so the pill is gone rather than disabled.
    //
    // Animation is unaffected: pose, blend, IK and skinning moved to an unconditional authoring
    // pass in EngineCoreUVE::Update, so pausing the clock no longer takes the authoring tools with
    // it.

    // ---- Preview Sun & Environment (Godot 4 parity) -----------------------------------------
    // Godot's 3D viewport has exactly three controls here: a sun toggle, an environment toggle and
    // a "three dots" menu for their settings (docs: tutorials/3d/introduction_to_3d, "Preview
    // environment and light"; introduced in godotengine/godot PR #46315). The preview is
    // editor-only - it is never written into the scene, never saved and never runs - and it is
    // valid only while the scene has no DirectionalLight3D / WorldEnvironment of its own.
    //
    // The previous single "Preview" pill hid both switches behind a label that named neither of
    // them, so the only way to find out what it did was to click it and watch. Three explicit
    // controls need no explanation: a sun, a sky, and the menu that edits both.
    constexpr float kPreviewToggleWidthUVE = 26.0F;
    constexpr float kPreviewToggleGapUVE = 2.0F;
    float previewCursorX = shadingPillMax.x + kBubbleGapUVE;
    const bool levelOnlyOverlays = !m_viewportOverlayState.entityAuthoringView;
    const auto previewToggle = [&](const char* const id, const bool active, const char* const tooltip,
                                   const auto& drawIcon) {
        if (!levelOnlyOverlays) {
            return false;
        }
        const ImVec2 minimum{previewCursorX, topY};
        const ImVec2 maximum{minimum.x + kPreviewToggleWidthUVE, minimum.y + unifiedHeight};
        ImGui::SetCursorScreenPos(minimum);
        const bool pressed = ImGui::InvisibleButton(id, ImVec2{kPreviewToggleWidthUVE, unifiedHeight});
        const bool hovered = ImGui::IsItemHovered();
        drawList->AddRectFilled(minimum, maximum,
                                active ? IM_COL32(45, 75, 120, 235)
                                       : (hovered ? IM_COL32(30, 34, 44, 225) : IM_COL32(18, 21, 28, 205)),
                                unifiedHeight * 0.5F);
        drawList->AddRect(minimum, maximum,
                          active ? IM_COL32(96, 164, 246, 190) : IM_COL32(255, 255, 255, 24),
                          unifiedHeight * 0.5F);
        drawIcon(ImVec2{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F});
        if (hovered) {
            ImGui::SetTooltip("%s", tooltip);
        }
        previewCursorX = maximum.x + kPreviewToggleGapUVE;
        return pressed;
    };

    if (previewToggle("##preview-sun-toggle", m_viewportOverlayState.previewSun,
                      m_viewportOverlayState.previewSun
                          ? "Preview Sun: ON (editor only)\nClick to turn it off"
                          : "Preview Sun: OFF\nClick to light the scene while it has no DirectionalLight3D",
                      [drawList](const ImVec2 center) {
                          constexpr float kSunRadiusUVE = 4.4F;
                          drawList->AddCircle(center, kSunRadiusUVE, IM_COL32(240, 226, 180, 255), 16, 1.6F);
                          for (int ray = 0; ray < 8; ++ray) {
                              const float angle = static_cast<float>(ray) * 0.785398163F;
                              const ImVec2 from{center.x + (std::cos(angle) * (kSunRadiusUVE + 2.0F)),
                                                center.y + (std::sin(angle) * (kSunRadiusUVE + 2.0F))};
                              const ImVec2 to{center.x + (std::cos(angle) * (kSunRadiusUVE + 4.0F)),
                                              center.y + (std::sin(angle) * (kSunRadiusUVE + 4.0F))};
                              drawList->AddLine(from, to, IM_COL32(240, 226, 180, 215), 1.3F);
                          }
                      })) {
        m_viewportOverlayState.previewSun = !m_viewportOverlayState.previewSun;
    }
    if (previewToggle("##preview-environment-toggle", m_viewportOverlayState.previewEnvironment,
                      m_viewportOverlayState.previewEnvironment
                          ? "Preview Environment: ON (editor only)\nClick to turn it off"
                          : "Preview Environment: OFF\nClick to show a sky while the scene has no WorldEnvironment",
                      [drawList](const ImVec2 center) {
                          constexpr float kSkyRadiusUVE = 5.6F;
                          drawList->AddCircle(center, kSkyRadiusUVE, IM_COL32(168, 200, 238, 230), 20, 1.3F);
                          drawList->AddLine(ImVec2{center.x - kSkyRadiusUVE, center.y},
                                            ImVec2{center.x + kSkyRadiusUVE, center.y},
                                            IM_COL32(168, 200, 238, 230), 1.3F);
                          drawList->PathArcTo(center, kSkyRadiusUVE - 0.6F, 0.0F, 3.14159265F, 18);
                          drawList->PathFillConvex(IM_COL32(96, 140, 196, 110));
                      })) {
        m_viewportOverlayState.previewEnvironment = !m_viewportOverlayState.previewEnvironment;
    }
    const bool previewMenuPressed = previewToggle(
        "##preview-sun-env-menu", m_viewportOverlayState.previewSun || m_viewportOverlayState.previewEnvironment,
        "Sun and Environment settings", [drawList](const ImVec2 center) {
            for (int dot = -1; dot <= 1; ++dot) {
                drawList->AddCircleFilled(ImVec2{center.x + (static_cast<float>(dot) * 4.6F), center.y}, 1.5F,
                                          IM_COL32(214, 220, 230, 235), 8);
            }
        });
    const ImVec2 previewPillMax{previewCursorX, topY + unifiedHeight};
    if (previewMenuPressed) {
        ImGui::OpenPopup("##preview-sun-env-popup");
    }
    if (ImGui::BeginPopup("##preview-sun-env-popup")) {
        const float fieldWidth = ImGui::GetFontSize() * 11.0F;

        ImGui::TextDisabled("PREVIEW SUN");
        ImGui::Checkbox("Enable preview sun##preview-sun", &m_viewportOverlayState.previewSun);
        ImGui::BeginDisabled(!m_viewportOverlayState.previewSun);
        // The preview colours are the author's colours too, so they open the same picker as every
        // other colour row instead of the plain ImGui one.
        EditorColorUVE sunColor{m_viewportOverlayState.previewSunColor[0],
                                m_viewportOverlayState.previewSunColor[1],
                                m_viewportOverlayState.previewSunColor[2], 1.0F};
        if (DrawColorFieldUVE("##previewSunColor", "Color", sunColor, false, m_colorPickerPreferences) !=
            ColorFieldEventUVE::None) {
            m_viewportOverlayState.previewSunColor = {sunColor.r, sunColor.g, sunColor.b};
        }
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::SliderFloat("Energy##preview-sun-energy", &m_viewportOverlayState.previewSunEnergy, 0.0F, 8.0F,
                           "%.2f");
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::SliderFloat("Azimuth##preview-sun-azimuth", &m_viewportOverlayState.previewSunAzimuth, -180.0F,
                           180.0F, "%.0f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Bearing around the world Y axis. Negative is west, positive is east.");
        }
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::SliderFloat("Altitude##preview-sun-altitude", &m_viewportOverlayState.previewSunAltitude, -90.0F,
                           90.0F, "%.0f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Elevation above the horizon. 90 lights the scene from straight above.");
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        if (ImGui::Button("Add Sun to Scene", ImVec2{-1.0F, 0.0F})) {
            Scene::EntityUVE sunEntity =
                FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
            if (sunEntity == Scene::kInvalidEntityUVE) {
                sunEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
            }
            if (sunEntity != Scene::kInvalidEntityUVE) {
                Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                // The preview's own solar math, so the node that lands in the scene points exactly
                // where the light the author was looking at pointed.
                const std::optional<Math::QuaternionUVE> rotation =
                    Scene::TryMakePreviewSunRotationUVE(m_viewportOverlayState.previewSunAzimuth,
                                                        m_viewportOverlayState.previewSunAltitude);
                if (rotation.has_value() && entityManager.HasComponentUVE<Scene::TransformComponentUVE>(sunEntity)) {
                    auto transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(sunEntity);
                    transform.localRotation = *rotation;
                    m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, sunEntity, transform);
                }
                if (entityManager.HasComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity)) {
                    auto& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity);
                    // Display-authored swatch, converted on the way into linear storage - the same
                    // rule the preview light itself follows, so copying it into the scene is lossless.
                    emitter.color = Math::ColorFromDisplayUVE(
                        Math::Vector3UVE{m_viewportOverlayState.previewSunColor[0],
                                         m_viewportOverlayState.previewSunColor[1],
                                         m_viewportOverlayState.previewSunColor[2]});
                    emitter.energy = m_viewportOverlayState.previewSunEnergy;
                    emitter.shadowEnabled = true;
                }
                m_sceneDirty = true;
                SelectEntityUVE(sunEntity);
                m_contentStatusMessage = "Added a DirectionalLight3D from the preview sun.";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Creates a real DirectionalLight3D carrying these values. The preview then steps aside.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("PREVIEW ENVIRONMENT");
        ImGui::Checkbox("Enable preview environment##preview-env", &m_viewportOverlayState.previewEnvironment);
        ImGui::BeginDisabled(!m_viewportOverlayState.previewEnvironment);
        EditorColorUVE skyColor{m_viewportOverlayState.previewSkyColor[0],
                                m_viewportOverlayState.previewSkyColor[1],
                                m_viewportOverlayState.previewSkyColor[2], 1.0F};
        if (DrawColorFieldUVE("##previewSkyColor", "Sky", skyColor, false, m_colorPickerPreferences) !=
            ColorFieldEventUVE::None) {
            m_viewportOverlayState.previewSkyColor = {skyColor.r, skyColor.g, skyColor.b};
        }
        EditorColorUVE horizonColor{m_viewportOverlayState.previewHorizonColor[0],
                                    m_viewportOverlayState.previewHorizonColor[1],
                                    m_viewportOverlayState.previewHorizonColor[2], 1.0F};
        if (DrawColorFieldUVE("##previewHorizonColor", "Horizon", horizonColor, false,
                              m_colorPickerPreferences) != ColorFieldEventUVE::None) {
            m_viewportOverlayState.previewHorizonColor = {horizonColor.r, horizonColor.g, horizonColor.b};
        }
        EditorColorUVE groundColor{m_viewportOverlayState.previewGroundColor[0],
                                   m_viewportOverlayState.previewGroundColor[1],
                                   m_viewportOverlayState.previewGroundColor[2], 1.0F};
        if (DrawColorFieldUVE("##previewGroundColor", "Ground", groundColor, false, m_colorPickerPreferences) !=
            ColorFieldEventUVE::None) {
            m_viewportOverlayState.previewGroundColor = {groundColor.r, groundColor.g, groundColor.b};
        }
        ImGui::SetNextItemWidth(fieldWidth);
        ImGui::SliderFloat("Ambient##preview-env-ambient", &m_viewportOverlayState.previewAmbientEnergy, 0.0F,
                           4.0F, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("How much of the sky and ground band lights the scene. Zero is a black sky.");
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        if (ImGui::Button("Add Environment to Scene", ImVec2{-1.0F, 0.0F})) {
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
                    env.ambientEnergy = m_viewportOverlayState.previewAmbientEnergy;
                    // Exactly what the author was looking at. The preview draws no effects stack,
                    // so an environment that switched them on here would change the picture at the
                    // moment of adding - the one thing "Add Environment to Scene" must not do.
                    env.postProcessingEnabled = false;
                }
                m_sceneDirty = true;
                SelectEntityUVE(envEntity);
                m_contentStatusMessage = "Added a WorldEnvironment3D from the preview environment.";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Creates a real WorldEnvironment3D carrying these values. The preview then steps aside.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Editor only: the preview is never saved with the scene\nand never runs in the game.");
        ImGui::EndPopup();
    }

    // ---- 2D / 3D Mode Switch & 2D Viewport Options ------------------------------------------
    // An icon bubble like the grid and gizmo controls beside it, not a wide text pill. The pill
    // was the only control in this row that spelled itself out, so it sat there as a different
    // kind of thing and pushed everything after it out of line.
    const float modeBubbleDiameter = kViewportBubbleIconRadiusUVE * 2.0F;
    const ImVec2 modeBubbleMin{previewPillMax.x + kBubbleGapUVE,
                               topY + ((unifiedHeight - modeBubbleDiameter) * 0.5F)};
    if (levelOnlyOverlays) {
        ImGui::SetCursorScreenPos(modeBubbleMin);
        if (DrawViewportBubbleIconButtonUVE("##viewport-2d-mode-btn", m_viewportOverlayState.is2DMode,
                                            DrawViewportModeIconUVE)) {
            m_viewportOverlayState.is2DMode = !m_viewportOverlayState.is2DMode;
        }
        // Click switches; right-click opens the 2D tool's own options, the same split the grid
        // button uses so a toolbar stays one control wide per idea.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && m_viewportOverlayState.is2DMode) {
            ImGui::OpenPopup("##viewport-2d-options-popup");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) &&
            !ImGui::IsPopupOpen("##viewport-2d-options-popup")) {
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

