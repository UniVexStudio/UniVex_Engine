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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_color_field_uve.h"
#include "editor_fonts_uve.h"
#include "editor_node_icons_uve.h"

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
    ImGuiIO& io = ImGui::GetIO();
    const bool lifecycleCommandAllowed = IsLifecycleCommandAllowedUVE() && IsDocumentEntityUVE(m_selectedEntity);
    const bool canEnterPlayMode = m_simulationControl != nullptr &&
                                  m_playModeState == EditorPlayModeStateUVE::Edit;
    if (!io.WantTextInput && canEnterPlayMode && ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        static_cast<void>(EnterPlayModeUVE());
    } else if (!io.WantTextInput && m_playModeState == EditorPlayModeStateUVE::Playing &&
               ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
        static_cast<void>(PausePlayModeUVE());
    } else if (!io.WantTextInput && m_playModeState == EditorPlayModeStateUVE::Paused &&
               ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
        static_cast<void>(ResumePlayModeUVE());
    } else if (!io.WantTextInput && m_playModeState == EditorPlayModeStateUVE::Paused &&
               ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
        static_cast<void>(StepPlayModeUVE());
    } else if (!io.WantTextInput && m_playModeState != EditorPlayModeStateUVE::Edit && io.KeyShift &&
               ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        static_cast<void>(StopPlayModeUVE());
    } else if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        static_cast<void>(io.KeyShift ? RedoUVE() : UndoUVE());
    } else if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        static_cast<void>(RedoUVE());
    } else if (!io.WantTextInput && lifecycleCommandAllowed && io.KeyCtrl &&
               ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        static_cast<void>(DuplicateSelectedEntityUVE());
    } else if (!io.WantTextInput && lifecycleCommandAllowed && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        static_cast<void>(DeleteSelectedEntityUVE());
    }

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
    const auto beginChrome = [mainViewport, chromeFlags, &beginChromeWindow](const char* const id, const float y,
                                                                            const float height) {
        ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{mainViewport->WorkSize.x, height}, ImGuiCond_Always);
        return beginChromeWindow(id, chromeFlags);
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
        // "UVE" wordmark badge + version, replacing the old bitmap logo image - our logo IS the
        // "UVE" name itself now, drawn procedurally (matching this file's own established
        // icon-drawing convention) rather than a separate texture asset to keep in sync.
        {
            constexpr float kLogoBadgeWidthUVE = 38.0F;
            constexpr float kLogoBadgeHeightUVE = 20.0F;
            const ImVec2 badgeMin = ImGui::GetCursorScreenPos();
            const ImVec2 badgeMax{badgeMin.x + kLogoBadgeWidthUVE, badgeMin.y + kLogoBadgeHeightUVE};
            titleDrawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(66, 120, 184, 255), 4.0F);
            const ImVec2 logoTextSize = ImGui::CalcTextSize("UVE");
            titleDrawList->AddText(ImVec2{badgeMin.x + (kLogoBadgeWidthUVE - logoTextSize.x) * 0.5F,
                                          badgeMin.y + (kLogoBadgeHeightUVE - logoTextSize.y) * 0.5F},
                                   IM_COL32(255, 255, 255, 255), "UVE");
            ImGui::Dummy(ImVec2{kLogoBadgeWidthUVE, kLogoBadgeHeightUVE});
            ImGui::SameLine(0.0F, 5.0F);
            ImGui::TextDisabled("0.1");
            ImGui::SameLine(0.0F, 6.0F);
        }

        // File/Edit/Assets/GameObject/Plugin/Window/Help all fold into one "Menu" dropdown here in
        // the title bar, replacing what used to be a separate always-visible menu-bar row below it
        // - one fewer chrome strip, and the seven items only take screen space while actually open.
        // Every item's own body/callback is unchanged from before; only the nesting level moved.
        if (ImGui::BeginMenu("Menu")) {
            if (ImGui::BeginMenu(kMenuLabelFileUVE)) {
                const bool canSave = IsAuthoringCommandAllowedUVE() && !m_activeScenePath.empty();
                ImGui::BeginDisabled(!canSave);
                if (ImGui::MenuItem("Save Scene")) {
                    static_cast<void>(SaveSceneUVE());
                }
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Load Scene")) {
                    static_cast<void>(LoadSceneUVE());
                }
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
                ImGui::Separator();
                if (ImGui::MenuItem("Save Editor Preferences")) {
                    static_cast<void>(SaveSessionSettingsUVE());
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelEditUVE)) {
                ImGui::BeginDisabled(!CanUndoUVE());
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                    static_cast<void>(UndoUVE());
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!CanRedoUVE());
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
                    static_cast<void>(RedoUVE());
                }
                ImGui::EndDisabled();
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
                ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
                if (ImGui::MenuItem("Create Empty")) {
                    static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Empty));
                }
                if (ImGui::MenuItem("Create Cube")) {
                    static_cast<void>(CreateDocumentEntityUVE(EditorEntityKindUVE::Cube));
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(kMenuLabelPluginUVE)) {
                m_pluginWindowVisible = true;
            }
            if (ImGui::BeginMenu(kMenuLabelWindowUVE)) {
                ImGui::MenuItem("Scene", nullptr, &m_scenePanelVisible);
                ImGui::MenuItem("Viewport", nullptr, &m_viewportPanelVisible);
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

        const char* workspaceLabel = "Library";
        switch (m_activeWorkspace) {
            case EditorWorkspaceUVE::Library: workspaceLabel = "Library"; break;
            case EditorWorkspaceUVE::Asset: workspaceLabel = "Asset"; break;
            case EditorWorkspaceUVE::Scripting: workspaceLabel = "Scripting"; break;
            case EditorWorkspaceUVE::Debug: workspaceLabel = "Debug"; break;
            case EditorWorkspaceUVE::Plugin: workspaceLabel = "Plugin"; break;
            case EditorWorkspaceUVE::Game: workspaceLabel = "Game"; break;
        }
        ImGui::TextDisabled("| %s |", workspaceLabel);
        ImGui::SameLine();
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

    if (beginChrome("##uve-tool-row", kEditorTitleBarHeightUVE,
                    kEditorToolbarHeightUVE)) {
        ImDrawList* const toolbarDrawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarMin = ImGui::GetWindowPos();
        const ImVec2 toolbarMax{toolbarMin.x + ImGui::GetWindowWidth(), toolbarMin.y + kEditorToolbarHeightUVE};
        toolbarDrawList->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(32, 37, 43, 255));
        toolbarDrawList->AddLine(ImVec2{toolbarMin.x, toolbarMin.y}, ImVec2{toolbarMax.x, toolbarMin.y},
                                 IM_COL32(48, 55, 64, 235), 1.0F);
        const auto drawWorkspace = [this](const char* const label, const EditorWorkspaceUVE workspace) {
            const bool active = m_activeWorkspace == workspace;
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.20F, 0.21F, 0.23F, 1.0F});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.32F, 0.35F, 0.39F, 1.0F});
            }
            if (ImGui::SmallButton(label)) {
                m_activeWorkspace = workspace;
            }
            if (active) {
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        };
        drawWorkspace("Scene", EditorWorkspaceUVE::Library);
        drawWorkspace("Scripting", EditorWorkspaceUVE::Scripting);
        drawWorkspace("Game", EditorWorkspaceUVE::Game);

        // ---- Play/Pause/Stop transport, relocated here from the menu row and right-aligned ----
        constexpr float kTransportButtonWidthUVE = 40.0F;
        constexpr float kTransportButtonHeightUVE = 20.0F;
        constexpr float kTransportButtonGapUVE = 4.0F;
        const float transportGroupWidth = 2.0F * kTransportButtonWidthUVE + kTransportButtonGapUVE;
        ImGui::SetCursorScreenPos(
            ImVec2{toolbarMax.x - transportGroupWidth - 8.0F,
                   toolbarMin.y + (kEditorToolbarHeightUVE - kTransportButtonHeightUVE) * 0.5F});
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

void EditorUVE::DrawBottomDockUVE() {
    // Filesystem and Debug are rendered as one bottom canvas by DrawBottomDockContentUVE().
    // Keep this entry point for session/layout compatibility; the old selector strip is intentionally gone.
}

void EditorUVE::DrawBottomDockContentUVE() {
    if (!m_bottomDockVisible) {
        return;
    }
    if (m_activeBottomDock == EditorBottomDockUVE::FileSystem) {
        DrawContentBrowserPanelUVE();
        DrawFilesystemContextPopupUVE();
        return;
    }

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible);
    // Always, not FirstUseEver - see DrawHierarchyPanelUVE()'s comment on the same change. This
    // window is the Content Browser's mutually-exclusive alternate, so it shares the exact same
    // bottom rect (via the centralized layout helper) and re-tiling guarantee.
    ImGui::SetNextWindowPos(layout.contentBrowserPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.contentBrowserSize, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Debug##lower-workspace", nullptr, flags);
    switch (m_activeBottomDock) {
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
        case EditorBottomDockUVE::Animator:
            ImGui::TextDisabled("Playback controls are centered in the File / Edit menu canvas.");
            break;
        case EditorBottomDockUVE::AIToolbar:
            ImGui::TextUnformatted("AI Tools");
            ImGui::TextDisabled("AI-assisted editor tools are intentionally separate from document state.");
            break;
        case EditorBottomDockUVE::FileSystem:
            break;
    }
    ImGui::End();
}

} // namespace UVE::Editor
