// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The top menu bar with its playback transport, the plugin window, and the bottom dock that hosts
// the debug output, the animator and the content browser.
//
// Split out of editor_uve.cpp as the last of the panel groups. The menu bar is the largest single
// method in the editor at three hundred lines, and it is the one most often touched when a
// feature needs a menu entry - which previously meant opening the same file as the viewport, the
// inspector and everything else.
//
// The bottom dock's debug page reads its rows from editor_render_stats_uve.h, the first file of
// this whole sequence: the formatting and the judgement of what counts as concerning live there,
// under test, and this file only prints them.
//
// Moved verbatim. Not one line of the four functions differs from what was in editor_uve.cpp.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_color_field_uve.h"
#include "editor_fonts_uve.h"
#include "editor_object_icons_uve.h"

#include "uve/diagnostics/profiler_view_uve.h"
#include "uve/editor/editor_render_stats_uve.h"

namespace UVE::Editor {
namespace {

constexpr const char* kMenuLabelFileUVE = "\xEE\xAA\xA4 File";
constexpr const char* kMenuLabelEditUVE = "\xEE\xAA\x98 Edit";
constexpr const char* kMenuLabelAssetsUVE = "\xEE\xA9\x85 Assets";
constexpr const char* kMenuLabelGameObjectUVE = "\xEF\xAA\x97 GameObject";
constexpr const char* kMenuLabelPluginUVE = "\xEE\xAF\x99 Plugin";
constexpr const char* kMenuLabelWindowUVE = "\xEE\xB6\xBA Window";
constexpr const char* kMenuLabelHelpUVE = "\xEF\xA4\x9D Help";

} // namespace

// Three colour rows plus a reset, editing the axis hues the viewport draws its gizmo and grid
// with. The same colour field as every other colour row in the editor, so the picker behaves
// identically wherever a colour is edited. Applied live, since these are preferences with no undo;
// Cancel in the picker puts the previous hue back.
//
// Reads back through GetViewportAxisColorUVE each frame rather than keeping its own copy: the
// value can also change underneath this menu when session settings load, and a cached copy would
// show the author a colour the viewport is no longer using.
// The outline drawn around selected meshes in the viewport: whether it shows, its colour (the same
// colour field as every other colour in the editor) and its thickness. Preferences, applied live.
void EditorUVE::DrawViewportSelectionOutlineMenuUVE() {
    bool visible = m_viewportOverlayState.selectionOutlineVisible;
    ViewportAxisColorUVE color = m_viewportOverlayState.selectionOutlineColor;
    int thickness = static_cast<int>(std::lround(m_viewportOverlayState.selectionOutlineThickness));
    bool changed = ImGui::Checkbox("Show outline", &visible);

    const float labelColumn = ImGui::GetFontSize() * 5.5F;
    ImGui::BeginDisabled(!visible);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Colour");
    ImGui::SameLine(labelColumn);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    EditorColorUVE picked{color.r, color.g, color.b, 1.0F};
    if (DrawColorFieldUVE("##selection-outline-color", "Selection outline", picked, false, m_colorPickerPreferences) !=
        ColorFieldEventUVE::None) {
        color = ViewportAxisColorUVE{picked.r, picked.g, picked.b};
        changed = true;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Thickness");
    ImGui::SameLine(labelColumn);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    changed |= ImGui::SliderInt("##selection-outline-thickness", &thickness,
                                static_cast<int>(kMinimumSelectionOutlineThicknessUVE),
                                static_cast<int>(kMaximumSelectionOutlineThicknessUVE), "%d px",
                                ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::MenuItem("Reset to defaults")) {
        const ViewportOverlayStateUVE defaults{};
        visible = defaults.selectionOutlineVisible;
        color = defaults.selectionOutlineColor;
        thickness = static_cast<int>(defaults.selectionOutlineThickness);
        changed = true;
    }
    if (changed) {
        static_cast<void>(SetViewportSelectionOutlineUVE(visible, color, static_cast<float>(thickness)));
    }
}

void EditorUVE::DrawViewportAxisColorPickerUVE() {
    if (!AreViewportAxisColorsSetUVE()) {
        // The host seeds the real defaults at startup; before that there is nothing true to show,
        // and inventing a placeholder here would make this a second home for the default hues.
        ImGui::TextDisabled("Viewport not ready");
        return;
    }

    struct AxisRowUVE {
        const char* label;
        const char* id;
    };
    constexpr std::array<AxisRowUVE, 3> rows{{
        {"X axis", "##viewport-axis-color-x"},
        {"Y axis", "##viewport-axis-color-y"},
        {"Z axis", "##viewport-axis-color-z"},
    }};

    std::array<ViewportAxisColorUVE, 3> colors{GetViewportAxisColorUVE(0), GetViewportAxisColorUVE(1),
                                               GetViewportAxisColorUVE(2)};
    bool edited = false;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(rows[index].label);
        ImGui::SameLine(ImGui::GetFontSize() * 4.0F);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
        EditorColorUVE color{colors[index].r, colors[index].g, colors[index].b, 1.0F};
        if (DrawColorFieldUVE(rows[index].id, rows[index].label, color, false, m_colorPickerPreferences) !=
            ColorFieldEventUVE::None) {
            colors[index] = ViewportAxisColorUVE{color.r, color.g, color.b};
            edited = true;
        }
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Reset to defaults")) {
        // Cleared rather than overwritten with values named here: dropping the valid flag makes
        // the host re-seed its own palette on the next frame, which keeps the defaults in the one
        // place that owns them.
        ResetViewportAxisColorsUVE();
        return;
    }

    if (edited) {
        static_cast<void>(SetViewportAxisColorsUVE(colors[0], colors[1], colors[2]));
    }
}

void EditorUVE::DrawMenuBarUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    // Every keyboard shortcut is a command's (editor_commands_uve.cpp), rebindable in Keyboard
    // Shortcuts.
    DispatchEditorShortcutsUVE();

    // The two chrome strips never come to the front when clicked: they tile against the panels
    // below them, and a strip drawn over a panel hides that panel's title row.
    constexpr ImGuiWindowFlags chromeFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoScrollWithMouse |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus;
    // Dear ImGui clamps every window to style.WindowMinSize (32px by default), which silently made
    // the 24px title bar and the 26px tool row taller than their slots, overlapping the panels'
    // title rows. The strips are exactly their declared height.
    const auto beginChromeWindow = [](const char* const id, const ImGuiWindowFlags flags) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{1.0F, 1.0F});
        const bool open = ImGui::Begin(id, nullptr, flags);
        ImGui::PopStyleVar();
        return open;
    };

    ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2{mainViewport->WorkSize.x, kEditorTitleBarHeightUVE}, ImGuiCond_Always);
    if (beginChromeWindow("##uve-titlebar", chromeFlags | ImGuiWindowFlags_MenuBar)) {
        ImDrawList* const titleDrawList = ImGui::GetWindowDrawList();
        const ImVec2 titleMin = ImGui::GetWindowPos();
        const ImVec2 titleMax{titleMin.x + ImGui::GetWindowWidth(), titleMin.y + kEditorTitleBarHeightUVE};
        titleDrawList->AddRectFilled(titleMin, titleMax, IM_COL32(17, 21, 26, 255));
        titleDrawList->AddLine(ImVec2{titleMin.x, titleMax.y - 1.0F}, ImVec2{titleMax.x, titleMax.y - 1.0F},
                               IM_COL32(48, 55, 64, 235), 1.0F);
        // Everything in this row - badge, workspace/saved status, the "Menu" dropdown, and the
        // version text - lives inside one real ImGui menu-bar region rather than plain window
        // content: a menu bar's own top-of-window reserved strip is the only content area this
        // 24px-tall window actually has room for once ImGuiWindowFlags_MenuBar is set, so
        // everything has to share that one strip instead of stacking above/below it.
        if (!ImGui::BeginMenuBar()) {
            ImGui::End();
            return;
        }
        // The logo's box covers the corner (drawn after this row); the row starts beside it.
        ImGui::Dummy(ImVec2{kEditorTopChromeHeightUVE - ImGui::GetCursorPosX() + 4.0F, 1.0F});
        ImGui::SameLine(0.0F, 0.0F);

        // File/Edit/Assets/GameObject/Plugin/Window/Help all fold into one "Menu" dropdown here in
        // the title bar, replacing what used to be a separate always-visible menu-bar row below it
        // - one fewer chrome strip, and the seven items only take screen space while actually open.
        // Every item's own body/callback is unchanged from before; only the nesting level moved.
        if (ImGui::BeginMenu("Menu")) {
            if (ImGui::BeginMenu(kMenuLabelFileUVE)) {
                DrawCommandMenuItemUVE("file.saveScene");
                DrawCommandMenuItemUVE("file.saveAll");
                DrawCommandMenuItemUVE("file.loadScene");
                ImGui::Separator();
                // The viewport's X/Y/Z hues, edited here rather than in a viewport-anchored popup:
                // a second ImGui window floating over the 3D view would take hover away from
                // camera orbit and pan, which is exactly what ViewportOverlayStateUVE's
                // pointerOverOverlay guard exists to prevent. The menu bar is outside that panel,
                // so it cannot fight the camera at all.
                //
                // Only the gizmo's colours are offered. The grid's axis lines are derived from
                // them (a darker variant) inside the viewport, so there is nothing here to let
                // the two drift apart.
                if (ImGui::BeginMenu("Viewport Axis Colours")) {
                    DrawViewportAxisColorPickerUVE();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Selection Outline")) {
                    DrawViewportSelectionOutlineMenuUVE();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                DrawCommandMenuItemUVE("file.projectSettings");
                DrawCommandMenuItemUVE("file.inputMap");
                DrawCommandMenuItemUVE("file.preferences");
                DrawCommandMenuItemUVE("file.keyboardShortcuts");
                DrawCommandMenuItemUVE("file.commandPalette");
                if (ImGui::MenuItem("Save Editor Preferences")) {
                    static_cast<void>(SaveSessionSettingsUVE());
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelEditUVE)) {
                DrawCommandMenuItemUVE("edit.undo");
                DrawCommandMenuItemUVE("edit.redo");
                ImGui::Separator();
                DrawCommandMenuItemUVE("edit.duplicate");
                DrawCommandMenuItemUVE("edit.delete");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelAssetsUVE)) {
                if (ImGui::MenuItem("Open Project Browser")) {
                    m_activeBottomDock = EditorBottomDockUVE::FileSystem;
                    m_bottomDockVisible = true;
                }
                ImGui::MenuItem("Import Queue", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelGameObjectUVE)) {
                DrawCommandMenuItemUVE("create.empty");
                DrawCommandMenuItemUVE("create.cube");
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(kMenuLabelPluginUVE)) {
                m_pluginWindowVisible = true;
            }
            if (ImGui::BeginMenu(kMenuLabelWindowUVE)) {
                // The Viewport and the top bar are not on this list on purpose: they are the canvas
                // and the frame everything else is arranged around, so there is no state in which
                // hiding them leaves an editor you can still work in. The three below are regions
                // beside that canvas, each with a visible way back once hidden.
                ImGui::MenuItem("Outliner", nullptr, &m_scenePanelVisible);
                ImGui::MenuItem("Inspector", nullptr, &m_inspectorPanelVisible);
                ImGui::MenuItem("Content Browser + Debug Dock", nullptr, &m_bottomDockVisible);
                ImGui::MenuItem("Plugin Tools", nullptr, &m_pluginWindowVisible);
                ImGui::Separator();
                if (ImGui::MenuItem("Default Layout")) {
                    ApplyLayoutPresetUVE(EditorLayoutPresetUVE::Default);
                }
                if (ImGui::MenuItem("Focus Viewport")) {
                    ApplyLayoutPresetUVE(EditorLayoutPresetUVE::FocusViewport);
                }
                if (ImGui::MenuItem("Content Review")) {
                    ApplyLayoutPresetUVE(EditorLayoutPresetUVE::ContentReview);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Scene Workspace", nullptr,
                                    m_activeWorkspace == EditorWorkspaceUVE::Library)) {
                    m_activeWorkspace = EditorWorkspaceUVE::Library;
                }
                if (ImGui::MenuItem("Scripting Workspace", nullptr,
                                    m_activeWorkspace == EditorWorkspaceUVE::Scripting)) {
                    m_activeWorkspace = EditorWorkspaceUVE::Scripting;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelHelpUVE)) {
                ImGui::MenuItem("UVE Editor Reference", nullptr, false, false);
                ImGui::MenuItem("About UNIVEX Engine", nullptr, false, false);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::SameLine(0.0F, 10.0F);

        {
            // A small colored status dot before the saved/unsaved label, matching Cowork's
            // mockup `.tb-dot.saved`/`.tb-dot.unsaved` (--success #5fc98a / --warning #e0b13f
            // exact hex) - ImGui's plain TextDisabled call here previously had no color coding.
            const ImVec2 dotOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 dotCenter{dotOrigin.x + 5.0F, dotOrigin.y + ImGui::GetTextLineHeight() * 0.5F};
            ImGui::GetWindowDrawList()->AddCircleFilled(
                dotCenter, 2.5F, m_sceneDirty ? IM_COL32(224, 177, 63, 255) : IM_COL32(95, 201, 138, 255));
            ImGui::Dummy(ImVec2{11.0F, ImGui::GetTextLineHeight()});
        }
        ImGui::SameLine(0.0F, 4.0F);
        ImGui::TextDisabled("%s | %zu selected | %s", m_sceneDirty ? "unsaved" : "saved",
                            m_selectedEntities.size(),
                            m_playModeState == EditorPlayModeStateUVE::Edit
                                ? "edit"
                                : (m_playModeState == EditorPlayModeStateUVE::Paused ? "paused" : "playing"));
        ImGui::SameLine(ImGui::GetWindowWidth() - 220.0F);
        ImGui::TextDisabled("UVE Editor 0.1");
        ImGui::EndMenuBar();
        ImGui::End();
    }

    // The header row. Over the viewport it is the viewport's own header: its name, then the play
    // controls, then room for the editor's tools. A workspace that fills the window (Scripting, an
    // open Entity Editor, the Retarget preview) gets a plain full-width strip with the play controls.
    const bool sidePanelsShown = m_activeWorkspace != EditorWorkspaceUVE::Scripting && !m_entityEditSession.has_value() &&
                                 !m_retargetPreview.has_value();
    const EditorChromeLayoutUVE stripLayout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible,
                                     m_inspectorPanelVisible);
    const ImVec2 stripPos = sidePanelsShown ? stripLayout.viewportHeaderPos
                                            : ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + kEditorTitleBarHeightUVE};
    const ImVec2 stripSize = sidePanelsShown ? stripLayout.viewportHeaderSize : ImVec2{mainViewport->WorkSize.x, kEditorToolbarHeightUVE};
    ImGui::SetNextWindowPos(stripPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(stripSize, ImGuiCond_Always);
    if (beginChromeWindow("##uve-tool-row", chromeFlags)) {
        ImDrawList* const toolbarDrawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarMin = ImGui::GetWindowPos();
        const ImVec2 toolbarMax{toolbarMin.x + ImGui::GetWindowWidth(), toolbarMin.y + kEditorToolbarHeightUVE};
        toolbarDrawList->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(32, 37, 43, 255));
        toolbarDrawList->AddLine(ImVec2{toolbarMin.x, toolbarMax.y - 1.0F}, ImVec2{toolbarMax.x, toolbarMax.y - 1.0F},
                                 IM_COL32(48, 55, 64, 235), 1.0F);

        constexpr float kTransportButtonWidthUVE = 32.0F;
        constexpr float kTransportButtonHeightUVE = 20.0F;
        constexpr float kTransportButtonGapUVE = 2.0F;
        constexpr float kStripPaddingUVE = 8.0F;
        float cursorX = toolbarMin.x + kStripPaddingUVE;
        // ---- The viewport's name, as its own tab: the tab's edge is what separates it -----------
        if (sidePanelsShown) {
            // The open Viewport asset (the level): the same name and icon as its Outliner row and its
            // file in the Content Browser.
            const std::string stem = m_activeScenePath.stem().string();
            const std::string name = stem.empty() ? std::string{"Viewport3D"} : stem;
            constexpr float kTabIconSizeUVE = 16.0F;
            const float tabWidth = ImGui::CalcTextSize(name.c_str()).x + kTabIconSizeUVE + 34.0F;
            const ImVec2 tabMin{toolbarMin.x, toolbarMin.y + 3.0F};
            const ImVec2 tabMax{toolbarMin.x + tabWidth, toolbarMax.y};
            toolbarDrawList->AddRectFilled(tabMin, tabMax, IM_COL32(21, 25, 31, 255), 4.0F, ImDrawFlags_RoundCornersTop);
            const float midY = (tabMin.y + tabMax.y) * 0.5F;
            // The level's file icon, exactly the one the Content Browser puts on the .scene asset:
            // the tab names a file, so it wears the file's icon rather than the object-type icon
            // the Outliner uses for the same thing. The object icon is the fallback.
            std::uintptr_t icon = m_uiAssets.GetContentTypeIconTextureIdUVE(
                GetContentBrowserItemTypeLabelUVE(ContentBrowserItemTypeUVE::Scene));
            if (icon == 0U) {
                icon = m_uiAssets.GetObjectIconTextureIdUVE(Scene::Objects::SceneObjectKindUVE::Viewport3D);
            }
            if (icon != 0U) {
                toolbarDrawList->AddImage(static_cast<ImTextureID>(icon), ImVec2{tabMin.x + 12.0F, midY - kTabIconSizeUVE * 0.5F},
                                          ImVec2{tabMin.x + 12.0F + kTabIconSizeUVE, midY + kTabIconSizeUVE * 0.5F});
            }
            toolbarDrawList->AddText(ImVec2{tabMin.x + 18.0F + kTabIconSizeUVE, midY - ImGui::GetTextLineHeight() * 0.5F},
                                     ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            cursorX = tabMax.x + 8.0F;
        }
        // ---- Play/Pause/Stop -----------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2{cursorX, toolbarMin.y + (kEditorToolbarHeightUVE - kTransportButtonHeightUVE) * 0.5F});
        const auto drawTransportButton = [](const char* const id, const bool enabled, const auto& drawIcon) {
            ImGui::BeginDisabled(!enabled);
            ImGui::PushID(id);
            const bool pressed =
                ImGui::Button("##transport", ImVec2{kTransportButtonWidthUVE, kTransportButtonHeightUVE});
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const ImVec2 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F};
            drawIcon(*ImGui::GetWindowDrawList(), center);
            ImGui::PopID();
            ImGui::EndDisabled();
            return pressed;
        };

        const bool canEnterPlayModeNow =
            m_simulationControl != nullptr && m_playModeState == EditorPlayModeStateUVE::Edit;
        const bool playEnabled = m_playModeState == EditorPlayModeStateUVE::Edit ? canEnterPlayModeNow : true;
        // Eased toward 0 (plain Play triangle) or 1 (Pause bars) each frame instead of an instant
        // swap, so the icon genuinely animates from one shape into the other on toggle.
        const float morphTarget = m_playModeState == EditorPlayModeStateUVE::Playing ? 1.0F : 0.0F;
        constexpr float kPlayButtonMorphSpeedUVE = 8.0F; // full swing in ~125ms
        const float morphStep = ImGui::GetIO().DeltaTime * kPlayButtonMorphSpeedUVE;
        if (m_playButtonMorphProgress < morphTarget) {
            m_playButtonMorphProgress = std::min(morphTarget, m_playButtonMorphProgress + morphStep);
        } else if (m_playButtonMorphProgress > morphTarget) {
            m_playButtonMorphProgress = std::max(morphTarget, m_playButtonMorphProgress - morphStep);
        }
        const float morph = m_playButtonMorphProgress;
        if (drawTransportButton(
                "##transport-play", playEnabled, [morph](ImDrawList& drawList, const ImVec2 center) {
                    const ImU32 baseColor = ImGui::GetColorU32(ImGuiCol_Text);
                    // Triangle (Play) crossfades into two bars (Pause): the triangle fades out while
                    // its apex pulls inward, and the bars fade in while sliding apart, so the two
                    // shapes read as one continuous motion rather than an instant swap.
                    if (morph < 1.0F) {
                        const ImU32 triColor =
                            (baseColor & 0x00FFFFFFU) |
                            (static_cast<ImU32>((1.0F - morph) * 255.0F) << IM_COL32_A_SHIFT);
                        const float apexPull = morph * 4.0F;
                        drawList.AddTriangleFilled(ImVec2{center.x - 5.0F, center.y - 6.0F},
                                                   ImVec2{center.x - 5.0F, center.y + 6.0F},
                                                   ImVec2{center.x + 5.0F - apexPull, center.y}, triColor);
                    }
                    if (morph > 0.0F) {
                        const ImU32 barColor = (baseColor & 0x00FFFFFFU) |
                                               (static_cast<ImU32>(morph * 255.0F) << IM_COL32_A_SHIFT);
                        const float spread = morph * 2.0F;
                        drawList.AddRectFilled(ImVec2{center.x - 6.0F - spread, center.y - 6.0F},
                                               ImVec2{center.x - 2.0F - spread, center.y + 6.0F}, barColor);
                        drawList.AddRectFilled(ImVec2{center.x + 2.0F + spread, center.y - 6.0F},
                                               ImVec2{center.x + 6.0F + spread, center.y + 6.0F}, barColor);
                    }
                })) {
            if (m_playModeState == EditorPlayModeStateUVE::Edit) {
                static_cast<void>(EnterPlayModeUVE());
            } else if (m_playModeState == EditorPlayModeStateUVE::Playing) {
                static_cast<void>(PausePlayModeUVE());
            } else {
                static_cast<void>(ResumePlayModeUVE());
            }
        }
        ImGui::SameLine(0.0F, kTransportButtonGapUVE);
        if (drawTransportButton("##transport-stop", m_playModeState != EditorPlayModeStateUVE::Edit,
                                [](ImDrawList& drawList, const ImVec2 center) {
                                    drawList.AddRectFilled(ImVec2{center.x - 5.0F, center.y - 5.0F},
                                                           ImVec2{center.x + 5.0F, center.y + 5.0F},
                                                           ImGui::GetColorU32(ImGuiCol_Text));
                                })) {
            static_cast<void>(StopPlayModeUVE());
        }
        // The editor's tools go after the play controls; kept empty until they move in.
        ImGui::End();
    }

    // The Outliner's header, beside the logo: its name where a title row used to be.
    if (sidePanelsShown && m_scenePanelVisible && stripLayout.outlinerHeaderSize.x > 0.0F) {
        ImGui::SetNextWindowPos(stripLayout.outlinerHeaderPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(stripLayout.outlinerHeaderSize, ImGuiCond_Always);
        if (beginChromeWindow("##uve-outliner-header", chromeFlags)) {
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 max{min.x + ImGui::GetWindowWidth(), min.y + kEditorToolbarHeightUVE};
            drawList->AddRectFilled(min, max, IM_COL32(32, 37, 43, 255));
            drawList->AddLine(ImVec2{min.x, max.y - 1.0F}, ImVec2{max.x, max.y - 1.0F}, IM_COL32(48, 55, 64, 235), 1.0F);
            drawList->AddText(ImVec2{min.x + 8.0F, min.y + (kEditorToolbarHeightUVE - ImGui::GetTextLineHeight()) * 0.5F},
                              ImGui::GetColorU32(ImGuiCol_Text), "Outliner");
            ImGui::End();
        }
    }

    // The logo's box: the top-left corner, as tall as the title bar and the header row together,
    // drawn last so it sits over both. A square mark stands in until the studio's logo file exists.
    ImGui::SetNextWindowPos(stripLayout.logoPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(stripLayout.logoSize, ImGuiCond_Always);
    if (beginChromeWindow("##uve-logo", chromeFlags & ~ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max{min.x + stripLayout.logoSize.x, min.y + stripLayout.logoSize.y};
        drawList->AddRectFilled(min, max, IM_COL32(17, 21, 26, 255));
        drawList->AddLine(ImVec2{max.x - 1.0F, min.y}, ImVec2{max.x - 1.0F, max.y}, IM_COL32(48, 55, 64, 235), 1.0F);
        drawList->AddLine(ImVec2{min.x, max.y - 1.0F}, ImVec2{max.x, max.y - 1.0F}, IM_COL32(48, 55, 64, 235), 1.0F);
        constexpr float kInsetUVE = 7.0F;
        const ImVec2 markMin{min.x + kInsetUVE, min.y + kInsetUVE};
        const ImVec2 markMax{max.x - kInsetUVE, max.y - kInsetUVE};
        drawList->AddRectFilled(markMin, markMax, IM_COL32(66, 120, 184, 255), 6.0F);
        const float fontSize = ImGui::GetFontSize() * 1.6F;
        const ImVec2 markSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, "U");
        drawList->AddText(ImGui::GetFont(), fontSize,
                          ImVec2{(markMin.x + markMax.x - markSize.x) * 0.5F, (markMin.y + markMax.y - markSize.y) * 0.5F},
                          IM_COL32(255, 255, 255, 255), "U");
        if (ImGui::IsWindowHovered()) {
            ImGui::SetTooltip("UniVex Engine");
        }
        ImGui::End();
    }
}

void EditorUVE::DrawPluginWindowUVE() {
    if (!m_pluginWindowVisible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2{340.0F, 0.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2{ImGui::GetMainViewport()->WorkPos.x + 260.0F,
                                   ImGui::GetMainViewport()->WorkPos.y + 104.0F},
                            ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Plugin Tools", &m_pluginWindowVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawNativeIconLabelUVE(m_uiAssets.GetGeneralIconTextureIdUVE("plugin"), "Editor tools");
        ImGui::Separator();
        ImGui::TextDisabled("No editor plugins are currently installed.");
    }
    ImGui::End();
}

void EditorUVE::DrawBottomDockContentUVE() {
    if (!m_bottomDockVisible) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible, m_inspectorPanelVisible);
    // ONE window, and this is the point of the merge. Content, Output, Console, Capture, Profiler
    // and Audio used to be six differently-named windows taking turns being visible. That is a
    // different window rather than a different tab: switching tabs threw away each one's scroll
    // position, and under the docking build any of them could be dragged out of the editor as its
    // own floating frame - which is what "the tab has its own window" looked like. A dock is one
    // place in one editor; only its contents change.
    //
    // Always, never FirstUseEver. The rect comes from the chrome layout every frame and is not
    // something anyone drags: dragging happens on the seams, in DrawEditorChromeHandlesUVE().
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(layout.contentBrowserPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.contentBrowserSize, ImGuiCond_Always);
    // No title row and no close button - the tab strip at the top of the block is what names it and
    // what hides it. NoMove/NoResize/NoSavedSettings keep a stale imgui.ini from parking it anywhere
    // but its computed rect.
    constexpr ImGuiWindowFlags flags = kLayoutOwnedPanelFlagsUVE;
    // Square and borderless, like the tab strip directly above it. The theme draws every panel as a
    // rounded, 1px-outlined card; on a window that sits flush under the canvas those two things are
    // exactly what makes it read as a separate frame that happens to be parked there. With the
    // outline gone and the corners square, the strip and the body merge into one block that is
    // visibly part of the editor instead of a window laid on top of it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (!ImGui::Begin(kPanelLabelBottomDockUVE, nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    switch (m_activeBottomDock) {
        case EditorBottomDockUVE::FileSystem:
            DrawContentBrowserBodyUVE();
            // The item menu is opened from inside this window, so it is drained in this scope.
            DrawFilesystemContextPopupUVE();
            break;
        case EditorBottomDockUVE::Debugger: {
            ImGui::TextUnformatted("Debug Output");
            // Rows are built by BuildEditorRenderStatRowsUVE, which holds the formatting and the
            // judgement of what counts as concerning. This loop only prints them: deciding what a
            // counter means is the part worth testing, and it cannot be tested from in here.
            const Render::Renderer3DFrameDiagnosticsUVE diagnostics =
                m_services->GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
            std::string currentSection;
            for (const EditorRenderStatRowUVE& row : BuildEditorRenderStatRowsUVE(diagnostics)) {
                if (row.section != currentSection) {
                    currentSection = row.section;
                    ImGui::TextDisabled("%s", currentSection.c_str());
                }
                if (row.isConcerning) {
                    // Amber rather than red: every one of these is "worth a look", not "broken".
                    ImGui::TextColored(ImVec4{0.95F, 0.70F, 0.25F, 1.0F}, "  %s: %s", row.label.c_str(),
                                       row.value.c_str());
                } else {
                    ImGui::Text("  %s: %s", row.label.c_str(), row.value.c_str());
                }
            }
            ImGui::Separator();
            ImGui::Text("Scene state: %s | Undo: %s | Redo: %s",
                        m_sceneDirty ? "unsaved" : "saved", CanUndoUVE() ? "available" : "empty",
                        CanRedoUVE() ? "available" : "empty");
            break;
        }
        case EditorBottomDockUVE::Console:
            DrawConsoleBodyUVE();
            break;
        case EditorBottomDockUVE::Capture:
            DrawCaptureBodyUVE();
            break;
        case EditorBottomDockUVE::Profiler:
            DrawProfilerBodyUVE();
            break;
        case EditorBottomDockUVE::Audio:
            DrawAudioMixerBodyUVE();
            break;
        case EditorBottomDockUVE::Animator:
            ImGui::TextDisabled("Playback controls are centered in the File / Edit menu canvas.");
            break;
        case EditorBottomDockUVE::AIToolbar:
            ImGui::TextUnformatted("AI Tools");
            ImGui::TextDisabled("AI-assisted editor tools are intentionally separate from document state.");
            break;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void EditorUVE::DrawEditorChromeHandlesUVE() {
    if (!IsOutlinerLayoutActiveUVE()) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible, m_inspectorPanelVisible);
    constexpr ImGuiWindowFlags handleFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{1.0F, 1.0F});
    const auto beginHandle = [](const char* const id, const ImVec2 pos, const ImVec2 size) {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0F);
        return ImGui::Begin(id, nullptr, handleFlags);
    };
    const float totalWidth = mainViewport->WorkSize.x;
    const ImGuiIO& io = ImGui::GetIO();

    // ---- The three seams. Each grip straddles its divider, so a drag keeps tracking when the
    // pointer wanders a pixel or two onto either neighbour. The width the layout will use next
    // frame comes from the same clamp the drag applies, so the handle cannot outrun the panel.
    if (layout.sceneSplitterSize.x > 0.0F) {
        if (beginHandle("##left-scene-splitter", layout.sceneSplitterPos, layout.sceneSplitterSize)) {
            ImGui::InvisibleButton("##grip-left", layout.sceneSplitterSize);
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                // The grip is wider than the divider it stands on, so what lights up is the divider:
                // a two-pixel line down the seam, in the same blue the rest of the editor uses for
                // "this is live". Filling the whole grip would paint a ten-pixel band over both
                // neighbours' edges and read as a bar rather than as the divider being grabbed.
                const ImVec2 gripMin = ImGui::GetItemRectMin();
                const ImVec2 gripMax = ImGui::GetItemRectMax();
                const float seamX = (gripMin.x + gripMax.x) * 0.5F;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - 1.0F, gripMin.y},
                                                          ImVec2{seamX + 1.0F, gripMax.y},
                                                          IM_COL32(90, 150, 215, 220));
            }
            if (ImGui::IsItemActive()) {
                // The same shape the entity editor's column seams use: the width the author set is
                // the starting point, the frame's movement is added to it, and the result is what
                // the next frame reads. Guarding on a non-zero delta is not an optimisation - a
                // held-but-still pointer must not write, or a stray frame without movement would
                // re-clamp a width the author chose and quietly move the seam under them.
                if (io.MouseDelta.x != 0.0F) {
                    const float from = m_scenePanelWidth > 0.0F ? m_scenePanelWidth : layout.sceneSize.x;
                    m_scenePanelWidth = ClampScenePanelWidthUVE(totalWidth, from + io.MouseDelta.x);
                }
            }
        }
        ImGui::End();
    }
    if (layout.inspectorSplitterSize.x > 0.0F) {
        if (beginHandle("##right-inspector-splitter", layout.inspectorSplitterPos, layout.inspectorSplitterSize)) {
            ImGui::InvisibleButton("##grip-right", layout.inspectorSplitterSize);
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                const ImVec2 gripMin = ImGui::GetItemRectMin();
                const ImVec2 gripMax = ImGui::GetItemRectMax();
                const float seamX = (gripMin.x + gripMax.x) * 0.5F;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - 1.0F, gripMin.y},
                                                          ImVec2{seamX + 1.0F, gripMax.y},
                                                          IM_COL32(90, 150, 215, 220));
            }
            if (ImGui::IsItemActive()) {
                if (io.MouseDelta.x != 0.0F) {
                    const float from =
                        m_inspectorPanelWidth > 0.0F ? m_inspectorPanelWidth : layout.inspectorSize.x;
                    m_inspectorPanelWidth = ClampInspectorPanelWidthUVE(totalWidth, from - io.MouseDelta.x);
                }
            }
        }
        ImGui::End();
    }
    if (layout.dockSplitterSize.x > 0.0F) {
        if (beginHandle("##bottom-dock-splitter", layout.dockSplitterPos, layout.dockSplitterSize)) {
            ImGui::InvisibleButton("##grip-dock", layout.dockSplitterSize);
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                // Same as the side seams: the divider lights up, not the whole grip.
                const ImVec2 gripMin = ImGui::GetItemRectMin();
                const ImVec2 gripMax = ImGui::GetItemRectMax();
                const float seamY = (gripMin.y + gripMax.y) * 0.5F;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{gripMin.x, seamY - 1.0F},
                                                          ImVec2{gripMax.x, seamY + 1.0F},
                                                          IM_COL32(90, 150, 215, 220));
            }
            if (ImGui::IsItemActive()) {
                // Dragging up grows the dock, so the mouse delta is subtracted from its height.
                if (io.MouseDelta.y != 0.0F) {
                    const float column = layout.viewportSize.y + layout.dockTabBarSize.y + layout.contentBrowserSize.y;
                    const float currentDockHeight = layout.dockTabBarSize.y + layout.contentBrowserSize.y;
                    const float from =
                        m_bottomDockHeight > 0.0F ? m_bottomDockHeight : currentDockHeight;
                    m_bottomDockHeight = ClampBottomDockHeightUVE(column, from - io.MouseDelta.y);
                }
            }
        }
        ImGui::End();
    }

    // ---- The way back. A hidden side panel leaves this strip on the edge it came from; one click
    // anywhere on it restores the panel at the width it had. Hiding without a visible return is
    // exactly the dead end the close button used to be - the panel vanished and the only remaining
    // route back was a Window-menu item nobody knew was there.
    const auto drawRestoreStrip = [&](const char* const id, const ImVec2 pos, const ImVec2 size,
                                      const bool opensToTheRight) {
        if (size.x <= 0.0F) {
            return false;
        }
        bool clicked = false;
        if (beginHandle(id, pos, size)) {
            ImGui::InvisibleButton("##restore", size);
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList& drawList = *ImGui::GetWindowDrawList();
            const ImU32 background = hovered ? IM_COL32(58, 76, 102, 240) : IM_COL32(32, 37, 43, 240);
            drawList.AddRectFilled(pos, ImVec2{pos.x + size.x, pos.y + size.y}, background);
            // A chevron at the strip's vertical centre, pointing the way the panel comes back.
            constexpr float kHalf = 4.0F;
            const ImVec2 centre{pos.x + (size.x * 0.5F), pos.y + (size.y * 0.5F)};
            const ImVec2 tip{centre.x + (opensToTheRight ? kHalf : -kHalf), centre.y};
            const ImVec2 upper{centre.x + (opensToTheRight ? -kHalf : kHalf), centre.y - (kHalf + 1.0F)};
            const ImVec2 lower{centre.x + (opensToTheRight ? -kHalf : kHalf), centre.y + (kHalf + 1.0F)};
            const ImU32 colour = hovered ? IM_COL32(235, 240, 248, 255) : IM_COL32(170, 182, 198, 255);
            drawList.AddLine(upper, tip, colour, 1.6F);
            drawList.AddLine(lower, tip, colour, 1.6F);
            if (hovered && ImGui::IsWindowHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", opensToTheRight ? "Show the Outliner" : "Show the Inspector");
            }
            clicked = ImGui::IsItemClicked();
        }
        ImGui::End();
        return clicked;
    };
    if (drawRestoreStrip("##outliner-restore-strip", layout.outlinerRestorePos, layout.outlinerRestoreSize, true)) {
        m_scenePanelVisible = true;
    }
    if (drawRestoreStrip("##inspector-restore-strip", layout.inspectorRestorePos, layout.inspectorRestoreSize, false)) {
        m_inspectorPanelVisible = true;
    }

    ImGui::PopStyleVar(2);
}

void EditorUVE::DrawBottomDockTabBarUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible,
                                     m_inspectorPanelVisible);
    constexpr ImGuiWindowFlags stripFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

    // Always drawn, dock body or not: this strip is the dock's name, its switcher, and - when the
    // body is hidden - the only visible way back. Every tab toggles: click the one already showing
    // and the dock folds away, click any tab and it unfolds on that section.
    //
    // It sits at the top of the dock and takes the dock's own width, so it is the dock's header
    // rather than an unrelated band across the bottom of the screen. Same square, borderless
    // treatment as the body underneath, so the two read as one panel.
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(layout.dockTabBarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.dockTabBarSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0F, 2.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{2.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin(kPanelLabelDockTabsUVE, nullptr, stripFlags)) {
        struct TabUVE final {
            const char* label;
            EditorBottomDockUVE dock;
            const char* tooltip;
        };
        constexpr std::array<TabUVE, 6> kTabs{{
            {"Content", EditorBottomDockUVE::FileSystem, "The project's files and assets."},
            {"Output", EditorBottomDockUVE::Debugger, "Frame statistics and editor state."},
            {"Console", EditorBottomDockUVE::Console, "Engine messages and a command line."},
            {"Capture", EditorBottomDockUVE::Capture, "Tapped session datagrams and their decode."},
            {"Profiler", EditorBottomDockUVE::Profiler, "Capture spans, counters, and breadcrumbs."},
            {"Audio", EditorBottomDockUVE::Audio, "Mixer tree with live meters, and the selected source's falloff."},
        }};
        const ImGuiStyle& style = ImGui::GetStyle();
        for (const TabUVE& tab : kTabs) {
            const bool active = m_bottomDockVisible && m_activeBottomDock == tab.dock;
            ImGui::PushStyleColor(ImGuiCol_Button, active ? style.Colors[ImGuiCol_TabSelected] : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
            ImGui::PushStyleColor(ImGuiCol_Text, active ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Button(tab.label, ImVec2{0.0F, layout.dockTabBarSize.y - 4.0F})) {
                // Clicking the tab already showing folds the dock away; any other tab shows it.
                if (active) {
                    m_bottomDockVisible = false;
                } else {
                    m_activeBottomDock = tab.dock;
                    m_bottomDockVisible = true;
                }
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s%s", tab.tooltip, active ? " Click again to hide the dock." : "");
            }
            ImGui::SameLine(0.0F, 4.0F);
        }
        // Right end: the scene's save state, where the eye rests when looking for it.
        const char* const state = m_sceneDirty ? "unsaved changes" : "saved";
        const float stateWidth = ImGui::CalcTextSize(state).x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - stateWidth));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", state);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
}

void EditorUVE::DrawConsoleBodyUVE() {
    const DeveloperConsoleSnapshotUVE snapshot = m_developerConsole.GetSnapshotUVE();
    const float inputHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##console-output", ImVec2{0.0F, -inputHeight}, false);
    for (const DeveloperConsoleEntryUVE& entry : snapshot.output) {
        ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_Text];
        if (entry.severity == DeveloperConsoleSeverityUVE::Warning) {
            color = ImVec4{0.95F, 0.72F, 0.30F, 1.0F};
        } else if (entry.severity == DeveloperConsoleSeverityUVE::Error) {
            color = ImVec4{0.95F, 0.45F, 0.40F, 1.0F};
        }
        ImGui::TextColored(color, "%s", entry.text.c_str());
    }
    if (snapshot.output.empty()) {
        ImGui::TextDisabled("Nothing yet. Type help and press Enter.");
    }
    if (m_consoleScrollToBottom) {
        ImGui::SetScrollHereY(1.0F);
        m_consoleScrollToBottom = false;
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##console-input", "command  (Up/Down: history)", m_consoleInput.data(),
                                 m_consoleInput.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string command = m_consoleInput.data();
        if (!command.empty()) {
            static_cast<void>(m_developerConsole.ExecuteUVE(command));
            m_consoleInput.fill('\0');
            m_consoleScrollToBottom = true;
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

}

void EditorUVE::CapturePacketUVE(const std::vector<std::uint8_t>& datagram,
                                 const Network::PacketDirectionUVE direction,
                                 const std::uint64_t timestampMs,
                                 const Network::InspectedChannelUVE channel) {
    m_packetCapture.PushUVE(datagram, direction, timestampMs, channel);
}

void EditorUVE::ClearCapturedPacketsUVE() {
    m_packetCapture.ClearUVE();
}

const Network::PacketCaptureUVE& EditorUVE::CapturedPacketsUVE() const noexcept {
    return m_packetCapture;
}

Core::ProfilerCaptureUVE& EditorUVE::ProfilerUVE() noexcept {
    return m_profilerCapture;
}

const Core::DiagnosticCaptureBundleUVE& EditorUVE::ProfilerBundleUVE() const noexcept {
    return m_profilerBundle;
}

void EditorUVE::DrawCaptureBodyUVE() {
    // The viewer owns plain-int combo state; the filter struct is rebuilt from it each
    // frame so the ring's matching logic stays the single source of truth.
    static constexpr const char* kDirections[] = {"Both", "Out", "In"};
    static constexpr const char* kChannels[] = {"Any", "Wire", "Replication", "RPC", "Prediction", "Shot"};
    m_captureFilter.text = std::string(m_captureText.data());
    m_captureFilter.direction =
        static_cast<Network::PacketCaptureDirectionUVE>(m_captureDirection);
    if (m_captureChannel == 0) {
        m_captureFilter.channel.reset();
    } else {
        m_captureFilter.channel =
            static_cast<Network::InspectedChannelUVE>(m_captureChannel - 1);
    }
    m_captureFilter.malformedOnly = m_captureMalformedOnly;

    if (ImGui::Button("Clear")) {
        ClearCapturedPacketsUVE();
        m_captureSelected = kNoCaptureSelectionUVE;
    }
    ImGui::SameLine();
    const std::string stats = std::to_string(m_packetCapture.SizeUVE()) + "/" +
                              std::to_string(m_packetCapture.CapacityUVE()) + " packets (" +
                              std::to_string(m_packetCapture.DropsUVE()) + " dropped, " +
                              std::to_string(m_packetCapture.MalformedUVE()) + " malformed)";
    ImGui::TextDisabled("%s", stats.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0F);
    if (ImGui::BeginCombo("##uve_capture_dir", kDirections[m_captureDirection])) {
        for (int i = 0; i < 3; ++i) {
            const bool selected = m_captureDirection == i;
            if (ImGui::Selectable(kDirections[i], selected)) {
                m_captureDirection = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::BeginCombo("##uve_capture_ch", kChannels[m_captureChannel])) {
        for (int i = 0; i < 6; ++i) {
            const bool selected = m_captureChannel == i;
            if (ImGui::Selectable(kChannels[i], selected)) {
                m_captureChannel = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Malformed", &m_captureMalformedOnly);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##uve_capture_text", m_captureText.data(), m_captureText.size());

    const std::vector<std::size_t> rows = m_packetCapture.FilteredUVE(m_captureFilter);
    if (m_captureSelected != kNoCaptureSelectionUVE &&
        m_packetCapture.TryAtUVE(m_captureSelected) == nullptr) {
        m_captureSelected = kNoCaptureSelectionUVE;
    }
    if (ImGui::BeginChild("##uve_capture_list", ImVec2(0.0F, 220.0F), true)) {
        if (ImGui::BeginTable("##uve_capture_table", 1,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            for (const std::size_t index : rows) {
                const Network::InspectedPacketUVE* const packet =
                    m_packetCapture.TryAtUVE(index);
                if (packet == nullptr) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string line = Network::DescribePacketUVE(*packet);
                const bool selected = m_captureSelected == index;
                if (packet->malformed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.45F, 0.45F, 1.0F));
                }
                if (ImGui::Selectable(line.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    m_captureSelected = index;
                }
                if (packet->malformed) {
                    ImGui::PopStyleColor();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("##uve_capture_detail", ImVec2(0.0F, 0.0F), true)) {
        const Network::InspectedPacketUVE* const packet =
            m_captureSelected == kNoCaptureSelectionUVE
                ? nullptr
                : m_packetCapture.TryAtUVE(m_captureSelected);
        if (packet == nullptr) {
            ImGui::TextDisabled("Select a packet.");
        } else {
            ImGui::TextUnformatted(Network::DescribePacketUVE(*packet).c_str());
            const std::string meta = "t=" + std::to_string(packet->timestampMs) + " ms, " +
                                     std::to_string(packet->raw.size()) + " bytes" +
                                     (packet->malformed ? ", MALFORMED" : "");
            ImGui::TextDisabled("%s", meta.c_str());
            if (ImGui::BeginChild("##uve_capture_hex", ImVec2(0.0F, 0.0F), false,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                ImGui::TextUnformatted(Network::HexdumpPacketUVE(packet->raw).c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();

}
static std::uint64_t ProfilerNowNsUVE() {
    const std::chrono::nanoseconds now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<std::uint64_t>(now.count());
}

void EditorUVE::DrawProfilerBodyUVE() {
    const bool active = m_profilerCapture.IsActiveUVE();
    if (!active && ImGui::Button("Begin capture")) {
        ++m_profilerSession;
        const std::string session = "editor-" + std::to_string(m_profilerSession);
        const Core::DiagnosticCaptureResultUVE begun =
            m_profilerCapture.BeginUVE(session, ProfilerNowNsUVE());
        m_profilerStatus =
            begun.IsAcceptedUVE() ? "Capturing " + session + "." : begun.message;
    }
    if (active && ImGui::Button("End capture")) {
        Core::DiagnosticCaptureStopResultUVE ended =
            m_profilerCapture.EndUVE(ProfilerNowNsUVE());
        if (ended.IsAcceptedUVE()) {
            m_profilerBundle = std::move(ended.bundle);
            m_profilerHasBundle = true;
            m_profilerStatus = "Ended " + m_profilerBundle.sessionId + ".";
        } else {
            m_profilerStatus = ended.message;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_profilerStatus.c_str());

    // Live while capturing, frozen on the last completed bundle otherwise. The builders
    // hold the sorting and formatting; this loop only prints their rows.
    const Core::DiagnosticCaptureBundleUVE live =
        active ? m_profilerCapture.SnapshotUVE() : Core::DiagnosticCaptureBundleUVE{};
    const Core::DiagnosticCaptureBundleUVE& shown =
        active ? live : (m_profilerHasBundle ? m_profilerBundle : live);
    if (shown.sessionId.empty()) {
        ImGui::TextDisabled("No capture yet: begin one to profile this session.");
        ImGui::End();
        return;
    }
    const std::string counts =
        shown.sessionId + (active ? " (live)" : " (ended)") + ": " +
        std::to_string(shown.spans.size()) + " spans, " +
        std::to_string(shown.counters.size()) + " counters, " +
        std::to_string(shown.breadcrumbs.size()) + " breadcrumbs (" +
        std::to_string(shown.droppedSpanCount) + "/" +
        std::to_string(shown.droppedCounterCount) + "/" +
        std::to_string(shown.droppedBreadcrumbCount) + " dropped)";
    ImGui::TextDisabled("%s", counts.c_str());

    if (ImGui::BeginChild("##uve_profiler_body", ImVec2(0.0F, 0.0F), true)) {
        ImGui::TextUnformatted("Spans");
        const std::vector<Core::ProfilerSpanRowUVE> spans =
            Core::BuildProfilerSpanRowsUVE(shown.spans);
        if (ImGui::BeginTable("##uve_profiler_spans", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Span");
            ImGui::TableSetupColumn("Category");
            ImGui::TableSetupColumn("Begin (ns)");
            ImGui::TableSetupColumn("Duration");
            ImGui::TableHeadersRow();
            for (const Core::ProfilerSpanRowUVE& row : spans) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Core::ProfilerCategoryNameUVE(row.category).data());
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.beginNanoseconds));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(
                    Core::FormatProfilerDurationUVE(row.durationNanoseconds).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::TextUnformatted("Counters");
        const std::vector<Core::ProfilerCounterRowUVE> counters =
            Core::BuildProfilerCounterRowsUVE(shown.counters);
        if (ImGui::BeginTable("##uve_profiler_counters", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Counter");
            ImGui::TableSetupColumn("Category");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();
            for (const Core::ProfilerCounterRowUVE& row : counters) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Core::ProfilerCategoryNameUVE(row.category).data());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Core::FormatProfilerCounterUVE(row.value).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::TextUnformatted("Breadcrumbs");
        if (shown.breadcrumbs.empty()) {
            ImGui::TextDisabled("None.");
        }
        for (auto crumb = shown.breadcrumbs.rbegin(); crumb != shown.breadcrumbs.rend();
             ++crumb) {
            ImGui::TextDisabled("[%llu] %s", static_cast<unsigned long long>(crumb->sequence),
                                crumb->message.c_str());
        }
        ImGui::TextUnformatted("Runtime");
        const Core::DiagnosticRuntimeSnapshotUVE& runtime = shown.runtimeSnapshot;
        const std::string runtimeLine =
            "frame " + std::to_string(runtime.frameNumber) + ", " +
            std::to_string(runtime.activeAllocationCount) + " allocations (" +
            std::to_string(runtime.activeBytes) + " bytes, peak " +
            std::to_string(runtime.peakBytes) + "), " + std::to_string(runtime.workerCount) +
            " workers (" + std::to_string(runtime.activeWorkerCount) + " active, " +
            std::to_string(runtime.pendingTaskCount) + " pending, " +
            std::to_string(runtime.stolenJobCount) + " stolen), animation " +
            std::to_string(runtime.animationEvaluationCount) + ", ecs " +
            std::to_string(runtime.ecsSystemCount) + ", vm " +
            std::to_string(runtime.vmInstructionCount) + ", render passes " +
            std::to_string(runtime.renderPassCount);
        ImGui::TextDisabled("%s", runtimeLine.c_str());
    }
    ImGui::EndChild();

}

} // namespace UVE::Editor
