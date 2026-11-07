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
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include "editor_chrome_layout_uve.h"
#include "editor_color_field_uve.h"
#include "editor_fonts_uve.h"
#include "editor_object_icons_uve.h"

#include "uve/diagnostics/profiler_view_uve.h"
#include "uve/editor/editor_render_stats_uve.h"

namespace UVE::Editor {
namespace {

constexpr const char* kMenuLabelFileUVE = "File";
constexpr const char* kMenuLabelEditUVE = "Edit";
constexpr const char* kMenuLabelAssetsUVE = "Assets";
constexpr const char* kMenuLabelGameObjectUVE = "GameObject";
constexpr const char* kMenuLabelPluginUVE = "Plugin";
constexpr const char* kMenuLabelWindowUVE = "Window";
constexpr const char* kMenuLabelToolsUVE = "Tools";
constexpr const char* kMenuLabelBuildUVE = "Build";
constexpr const char* kMenuLabelHelpUVE = "Help";

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
    // Suppress window borders and rounding on all top chrome strips so no 1px border gaps appear
    // between the Titlebar, Menu row, Outliner tab, Viewport tab, or Transport row.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});

    const auto beginChromeWindow = [](const char* const id, const ImGuiWindowFlags flags) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{1.0F, 1.0F});
        const bool open = ImGui::Begin(id, nullptr, flags);
        ImGui::PopStyleVar();
        return open;
    };

    const bool sidePanelsShown = m_activeWorkspace != EditorWorkspaceUVE::Scripting && !m_entityEditSession.has_value() &&
                                 !m_retargetPreview.has_value();
    const EditorChromeLayoutUVE stripLayout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible,
                                     m_inspectorPanelVisible);

    // Native window handle for custom OS titlebar controls and window interaction
    GLFWwindow* const nativeWindow = m_services != nullptr
        ? static_cast<GLFWwindow*>(m_services->GetWindowManagerUVE().GetNativeWindowHandleUVE())
        : nullptr;

    // ---- Row 1: Dedicated Engine OS Titlebar Row (x = logoSize.x..W) with Menus ------------------
    const ImVec2 osTitlePos{mainViewport->WorkPos.x + stripLayout.logoSize.x, mainViewport->WorkPos.y};
    const ImVec2 osTitleSize{std::max(1.0F, mainViewport->WorkSize.x - stripLayout.logoSize.x),
                             kEditorOsTitleBarRowHeightUVE};
    ImGui::SetNextWindowPos(osTitlePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(osTitleSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{6.0F, 2.5F});
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{14.0F / 255.0F, 17.0F / 255.0F, 22.0F / 255.0F, 1.0F});
    if (beginChromeWindow("##uve-os-titlebar", chromeFlags | ImGuiWindowFlags_MenuBar)) {
        ImDrawList* const osDrawList = ImGui::GetWindowDrawList();
        const ImVec2 osMin = ImGui::GetWindowPos();
        const ImVec2 osMax{osMin.x + ImGui::GetWindowWidth(), osMin.y + kEditorOsTitleBarRowHeightUVE};
        osDrawList->AddRectFilled(osMin, osMax, kEditorHeaderStripColorUVE);
        const bool isMaximized =
            nativeWindow != nullptr && glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED) == GLFW_TRUE;
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu(kMenuLabelFileUVE)) {
                DrawCommandMenuItemUVE("file.saveScene");
                DrawCommandMenuItemUVE("file.saveAll");
                DrawCommandMenuItemUVE("file.loadScene");
                ImGui::Separator();
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
            if (ImGui::BeginMenu(kMenuLabelWindowUVE)) {
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
            if (ImGui::BeginMenu(kMenuLabelToolsUVE)) {
                if (ImGui::MenuItem("Project Browser")) {
                    m_activeBottomDock = EditorBottomDockUVE::FileSystem;
                    m_bottomDockVisible = true;
                }
                ImGui::MenuItem("Import Queue", nullptr, false, false);
                if (ImGui::MenuItem("Plugin Tools")) {
                    m_pluginWindowVisible = true;
                }
                ImGui::Separator();
                DrawCommandMenuItemUVE("file.projectSettings");
                DrawCommandMenuItemUVE("file.preferences");
                DrawCommandMenuItemUVE("file.commandPalette");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelBuildUVE)) {
                ImGui::MenuItem("Build All Levels", nullptr, false, false);
                ImGui::MenuItem("Build Geometry", nullptr, false, false);
                ImGui::MenuItem("Build Lighting Only", nullptr, false, false);
                ImGui::MenuItem("Cook Content for Project", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(kMenuLabelHelpUVE)) {
                ImGui::MenuItem("UVE Editor Reference", nullptr, false, false);
                ImGui::MenuItem("About UNIVEX Engine", nullptr, false, false);
                ImGui::EndMenu();
            }
            // Document Title in Titlebar center
            const std::string sceneFileName = m_activeScenePath.filename().string();
            const std::string windowTitleText =
                "UniVex Engine  -  " + (sceneFileName.empty() ? std::string{"Viewport.uvscene"} : sceneFileName) +
                (m_sceneDirty ? " *" : "");
            const float titleTextWidth = ImGui::CalcTextSize(windowTitleText.c_str()).x;
            constexpr float kMinBtnW = 36.0F;
            constexpr float kMaxBtnW = 36.0F;
            constexpr float kCloseBtnW = 40.0F;
            constexpr float kOsButtonsTotalW = kMinBtnW + kMaxBtnW + kCloseBtnW;

            const float centerTargetX = (ImGui::GetWindowWidth() - titleTextWidth) * 0.5F;
            if (centerTargetX > ImGui::GetCursorPosX() + 15.0F &&
                centerTargetX + titleTextWidth < ImGui::GetWindowWidth() - kOsButtonsTotalW - 15.0F) {
                ImGui::SameLine(centerTargetX);
                ImGui::TextColored(ImVec4{0.80F, 0.84F, 0.90F, 0.90F}, "%s", windowTitleText.c_str());
            }

            // OS Window Buttons on far right (─, □, ✕)
            ImGui::SameLine(ImGui::GetWindowWidth() - kOsButtonsTotalW);

            // 1. Minimize Button
            const ImVec2 minBtnScreenPos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##uve-os-min", ImVec2{kMinBtnW, kEditorOsTitleBarRowHeightUVE})) {
                if (nativeWindow != nullptr) {
                    glfwIconifyWindow(nativeWindow);
                }
            }
            const bool minHovered = ImGui::IsItemHovered();
            if (minHovered) {
                osDrawList->AddRectFilled(
                    minBtnScreenPos,
                    ImVec2{minBtnScreenPos.x + kMinBtnW, minBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE},
                    IM_COL32(255, 255, 255, 24));
            }
            const ImVec2 minCenter{minBtnScreenPos.x + kMinBtnW * 0.5F,
                                   minBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE * 0.5F};
            const ImU32 minColor = minHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 192, 208, 220);
            osDrawList->AddLine(ImVec2{minCenter.x - 5.0F, minCenter.y}, ImVec2{minCenter.x + 5.0F, minCenter.y},
                                minColor, 1.2F);

            ImGui::SameLine(0.0F, 0.0F);

            // 2. Maximize / Restore Button
            const ImVec2 maxBtnScreenPos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##uve-os-max", ImVec2{kMaxBtnW, kEditorOsTitleBarRowHeightUVE})) {
                if (nativeWindow != nullptr) {
                    if (isMaximized) {
                        glfwRestoreWindow(nativeWindow);
                    } else {
                        glfwMaximizeWindow(nativeWindow);
                    }
                }
            }
            const bool maxHovered = ImGui::IsItemHovered();
            if (maxHovered) {
                osDrawList->AddRectFilled(
                    maxBtnScreenPos,
                    ImVec2{maxBtnScreenPos.x + kMaxBtnW, maxBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE},
                    IM_COL32(255, 255, 255, 24));
            }
            const ImVec2 maxCenter{maxBtnScreenPos.x + kMaxBtnW * 0.5F,
                                   maxBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE * 0.5F};
            const ImU32 maxColor = maxHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 192, 208, 220);
            if (isMaximized) {
                osDrawList->AddRect(ImVec2{maxCenter.x - 3.0F, maxCenter.y - 5.0F},
                                    ImVec2{maxCenter.x + 5.0F, maxCenter.y + 3.0F}, maxColor, 0.0F, 0, 1.0F);
                osDrawList->AddRectFilled(ImVec2{maxCenter.x - 5.0F, maxCenter.y - 3.0F},
                                          ImVec2{maxCenter.x + 3.0F, maxCenter.y + 5.0F}, kEditorHeaderStripColorUVE);
                osDrawList->AddRect(ImVec2{maxCenter.x - 5.0F, maxCenter.y - 3.0F},
                                    ImVec2{maxCenter.x + 3.0F, maxCenter.y + 5.0F}, maxColor, 0.0F, 0, 1.0F);
            } else {
                osDrawList->AddRect(ImVec2{maxCenter.x - 4.5F, maxCenter.y - 4.5F},
                                    ImVec2{maxCenter.x + 4.5F, maxCenter.y + 4.5F}, maxColor, 0.0F, 0, 1.2F);
            }

            ImGui::SameLine(0.0F, 0.0F);

            // 3. Close Button
            const ImVec2 closeBtnScreenPos = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##uve-os-close", ImVec2{kCloseBtnW, kEditorOsTitleBarRowHeightUVE})) {
                if (nativeWindow != nullptr) {
                    glfwSetWindowShouldClose(nativeWindow, GLFW_TRUE);
                }
            }
            const bool closeHovered = ImGui::IsItemHovered();
            if (closeHovered) {
                osDrawList->AddRectFilled(
                    closeBtnScreenPos,
                    ImVec2{closeBtnScreenPos.x + kCloseBtnW, closeBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE},
                    IM_COL32(232, 17, 35, 255));
            }
            const ImVec2 closeCenter{closeBtnScreenPos.x + kCloseBtnW * 0.5F,
                                     closeBtnScreenPos.y + kEditorOsTitleBarRowHeightUVE * 0.5F};
            const ImU32 closeColor = closeHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 192, 208, 220);
            osDrawList->AddLine(ImVec2{closeCenter.x - 4.0F, closeCenter.y - 4.0F},
                                ImVec2{closeCenter.x + 4.0F, closeCenter.y + 4.0F}, closeColor, 1.2F);
            osDrawList->AddLine(ImVec2{closeCenter.x - 4.0F, closeCenter.y + 4.0F},
                                ImVec2{closeCenter.x + 4.0F, closeCenter.y - 4.0F}, closeColor, 1.2F);

            ImGui::EndMenuBar();
        }

        // Dragging and double-click to maximize/restore on empty OS titlebar space
        if (nativeWindow != nullptr && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            !ImGui::IsAnyItemActive()) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (isMaximized) {
                    glfwRestoreWindow(nativeWindow);
                } else {
                    glfwMaximizeWindow(nativeWindow);
                }
            } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0F)) {
                if (!isMaximized) {
                    const ImVec2 dragDelta = ImGui::GetIO().MouseDelta;
                    if (dragDelta.x != 0.0F || dragDelta.y != 0.0F) {
                        int winX = 0;
                        int winY = 0;
                        glfwGetWindowPos(nativeWindow, &winX, &winY);
                        glfwSetWindowPos(nativeWindow, winX + static_cast<int>(dragDelta.x),
                                         winY + static_cast<int>(dragDelta.y));
                    }
                }
            }
        }

        ImGui::End();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ---- Row 2: Elevated Viewport Tab & Header Row (x = logoSize.x..W) --------------------------
    const ImVec2 menuRowPos{mainViewport->WorkPos.x + stripLayout.logoSize.x,
                            mainViewport->WorkPos.y + kEditorOsTitleBarRowHeightUVE};
    const ImVec2 menuRowSize{std::max(1.0F, mainViewport->WorkSize.x - stripLayout.logoSize.x),
                             kEditorMenuBarRowHeightUVE};
    ImGui::SetNextWindowPos(menuRowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(menuRowSize, ImGuiCond_Always);
    const bool menuRowOpen = beginChromeWindow("##uve-titlebar", chromeFlags);
    if (menuRowOpen) {
        ImDrawList* const titleDrawList = ImGui::GetWindowDrawList();
        const ImVec2 titleMin = ImGui::GetWindowPos();
        const ImVec2 titleMax{titleMin.x + ImGui::GetWindowWidth(), titleMin.y + kEditorMenuBarRowHeightUVE};
        titleDrawList->AddRectFilled(titleMin, titleMax, kEditorHeaderStripColorUVE);

        // Elevated Viewport Tab seated directly above the Viewport column
        if (sidePanelsShown) {
            const std::string stem = m_activeScenePath.stem().string();
            const std::string name = stem.empty() ? std::string{"Viewport3D"} : stem;
            constexpr float kTabIconSizeUVE = 16.0F;
            // Left edge moved 8px pa-left (0px inset mula sa Viewport split)
            const float tabStartX = stripLayout.viewportHeaderPos.x + kViewportTabInsetX;
            const float textWidth = ImGui::CalcTextSize(name.c_str()).x;
            // Kanan nananatiling nakapwesto (+8px usog sa dulo ng select, kabuuang lapad 164px)
            constexpr float kTabTargetWidth = 164.0F;
            const float tabWidth = std::max(kTabTargetWidth, textWidth + kTabIconSizeUVE + 46.0F);
            const ImVec2 tabMin{tabStartX, titleMin.y};
            const ImVec2 tabMax{tabStartX + tabWidth, titleMax.y + 1.0F};

            // Top-rounded active tab with subtle blue highlight fade
            DrawActiveTopRoundedTabUVE(*titleDrawList, tabMin, tabMax, IM_COL32(32, 37, 43, 255), 6.0F);

            const float midY = (tabMin.y + tabMax.y) * 0.5F;
            std::uintptr_t icon = m_uiAssets.GetContentTypeIconTextureIdUVE(
                GetContentBrowserItemTypeLabelUVE(ContentBrowserItemTypeUVE::Scene));
            if (icon == 0U) {
                icon = m_uiAssets.GetObjectIconTextureIdUVE(Scene::Objects::SceneObjectKindUVE::Viewport3D);
            }
            if (icon != 0U) {
                titleDrawList->AddImage(static_cast<ImTextureID>(icon),
                                        ImVec2{tabMin.x + 12.0F, midY - kTabIconSizeUVE * 0.5F},
                                        ImVec2{tabMin.x + 12.0F + kTabIconSizeUVE, midY + kTabIconSizeUVE * 0.5F});
            }
            titleDrawList->AddText(ImVec2{tabMin.x + 18.0F + kTabIconSizeUVE, midY - ImGui::GetTextLineHeight() * 0.5F},
                                   IM_COL32(235, 240, 248, 255), name.c_str());

            // Tab close indicator / cross with awang on the right
            const float closeX = tabMax.x - 14.0F;
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            const bool closeHovered = (mousePos.x >= closeX - 8.0F && mousePos.x <= closeX + 8.0F &&
                                       mousePos.y >= midY - 8.0F && mousePos.y <= midY + 8.0F);
            if (closeHovered) {
                titleDrawList->AddCircleFilled(ImVec2{closeX, midY}, 7.0F, IM_COL32(255, 255, 255, 30));
            }
            const ImU32 closeCol = closeHovered ? IM_COL32(255, 255, 255, 240) : IM_COL32(160, 172, 188, 180);
            titleDrawList->AddLine(ImVec2{closeX - 3.5F, midY - 3.5F}, ImVec2{closeX + 3.5F, midY + 3.5F},
                                   closeCol, 1.2F);
            titleDrawList->AddLine(ImVec2{closeX - 3.5F, midY + 3.5F}, ImVec2{closeX + 3.5F, midY - 3.5F},
                                   closeCol, 1.2F);
        }

        // Status / Mode / Version on the right of Row 2
        const char* const versionLabel = "UVE Editor 0.1";
        const float versionLabelWidth = ImGui::CalcTextSize(versionLabel).x;
        const float rightMargin = titleMax.x - versionLabelWidth - 14.0F;
        const float midY = titleMin.y + (kEditorMenuBarRowHeightUVE - ImGui::GetTextLineHeight()) * 0.5F;
        titleDrawList->AddText(ImVec2{rightMargin, midY}, IM_COL32(140, 150, 165, 180), versionLabel);

        const char* const modeLabel =
            m_playModeState == EditorPlayModeStateUVE::Edit
                ? "edit"
                : (m_playModeState == EditorPlayModeStateUVE::Paused
                       ? "paused"
                       : (m_playFastForwardEnabled ? "fast-forward" : "playing"));
        char statusBuf[96]{};
        std::snprintf(statusBuf, sizeof(statusBuf), "%s | %zu selected | %s",
                      m_sceneDirty ? "unsaved" : "saved", m_selectedEntities.size(), modeLabel);
        const float statusWidth = ImGui::CalcTextSize(statusBuf).x;
        const float statusX = rightMargin - statusWidth - 24.0F;
        if (statusX > stripLayout.viewportHeaderPos.x + 200.0F) {
            const ImVec2 dotCenter{statusX - 8.0F, titleMin.y + kEditorMenuBarRowHeightUVE * 0.5F};
            titleDrawList->AddCircleFilled(
                dotCenter, 2.5F, m_sceneDirty ? IM_COL32(224, 177, 63, 255) : IM_COL32(95, 201, 138, 255));
            titleDrawList->AddText(ImVec2{statusX, midY}, IM_COL32(140, 150, 165, 180), statusBuf);
        }

        ImGui::End();
    }

    // ---- Shared Engine Logo Box: spans Titlebar Row + Menu Row ----------------------------------
    if (stripLayout.logoSize.x > 0.0F && stripLayout.logoSize.y > 0.0F) {
        ImGui::SetNextWindowPos(stripLayout.logoPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(stripLayout.logoSize, ImGuiCond_Always);
        if (beginChromeWindow("##uve-logo", chromeFlags)) {
            ImDrawList* const logoDrawList = ImGui::GetWindowDrawList();
            const ImVec2 logoMin = ImGui::GetWindowPos();
            const ImVec2 logoMax{logoMin.x + stripLayout.logoSize.x, logoMin.y + stripLayout.logoSize.y};
            logoDrawList->AddRectFilled(logoMin, logoMax, kEditorHeaderStripColorUVE);

            const float markDimension = std::min(stripLayout.logoSize.x, stripLayout.logoSize.y) - 8.0F;
            const ImVec2 markCenter{(logoMin.x + logoMax.x) * 0.5F, (logoMin.y + logoMax.y) * 0.5F};
            const ImVec2 markMin{markCenter.x - markDimension * 0.5F, markCenter.y - markDimension * 0.5F};
            const ImVec2 markMax{markCenter.x + markDimension * 0.5F, markCenter.y + markDimension * 0.5F};
            logoDrawList->AddRectFilled(markMin, markMax, IM_COL32(32, 112, 228, 255), 6.0F);
            logoDrawList->AddRect(markMin, markMax, IM_COL32(82, 160, 255, 220), 6.0F, 0, 1.2F);
            const float logoFontSize = ImGui::GetFontSize() * 1.50F;
            const ImVec2 uSize = ImGui::GetFont()->CalcTextSizeA(logoFontSize, FLT_MAX, 0.0F, "U");
            logoDrawList->AddText(ImGui::GetFont(), logoFontSize,
                                  ImVec2{(markMin.x + markMax.x - uSize.x) * 0.5F,
                                         (markMin.y + markMax.y - uSize.y) * 0.5F},
                                  IM_COL32(255, 255, 255, 255), "U");
            ImGui::End();
        }
    }

    // ---- Row 3: Viewport Header & Dedicated Transport Row + Tool Strip --------------------------
    const ImVec2 stripPos = sidePanelsShown ? stripLayout.viewportHeaderPos
                                            : ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + kEditorTitleBarHeightUVE};
    const ImVec2 stripSize = sidePanelsShown ? stripLayout.viewportHeaderSize : ImVec2{mainViewport->WorkSize.x, kEditorToolbarHeightUVE};
    ImGui::SetNextWindowPos(stripPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(stripSize, ImGuiCond_Always);
    if (beginChromeWindow("##uve-tool-row", chromeFlags)) {
        ImDrawList* const toolbarDrawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarMin = ImGui::GetWindowPos();
        const ImVec2 toolbarMax{toolbarMin.x + ImGui::GetWindowWidth(), toolbarMin.y + kEditorToolbarHeightUVE};
        constexpr float kRow1HeightUVE = kEditorTabRowHeightUVE;

        // Viewport Header Tool Strip background (seamlessly connects to elevated tab above)
        toolbarDrawList->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(32, 37, 43, 255));
        toolbarDrawList->AddLine(ImVec2{toolbarMin.x, toolbarMin.y + kRow1HeightUVE},
                                 ImVec2{toolbarMax.x, toolbarMin.y + kRow1HeightUVE},
                                 IM_COL32(42, 48, 56, 200), 1.0F);
        toolbarDrawList->AddLine(ImVec2{toolbarMin.x, toolbarMax.y - 1.0F},
                                 ImVec2{toolbarMax.x, toolbarMax.y - 1.0F},
                                 IM_COL32(48, 55, 64, 235), 1.0F);

        float cursorX = toolbarMin.x + 8.0F;

        // ---- Upper Sub-Row Left: Selection Mode Dropdown (UE5 Modes Selector) -------------------
        constexpr float kModeBtnW = 148.0F;
        constexpr float kModeBtnH = 22.0F;
        const float modeBtnY = toolbarMin.y + (kRow1HeightUVE - kModeBtnH) * 0.5F;
        ImGui::SetCursorScreenPos(ImVec2{toolbarMin.x + 8.0F, modeBtnY});

        static int s_activeEditorMode = 0;
        const char* const kModeNames[] = {
            "Selection Mode",
            "Landscape Mode",
            "Foliage Mode",
            "Mesh Paint Mode",
            "Modeling Mode",
            "Animation Mode"
        };
        const char* const currentModeName = kModeNames[std::clamp(s_activeEditorMode, 0, 5)];

        ImGui::PushID("##uve-modes-btn");
        const bool modeBtnPressed = ImGui::InvisibleButton("##modes-toggle", ImVec2{kModeBtnW, kModeBtnH});
        const bool modeBtnHovered = ImGui::IsItemHovered();
        const ImVec2 mMin = ImGui::GetItemRectMin();
        const ImVec2 mMax = ImGui::GetItemRectMax();
        const ImU32 modeBg = modeBtnHovered ? IM_COL32(50, 60, 74, 240) : IM_COL32(24, 28, 35, 230);
        toolbarDrawList->AddRectFilled(mMin, mMax, modeBg, 4.0F);
        toolbarDrawList->AddRect(mMin, mMax, IM_COL32(58, 68, 82, 220), 4.0F, 0, 1.0F);

        const ImVec2 iconCenter{mMin.x + 12.0F, (mMin.y + mMax.y) * 0.5F};
        toolbarDrawList->AddRectFilled(ImVec2{iconCenter.x - 4.5F, iconCenter.y - 4.5F},
                                       ImVec2{iconCenter.x + 4.5F, iconCenter.y + 4.5F},
                                       IM_COL32(46, 136, 248, 240), 1.5F);
        toolbarDrawList->AddText(ImVec2{mMin.x + 22.0F, (mMin.y + mMax.y - ImGui::GetTextLineHeight()) * 0.5F},
                                 IM_COL32(230, 238, 248, 255), currentModeName);
        const ImVec2 arrowCenter{mMax.x - 10.0F, (mMin.y + mMax.y) * 0.5F};
        toolbarDrawList->AddTriangleFilled(ImVec2{arrowCenter.x - 3.5F, arrowCenter.y - 2.0F},
                                           ImVec2{arrowCenter.x + 3.5F, arrowCenter.y - 2.0F},
                                           ImVec2{arrowCenter.x, arrowCenter.y + 2.5F},
                                           IM_COL32(180, 192, 208, 220));

        if (modeBtnPressed) {
            ImGui::OpenPopup("##uve-modes-popup");
        }
        if (ImGui::BeginPopup("##uve-modes-popup")) {
            ImGui::TextDisabled("ACTIVE EDITING MODE");
            ImGui::Separator();
            for (int i = 0; i < 6; ++i) {
                const char* const shortcuts[] = { "Shift+1", "Shift+2", "Shift+3", "Shift+4", "Shift+5", "Shift+6" };
                const bool isSel = (s_activeEditorMode == i);
                if (ImGui::MenuItem(kModeNames[i], shortcuts[i], isSel)) {
                    s_activeEditorMode = i;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        cursorX = mMax.x + 12.0F;

        // ---- Upper Sub-Row Center/Right: Expanded Transport Row (Play, Pause, Stop, Fast-Forward, ⋮)
        constexpr float kTransportBtnW = 32.0F;
        constexpr float kTransportBtnH = 22.0F;
        constexpr float kTransportDotsW = 22.0F;
        constexpr float kTransportGap = 3.0F;
        constexpr float kTransportPadX = 5.0F;
        const float transportRowW =
            (kTransportPadX * 2.0F) + (kTransportBtnW * 4.0F) + kTransportDotsW + (kTransportGap * 4.0F);
        const float centerTargetX = toolbarMin.x + (ImGui::GetWindowWidth() - transportRowW) * 0.5F;
        const float transportBoxX = std::max(cursorX, centerTargetX);
        const float transportBoxY = toolbarMin.y + 1.0F;
        const ImVec2 transportBoxMin{transportBoxX, transportBoxY};
        const ImVec2 transportBoxMax{transportBoxX + transportRowW, toolbarMin.y + kRow1HeightUVE - 1.0F};

        // Dedicated transport row container background & subtle border
        toolbarDrawList->AddRectFilled(transportBoxMin, transportBoxMax, IM_COL32(22, 26, 33, 250), 5.0F,
                                       ImDrawFlags_RoundCornersTop);
        toolbarDrawList->AddRect(transportBoxMin, transportBoxMax, IM_COL32(56, 66, 80, 220), 5.0F,
                                 ImDrawFlags_RoundCornersTop, 1.0F);

        ImGui::SetCursorScreenPos(ImVec2{transportBoxMin.x + kTransportPadX,
                                         toolbarMin.y + (kRow1HeightUVE - kTransportBtnH) * 0.5F});

        const auto drawTransportImageButton = [&](const char* const id, const std::uintptr_t texId,
                                                  const bool enabled, const bool active,
                                                  const ImU32 activeBgCol, const ImU32 iconTint,
                                                  const char* const tooltip) {
            ImGui::BeginDisabled(!enabled);
            ImGui::PushID(id);
            const bool pressed = ImGui::InvisibleButton("##tbtn", ImVec2{kTransportBtnW, kTransportBtnH});
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            const ImVec2 bMin = ImGui::GetItemRectMin();
            const ImVec2 bMax = ImGui::GetItemRectMax();
            const ImVec2 bCenter{(bMin.x + bMax.x) * 0.5F, (bMin.y + bMax.y) * 0.5F};
            if (active) {
                toolbarDrawList->AddRectFilled(bMin, bMax, activeBgCol, 3.0F);
            } else if (hovered && enabled) {
                toolbarDrawList->AddRectFilled(bMin, bMax, IM_COL32(56, 66, 80, 220), 3.0F);
            }
            constexpr float kHalfIcon = 8.0F;
            const ImU32 tint = enabled ? iconTint : IM_COL32(120, 128, 140, 110);
            if (texId != 0U) {
                toolbarDrawList->AddImage(static_cast<ImTextureID>(texId),
                                          ImVec2{bCenter.x - kHalfIcon, bCenter.y - kHalfIcon},
                                          ImVec2{bCenter.x + kHalfIcon, bCenter.y + kHalfIcon},
                                          ImVec2{0.0F, 0.0F}, ImVec2{1.0F, 1.0F}, tint);
            }
            if (tooltip != nullptr && hovered) {
                ImGui::SetTooltip("%s", tooltip);
            }
            ImGui::PopID();
            ImGui::EndDisabled();
            return pressed;
        };

        const std::uintptr_t playTexId = m_uiAssets.GetGeneralIconTextureIdUVE("play");
        const std::uintptr_t pauseTexId = m_uiAssets.GetGeneralIconTextureIdUVE("pause");
        const std::uintptr_t stopTexId = m_uiAssets.GetGeneralIconTextureIdUVE("stop");
        const std::uintptr_t ffTexId = m_uiAssets.GetGeneralIconTextureIdUVE("fast-forward");

        const bool isEdit = m_playModeState == EditorPlayModeStateUVE::Edit;
        const bool isPlaying = m_playModeState == EditorPlayModeStateUVE::Playing;
        const bool isPaused = m_playModeState == EditorPlayModeStateUVE::Paused;
        const bool hasSimControl = m_simulationControl != nullptr;

        // 1. Play Button (actual play.png texture)
        const bool playActive = isPlaying && !m_playFastForwardEnabled;
        if (drawTransportImageButton("##transport-play", playTexId, hasSimControl, playActive,
                                     IM_COL32(36, 115, 64, 230), IM_COL32(110, 230, 145, 255),
                                     isEdit ? "Play Simulation" : "Play (Normal 1x Speed)")) {
            if (isEdit) {
                static_cast<void>(SetPlayFastForwardUVE(false));
                static_cast<void>(EnterPlayModeUVE());
            } else if (isPaused) {
                static_cast<void>(SetPlayFastForwardUVE(false));
                static_cast<void>(ResumePlayModeUVE());
            } else if (isPlaying && m_playFastForwardEnabled) {
                static_cast<void>(SetPlayFastForwardUVE(false));
            }
        }
        ImGui::SameLine(0.0F, kTransportGap);

        // 2. Pause Button (actual pause.png texture)
        if (drawTransportImageButton("##transport-pause", pauseTexId, !isEdit, isPaused,
                                     IM_COL32(180, 130, 36, 220), IM_COL32(235, 240, 248, 255),
                                     isPaused ? "Resume Simulation" : "Pause Simulation")) {
            if (isPlaying) {
                static_cast<void>(PausePlayModeUVE());
            } else if (isPaused) {
                static_cast<void>(ResumePlayModeUVE());
            }
        }
        ImGui::SameLine(0.0F, kTransportGap);

        // 3. Stop Button (actual stop.png texture)
        if (drawTransportImageButton("##transport-stop", stopTexId, !isEdit, false,
                                     IM_COL32(140, 44, 48, 220), IM_COL32(240, 110, 110, 255),
                                     "Stop Simulation")) {
            static_cast<void>(StopPlayModeUVE());
        }
        ImGui::SameLine(0.0F, kTransportGap);

        // 4. Fast-Forward Button (actual fast-forward.png texture)
        const bool ffActive = !isEdit && m_playFastForwardEnabled;
        if (drawTransportImageButton("##transport-ff", ffTexId, hasSimControl, ffActive,
                                     IM_COL32(36, 118, 230, 235), IM_COL32(150, 215, 255, 255),
                                     ffActive ? "Fast-Forward Active (Click for 1x Speed)"
                                              : "Fast-Forward Simulation")) {
            if (isEdit) {
                if (EnterPlayModeUVE()) {
                    static_cast<void>(SetPlayFastForwardUVE(true));
                }
            } else if (isPaused) {
                static_cast<void>(SetPlayFastForwardUVE(true));
                static_cast<void>(ResumePlayModeUVE());
            } else if (isPlaying) {
                static_cast<void>(SetPlayFastForwardUVE(!m_playFastForwardEnabled));
            }
        }
        ImGui::SameLine(0.0F, kTransportGap);

        // 5. 3-Dot (⋮) Transport Settings Button
        ImGui::PushID("##transport-settings-dots");
        if (ImGui::InvisibleButton("##tdots", ImVec2{kTransportDotsW, kTransportBtnH})) {
            ImGui::OpenPopup("##transport-row-settings");
        }
        {
            const bool dotsHovered = ImGui::IsItemHovered();
            const ImVec2 dMin = ImGui::GetItemRectMin();
            const ImVec2 dMax = ImGui::GetItemRectMax();
            const ImVec2 dCenter{(dMin.x + dMax.x) * 0.5F, (dMin.y + dMax.y) * 0.5F};
            if (dotsHovered) {
                toolbarDrawList->AddRectFilled(dMin, dMax, IM_COL32(56, 66, 80, 220), 3.0F);
                ImGui::SetTooltip("Playback & Fast-Forward Settings");
            }
            const ImU32 dotCol = dotsHovered ? IM_COL32(245, 248, 255, 255) : IM_COL32(185, 196, 212, 230);
            for (int dot = -1; dot <= 1; ++dot) {
                toolbarDrawList->AddCircleFilled(
                    ImVec2{dCenter.x, dCenter.y + (static_cast<float>(dot) * 4.2F)}, 1.4F, dotCol, 8);
            }
        }
        if (ImGui::BeginPopup("##transport-row-settings")) {
            ImGui::TextDisabled("TRANSPORT & FAST-FORWARD SETTINGS");
            ImGui::Separator();
            ImGui::TextUnformatted("Fast-Forward Speed");
            for (const float mult : {2.0F, 4.0F, 8.0F}) {
                char label[32]{};
                std::snprintf(label, sizeof(label), "%.0fx Speed", static_cast<double>(mult));
                const bool selected = std::abs(m_playFastForwardMultiplier - mult) < 0.05F;
                if (ImGui::MenuItem(label, nullptr, selected)) {
                    SetPlayFastForwardMultiplierUVE(mult);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Step Single Frame", nullptr, false, isPaused)) {
                static_cast<void>(StepPlayModeUVE());
            }
            ImGui::Separator();
            ImGui::Checkbox("Pause on Start", &m_playPauseOnStart);
            ImGui::Checkbox("Save Scene First", &m_playSaveSceneFirst);
            ImGui::EndPopup();
        }
        ImGui::PopID();

        // ---- Row 2: Dedicated Viewport Tool Strip (Gizmos, Views, Snapping, Grid) ---------------
        const float row2Y = toolbarMin.y + kRow1HeightUVE + 3.0F;
        ImGui::SetCursorScreenPos(ImVec2{toolbarMin.x + 8.0F, row2Y});

        const auto drawToolbarIconButton = [&](const char* id, const bool active, const auto& drawIcon, const char* tooltip) {
            ImGui::PushID(id);
            constexpr float kBtnW = 24.0F;
            constexpr float kBtnH = 20.0F;
            const bool pressed = ImGui::InvisibleButton("##btn", ImVec2{kBtnW, kBtnH});
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 bMin = ImGui::GetItemRectMin();
            const ImVec2 bMax = ImGui::GetItemRectMax();
            const ImVec2 bCenter{(bMin.x + bMax.x) * 0.5F, (bMin.y + bMax.y) * 0.5F};
            const ImU32 bgCol = active ? IM_COL32(64, 132, 214, 235)
                                       : (hovered ? IM_COL32(255, 255, 255, 28) : IM_COL32(255, 255, 255, 8));
            toolbarDrawList->AddRectFilled(bMin, bMax, bgCol, 3.0F);
            const ImU32 iconCol = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(214, 220, 230, 220);
            drawIcon(*toolbarDrawList, bCenter, 9.0F, iconCol);
            if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", tooltip);
            }
            ImGui::PopID();
            return pressed;
        };

        if (drawToolbarIconButton("##tool-move", m_viewportOverlayState.gizmoMode == ViewportGizmoModeUVE::Move,
                                  DrawMoveIconUVE, "Move Tool (W)")) {
            m_viewportOverlayState.gizmoMode = ViewportGizmoModeUVE::Move;
        }
        ImGui::SameLine(0.0F, 3.0F);

        if (drawToolbarIconButton("##tool-rotate", m_viewportOverlayState.gizmoMode == ViewportGizmoModeUVE::Rotate,
                                  DrawRotateIconUVE, "Rotate Tool (E)")) {
            m_viewportOverlayState.gizmoMode = ViewportGizmoModeUVE::Rotate;
        }
        ImGui::SameLine(0.0F, 3.0F);

        if (drawToolbarIconButton("##tool-scale", m_viewportOverlayState.gizmoMode == ViewportGizmoModeUVE::Scale,
                                  DrawScaleIconUVE, "Scale Tool (R)")) {
            m_viewportOverlayState.gizmoMode = ViewportGizmoModeUVE::Scale;
        }
        ImGui::SameLine(0.0F, 3.0F);

        if (drawToolbarIconButton("##tool-universal", m_viewportOverlayState.gizmoMode == ViewportGizmoModeUVE::Universal,
                                  DrawUniversalIconUVE, "Universal Tool (T)")) {
            m_viewportOverlayState.gizmoMode = ViewportGizmoModeUVE::Universal;
        }
        ImGui::SameLine(0.0F, 4.0F);

        const std::uintptr_t snapIcon = m_uiAssets.GetGeneralIconTextureIdUVE("snap");
        if (drawToolbarIconButton(
                "##tool-snap", m_viewportOverlayState.snapEnabled,
                [snapIcon](ImDrawList& list, const ImVec2 center, const float radius, const ImU32 col) {
                    if (snapIcon != 0U) {
                        const float half = radius * 0.55F;
                        list.AddImage(static_cast<ImTextureID>(snapIcon),
                                      ImVec2{center.x - half, center.y - half},
                                      ImVec2{center.x + half, center.y + half});
                    } else {
                        list.AddCircle(center, radius * 0.45F, col, 12, 1.2F);
                    }
                },
                "Toggle Snapping")) {
            EditorTransformSnappingSettingsUVE snapping = GetTransformSnappingSettingsUVE();
            snapping.enabled = !m_viewportOverlayState.snapEnabled;
            if (SetTransformSnappingSettingsUVE(snapping)) {
                m_viewportOverlayState.snapEnabled = snapping.enabled;
            }
        }
        ImGui::SameLine(0.0F, 3.0F);

        if (drawToolbarIconButton("##tool-grid", m_viewportOverlayState.gridVisible, DrawGridIconUVE,
                                  "Toggle Grid (Right-click for options)")) {
            static_cast<void>(SetViewportGridUVE(!m_viewportOverlayState.gridVisible,
                                                 m_viewportOverlayState.gridOpacity));
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##toolbar-grid-options");
        }
        if (ImGui::BeginPopup("##toolbar-grid-options")) {
            ImGui::TextDisabled("Grid Options");
            ImGui::Separator();
            bool visible = m_viewportOverlayState.gridVisible;
            float opacityPercent = m_viewportOverlayState.gridOpacity * 100.0F;
            bool changed = ImGui::Checkbox("Show Grid", &visible);
            ImGui::BeginDisabled(!visible);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
            changed |= ImGui::SliderFloat("Opacity", &opacityPercent, kMinimumViewportGridOpacityUVE * 100.0F, 100.0F,
                                          "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
            if (changed) {
                static_cast<void>(SetViewportGridUVE(visible, opacityPercent / 100.0F));
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0F, 6.0F);

        // Vertical separator before view projection dropdown
        const float sep1X = ImGui::GetCursorScreenPos().x;
        toolbarDrawList->AddLine(ImVec2{sep1X, row2Y + 2.0F}, ImVec2{sep1X, row2Y + 18.0F},
                                 IM_COL32(50, 56, 66, 200), 1.0F);
        ImGui::Dummy(ImVec2{6.0F, 20.0F});
        ImGui::SameLine(0.0F, 0.0F);

        // Projection Dropdown (Perspective / Orthographic / Named views)
        const char* const projName = m_viewportOverlayState.orthographic ? "Orthographic" : "Perspective";
        const std::string projText =
            m_viewportOverlayState.view == ViewportViewUVE::User
                ? std::string{projName}
                : std::string{GetViewportViewNameUVE(m_viewportOverlayState.view)} +
                      (m_viewportOverlayState.orthographic ? " (Ortho)" : " (Persp)");
        const std::string projButtonLabel = projText + "   ##toolbar-proj-btn";
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 2.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 37, 45, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 52, 64, 255));
        if (ImGui::Button(projButtonLabel.c_str(), ImVec2{0.0F, 20.0F})) {
            ImGui::OpenPopup("##toolbar-proj-popup");
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        // Subtle downward chevron arrow instead of literal letter 'v'
        {
            const ImVec2 btnMin = ImGui::GetItemRectMin();
            const ImVec2 btnMax = ImGui::GetItemRectMax();
            const float arrowX = btnMax.x - 7.5F;
            const float arrowY = (btnMin.y + btnMax.y) * 0.5F + 0.5F;
            const ImU32 arrowCol = ImGui::IsItemHovered() ? IM_COL32(235, 242, 255, 255) : IM_COL32(150, 162, 178, 200);
            toolbarDrawList->AddTriangleFilled(
                ImVec2{arrowX - 2.5F, arrowY - 1.5F},
                ImVec2{arrowX + 2.5F, arrowY - 1.5F},
                ImVec2{arrowX, arrowY + 1.5F},
                arrowCol);
        }
        if (ImGui::BeginPopup("##toolbar-proj-popup")) {
            if (ImGui::MenuItem("Perspective", nullptr, !m_viewportOverlayState.orthographic && m_viewportOverlayState.view == ViewportViewUVE::User)) {
                static_cast<void>(SetViewportOrthographicUVE(false));
                static_cast<void>(RequestViewportViewUVE(ViewportViewUVE::User));
            }
            if (ImGui::MenuItem("Orthographic", nullptr, m_viewportOverlayState.orthographic && m_viewportOverlayState.view == ViewportViewUVE::User)) {
                static_cast<void>(SetViewportOrthographicUVE(true));
                static_cast<void>(RequestViewportViewUVE(ViewportViewUVE::User));
            }
            ImGui::Separator();
            for (const auto viewMode : {ViewportViewUVE::Top, ViewportViewUVE::Bottom, ViewportViewUVE::Front,
                                        ViewportViewUVE::Back, ViewportViewUVE::Right, ViewportViewUVE::Left}) {
                if (ImGui::MenuItem(GetViewportViewNameUVE(viewMode), nullptr, m_viewportOverlayState.view == viewMode)) {
                    static_cast<void>(RequestViewportViewUVE(viewMode));
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0F, 4.0F);

        // Shading Mode Dropdown (Lit, Unshaded, Wireframe, Lighting, Normals)
        static constexpr std::array<const char*, 5> kShadingNames{"Lit", "Unshaded", "Wireframe", "Lighting", "Normals"};
        const char* const shadingName =
            kShadingNames[std::clamp(static_cast<std::size_t>(m_viewportOverlayState.shadingMode), std::size_t{0U},
                                     kShadingNames.size() - 1U)];
        const std::string shadingButtonLabel = std::string{shadingName} + "   ##toolbar-shading-btn";
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 2.0F});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 37, 45, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 52, 64, 255));
        if (ImGui::Button(shadingButtonLabel.c_str(), ImVec2{0.0F, 20.0F})) {
            ImGui::OpenPopup("##toolbar-shading-popup");
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        // Subtle downward chevron arrow instead of literal letter 'v'
        {
            const ImVec2 btnMin = ImGui::GetItemRectMin();
            const ImVec2 btnMax = ImGui::GetItemRectMax();
            const float arrowX = btnMax.x - 7.5F;
            const float arrowY = (btnMin.y + btnMax.y) * 0.5F + 0.5F;
            const ImU32 arrowCol = ImGui::IsItemHovered() ? IM_COL32(235, 242, 255, 255) : IM_COL32(150, 162, 178, 200);
            toolbarDrawList->AddTriangleFilled(
                ImVec2{arrowX - 2.5F, arrowY - 1.5F},
                ImVec2{arrowX + 2.5F, arrowY - 1.5F},
                ImVec2{arrowX, arrowY + 1.5F},
                arrowCol);
        }
        if (ImGui::BeginPopup("##toolbar-shading-popup")) {
            for (std::size_t i = 0U; i < kShadingNames.size(); ++i) {
                if (ImGui::MenuItem(kShadingNames[i], nullptr, m_viewportOverlayState.shadingMode == static_cast<int>(i))) {
                    m_viewportOverlayState.shadingMode = static_cast<int>(i);
                }
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(0.0F, 6.0F);
        // Vertical separator before lighting and view toggles
        const float sep2X = ImGui::GetCursorScreenPos().x;
        toolbarDrawList->AddLine(ImVec2{sep2X, row2Y + 2.0F}, ImVec2{sep2X, row2Y + 18.0F},
                                 IM_COL32(50, 56, 66, 200), 1.0F);
        ImGui::Dummy(ImVec2{6.0F, 20.0F});
        ImGui::SameLine(0.0F, 0.0F);

        // Preview Sun toggle
        if (drawToolbarIconButton(
                "##tool-sun", m_viewportOverlayState.previewSun,
                [](ImDrawList& list, const ImVec2 center, const float, const ImU32) {
                    constexpr float kSunR = 4.0F;
                    list.AddCircle(center, kSunR, IM_COL32(240, 226, 180, 255), 16, 1.5F);
                    for (int ray = 0; ray < 8; ++ray) {
                        const float angle = static_cast<float>(ray) * 0.785398163F;
                        const ImVec2 from{center.x + (std::cos(angle) * (kSunR + 1.5F)),
                                          center.y + (std::sin(angle) * (kSunR + 1.5F))};
                        const ImVec2 to{center.x + (std::cos(angle) * (kSunR + 3.5F)),
                                        center.y + (std::sin(angle) * (kSunR + 3.5F))};
                        list.AddLine(from, to, IM_COL32(240, 226, 180, 215), 1.2F);
                    }
                },
                "Preview Sun (Toggle directional light)")) {
            m_viewportOverlayState.previewSun = !m_viewportOverlayState.previewSun;
        }
        ImGui::SameLine(0.0F, 3.0F);

        // Preview Environment / Sky toggle
        if (drawToolbarIconButton(
                "##tool-sky", m_viewportOverlayState.previewEnvironment,
                [](ImDrawList& list, const ImVec2 center, const float, const ImU32) {
                    constexpr float kSkyR = 5.0F;
                    list.AddCircle(center, kSkyR, IM_COL32(168, 200, 238, 230), 16, 1.3F);
                    list.AddLine(ImVec2{center.x - kSkyR, center.y}, ImVec2{center.x + kSkyR, center.y},
                                 IM_COL32(168, 200, 238, 230), 1.2F);
                    list.PathArcTo(center, kSkyR - 0.5F, 0.0F, 3.14159265F, 16);
                    list.PathFillConvex(IM_COL32(96, 140, 196, 110));
                },
                "Preview Environment (Toggle sky/environment)")) {
            m_viewportOverlayState.previewEnvironment = !m_viewportOverlayState.previewEnvironment;
        }
        ImGui::SameLine(0.0F, 3.0F);

        // Sun & Environment options popup
        if (drawToolbarIconButton(
                "##tool-sun-sky-opts", false,
                [](ImDrawList& list, const ImVec2 center, const float, const ImU32) {
                    for (int dot = -1; dot <= 1; ++dot) {
                        list.AddCircleFilled(ImVec2{center.x + (static_cast<float>(dot) * 4.0F), center.y}, 1.4F,
                                             IM_COL32(214, 220, 230, 235), 8);
                    }
                },
                "Sun and Environment settings")) {
            ImGui::OpenPopup("##toolbar-sun-env-popup");
        }
        if (ImGui::BeginPopup("##toolbar-sun-env-popup")) {
            ImGui::TextDisabled("PREVIEW SUN & ENVIRONMENT");
            ImGui::Separator();
            ImGui::Checkbox("Preview Sun", &m_viewportOverlayState.previewSun);
            ImGui::Checkbox("Preview Environment", &m_viewportOverlayState.previewEnvironment);
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0F, 3.0F);

        // 2D / 3D mode switch
        if (drawToolbarIconButton(
                "##tool-2d-mode", m_viewportOverlayState.is2DMode,
                DrawViewportModeIconUVE,
                "Toggle 2D / 3D Mode")) {
            m_viewportOverlayState.is2DMode = !m_viewportOverlayState.is2DMode;
        }

        ImGui::End();
    }

    // The Outliner's tab row: empty dark background outside the rounded Outliner tab itself
    if (sidePanelsShown && m_scenePanelVisible && stripLayout.outlinerHeaderSize.x > 0.0F) {
        ImGui::SetNextWindowPos(stripLayout.outlinerHeaderPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(stripLayout.outlinerHeaderSize, ImGuiCond_Always);
        if (beginChromeWindow("##uve-outliner-header", chromeFlags)) {
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 max{min.x + ImGui::GetWindowWidth(), min.y + stripLayout.outlinerHeaderSize.y};
            drawList->AddRectFilled(min, max, kEditorHeaderStripColorUVE);

            // Tab badge for Outliner flush with top-left and merging into the Outliner panel below
            const ImVec2 tabMin{min.x, min.y};
            const float tabW = ImGui::CalcTextSize("Outliner").x + 28.0F;
            const ImVec2 tabMax{tabMin.x + tabW, max.y + 1.0F};
            DrawActiveTopRoundedTabUVE(*drawList, tabMin, tabMax, IM_COL32(27, 30, 33, 255), 6.0F);
            drawList->AddText(ImVec2{tabMin.x + 12.0F, min.y + (stripLayout.outlinerHeaderSize.y - ImGui::GetTextLineHeight()) * 0.5F},
                              IM_COL32(235, 240, 248, 255), "Outliner");
            ImGui::End();
        }
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{1.0F, 1.0F});
    const auto beginHandle = [mainViewport](const char* const id, const ImVec2 pos, const ImVec2 size) {
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0F);
        const bool began = ImGui::Begin(id, nullptr, handleFlags);
        if (began) {
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        }
        return began;
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
                ImGui::SetTooltip("Drag to resize Outliner\nDouble-click to reset");
            }
            const ImVec2 gripMin = ImGui::GetItemRectMin();
            const ImVec2 gripMax = ImGui::GetItemRectMax();
            const float seamX = (gripMin.x + gripMax.x) * 0.5F;
            const ImU32 lineColor = hot
                                        ? (ImGui::IsItemActive() ? IM_COL32(14, 112, 227, 255) : IM_COL32(56, 140, 240, 230))
                                        : IM_COL32(44, 48, 56, 220);
            const float halfThickness = hot ? 1.5F : 0.5F;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - halfThickness, gripMin.y},
                                                      ImVec2{seamX + halfThickness, gripMax.y},
                                                      lineColor);
            const float midY = (gripMin.y + gripMax.y) * 0.5F;
            const ImU32 handleColor = hot
                                          ? (ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 255) : IM_COL32(14, 112, 227, 255))
                                          : IM_COL32(70, 80, 94, 200);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - 1.5F, midY - 16.0F},
                                                      ImVec2{seamX + 1.5F, midY + 16.0F},
                                                      handleColor, 1.5F);
            if (ImGui::IsItemActive()) {
                const float newWidth = io.MousePos.x - mainViewport->WorkPos.x;
                m_scenePanelWidth = ClampScenePanelWidthUVE(totalWidth, newWidth);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_scenePanelWidth = totalWidth * kScenePanelWidthFractionUVE;
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
                ImGui::SetTooltip("Drag to resize Inspector\nDouble-click to reset");
            }
            const ImVec2 gripMin = ImGui::GetItemRectMin();
            const ImVec2 gripMax = ImGui::GetItemRectMax();
            const float seamX = (gripMin.x + gripMax.x) * 0.5F;
            const ImU32 lineColor = hot
                                        ? (ImGui::IsItemActive() ? IM_COL32(14, 112, 227, 255) : IM_COL32(56, 140, 240, 230))
                                        : IM_COL32(44, 48, 56, 220);
            const float halfThickness = hot ? 1.5F : 0.5F;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - halfThickness, gripMin.y},
                                                      ImVec2{seamX + halfThickness, gripMax.y},
                                                      lineColor);
            const float midY = (gripMin.y + gripMax.y) * 0.5F;
            const ImU32 handleColor = hot
                                          ? (ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 255) : IM_COL32(14, 112, 227, 255))
                                          : IM_COL32(70, 80, 94, 200);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{seamX - 1.5F, midY - 16.0F},
                                                      ImVec2{seamX + 1.5F, midY + 16.0F},
                                                      handleColor, 1.5F);
            if (ImGui::IsItemActive()) {
                const float newWidth = (mainViewport->WorkPos.x + totalWidth) - io.MousePos.x;
                m_inspectorPanelWidth = ClampInspectorPanelWidthUVE(totalWidth, newWidth);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_inspectorPanelWidth = totalWidth * kInspectorPanelWidthFractionUVE;
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
                ImGui::SetTooltip("Drag to resize Bottom Dock\nDouble-click to reset");
            }
            const ImVec2 gripMin = ImGui::GetItemRectMin();
            const ImVec2 gripMax = ImGui::GetItemRectMax();
            const float seamY = (gripMin.y + gripMax.y) * 0.5F;
            const ImU32 lineColor = hot
                                        ? (ImGui::IsItemActive() ? IM_COL32(14, 112, 227, 255) : IM_COL32(56, 140, 240, 230))
                                        : IM_COL32(44, 48, 56, 220);
            const float halfThickness = hot ? 1.5F : 0.5F;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{gripMin.x, seamY - halfThickness},
                                                      ImVec2{gripMax.x, seamY + halfThickness},
                                                      lineColor);
            const float midX = (gripMin.x + gripMax.x) * 0.5F;
            const ImU32 handleColor = hot
                                          ? (ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 255) : IM_COL32(14, 112, 227, 255))
                                          : IM_COL32(70, 80, 94, 200);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2{midX - 22.0F, seamY - 1.5F},
                                                      ImVec2{midX + 22.0F, seamY + 1.5F},
                                                      handleColor, 1.5F);
            if (ImGui::IsItemActive()) {
                const float availableHeight = mainViewport->WorkSize.y - kEditorTopChromeHeightUVE;
                const float newHeight = (mainViewport->WorkPos.y + mainViewport->WorkSize.y) - io.MousePos.y;
                m_bottomDockHeight = ClampBottomDockHeightUVE(availableHeight, newHeight);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_bottomDockHeight = kAssetsPanelHeightUVE;
            }
        }
        ImGui::End();
    } else if (!m_bottomDockVisible) {
        // When bottom dock is collapsed, allow dragging up on the top edge of the tab bar in the center
        // column under the Viewport to open it
        const ImVec2 tabGripPos{layout.viewportPos.x, layout.dockTabBarPos.y - (kPanelSplitterThicknessUVE * 0.5F)};
        const ImVec2 tabGripSize{layout.viewportSize.x, kPanelSplitterThicknessUVE};
        if (beginHandle("##bottom-dock-tab-open-grip", tabGripPos, tabGripSize)) {
            ImGui::InvisibleButton("##grip-dock-open", tabGripSize);
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                ImGui::SetTooltip("Drag up to open Bottom Dock");
            }
            if (ImGui::IsItemActive()) {
                const float availableHeight = mainViewport->WorkSize.y - kEditorTopChromeHeightUVE;
                const float requestedHeight = (mainViewport->WorkPos.y + mainViewport->WorkSize.y) - io.MousePos.y;
                if (requestedHeight > kDockTabBarHeightUVE + 20.0F) {
                    m_bottomDockVisible = true;
                    m_bottomDockHeight = ClampBottomDockHeightUVE(availableHeight, requestedHeight);
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
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
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
            if (active) {
                ImDrawList* const dockDrawList = ImGui::GetWindowDrawList();
                const ImVec2 tabMin = ImGui::GetItemRectMin();
                const ImVec2 tabMax{ImGui::GetItemRectMax().x, ImGui::GetWindowPos().y + layout.dockTabBarSize.y};
                DrawActiveTopRoundedTabUVE(*dockDrawList, tabMin, tabMax, IM_COL32(28, 33, 40, 255), 5.0F);
                const ImVec2 labelSize = ImGui::CalcTextSize(tab.label);
                dockDrawList->AddText(
                    ImVec2{(tabMin.x + tabMax.x - labelSize.x) * 0.5F,
                           (tabMin.y + ImGui::GetItemRectMax().y - labelSize.y) * 0.5F},
                    ImGui::GetColorU32(ImGuiCol_Text), tab.label);
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
