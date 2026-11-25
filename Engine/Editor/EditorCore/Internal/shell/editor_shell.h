// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_chrome.h"
#include "editor_asset_table.h"
#include "editor_details.h"
#include "editor_dock_panels.h"
#include "editor_entity_editor.h"
#include "editor_entity_class_window.h"
#include "editor_montage_editor.h"
#include "vfx/editor_vfx_editor.h"
#include "editor_retarget_editor.h"
#include "editor_animation_library_editor.h"
#include "editor_file_dialog.h"
#include "editor_host.h"
#include "editor_icons.h"
#include "editor_loading_screen.h"
#include "editor_settings_window.h"
#include "editor_shortcuts_window.h"
#include "editor_input_map_window.h"
#include "editor_tools_windows.h"
#include "editor_view_toolbar.h"
#include "editor_viewport_gizmos.h"

#include "entity.h"

#include "prism/Menu.h"
#include "prism/Widget.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct GLFWwindow;

namespace prism {
class Border;
class Button;
class DockSpace;
class EngineView;
class Label;
class PropertyGrid;
class Segmented;
class TabView;
class TextInput;
class TreeView;
class StackSwitcher;
class ComboBox;
class Widget;
} // namespace prism

namespace UVE::Editor {

class Editor;

/// The Level Editor built with PRISM (the layout of the editor design sheet):
///
///   title bar (logo tab, drag strip, minimize / maximize / close)
///   sub row 1: open folder, save, selection mode, blueprint (beta) | play / pause / stop
///   [ Outliner          | Viewport tab, sub row 2 (gizmo / view settings), viewport | Details   ]
///   [ + / filter / search| dock area (Asset Table, Output Log, ...)               | search    ]
///   dock bar
///
/// It is a thin view over Editor: every action goes through the same Editor entry points
/// the previous shell used, and the panels re-read editor state each frame.
class EditorShell final {
public:
    explicit EditorShell(Editor& editor);
    ~EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell& operator=(const EditorShell&) = delete;

    [[nodiscard]] bool Initialize(GLFWwindow* window, const std::filesystem::path& assetDir);
    void Shutdown();
    [[nodiscard]] bool IsReady() const noexcept { return m_host.IsReady(); }

    /// One editor frame: input -> layout -> viewport render -> UI render -> composite.
    void Frame();

    bool SaveScreenshot(const std::filesystem::path& path) { return m_host.SaveScreenshot(path); }
    /// Saves the Entity Editor window's frame (false while no entity is open).
    bool SaveEntityEditorScreenshot(const std::filesystem::path& path);
    bool SaveVfxEditorScreenshot(const std::filesystem::path& path);

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildLayout();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBar();
    [[nodiscard]] std::vector<prism::MenuItem> CommandItems(std::initializer_list<const char*> ids);
    [[nodiscard]] bool DispatchShortcut(prism::Key key, std::uint8_t mods);
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarRow();
    [[nodiscard]] prism::Ref<prism::Widget> BuildOutliner();
    [[nodiscard]] prism::Ref<prism::Widget> BuildViewport();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDetails();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDock();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockBar();
    void ShowDockPanel(int index, bool forceVisible);
    /// Shows a side column only while it holds a panel (a panel can be docked elsewhere) and the
    /// Window menu has not hidden it.
    void UpdateDockColumns();
    /// Puts the Outliner and Details back in their own columns and closes split / floating areas.
    void ResetPanelLayout();
    /// Window > Layout: 0 Default, 1 Details under Outliner, 2 Details beside Outliner, 3 Wide Viewport.
    void ApplyLayoutPreset(int preset);

public:
    /// Renders the selected camera's Screen of `details` (when it is shown) and adds it to `views`.
    static void AppendCameraScreen(Editor& editor, const EditorDetails& details, CameraScreen* screen,
                                      std::vector<EditorHost::ViewImage>& views);
    /// Places content dropped on the Outliner (entity assets, models) under the row at `at`.
    bool DropContentOnOutliner(const std::string& relativePath, prism::Vec2 at);

private:

    void ShowAddMenu();
    /// The command palette (Ctrl+Shift+P): every command, searchable, the recent ones first.
    void ShowCommandPalette();
    void RefreshOutliner();
    void RefreshDetails();
    void RefreshToolbar();
    [[nodiscard]] std::uint64_t OutlinerSignature();
    [[nodiscard]] int TreeIdFor(Scene::Entity entity);
    [[nodiscard]] std::vector<Scene::Entity> OutlinerRoots();
    [[nodiscard]] Scene::Entity EntityForTreeId(int id) const;

    Editor& m_editor;
    // Outlives the host's widget tree (declared first, destroyed last): the TabViews it wires call back into it.
    std::unique_ptr<prism::DockSpace> m_dockSpace;
    EditorHost m_host;

public:
    [[nodiscard]] bool IsAnyWindowFocused() const { return m_host.IsAnyWindowFocused(); }

private:
    // The Entity Editor's window (opened while an entity is open); shares the host.
    EditorEntityEditor m_entityEditor;
    // The file window every Load / Import opens.
    EditorFileDialog m_fileDialog;
    // "+ Add > Entity > Entity Class...": the window that picks a new entity's class.
    EditorEntityClassWindow m_entityClassWindow;
    EditorSettingsWindow m_preferencesWindow;
    EditorSettingsWindow m_projectSettingsWindow;
    EditorShortcutsWindow m_shortcutsWindow;
    EditorInputMapWindow m_inputMapWindow;
    EditorNetworkCaptureWindow m_captureWindow;
    EditorAudioMixerWindow m_audioMixerWindow;
    // The Motion Montage Editor's window (opened while a montage is open).
    EditorMontageEditor m_montageEditor;
    EditorVfxEditor m_vfxEditor;
    EditorRetargetWindow m_retargetEditor;
    EditorAnimationLibraryWindow m_animationLibraryEditor;
    EditorAssetTable m_assetTable;
    EditorDetails m_details2;
    EditorDockPanels m_dockPanels;
    EditorIcons m_icons;
    EditorViewToolbar m_viewToolbar;
    prism::Ref<EditorLoadingScreen> m_loadingScreen;
    std::string m_projectName;
    bool m_startupCovered = false;
    std::chrono::steady_clock::time_point m_viewWarmBegan{};
    int m_viewWarmFrame = 0;
    std::size_t m_renderLoadPeak = 0U;
    bool m_renderLoadDone = false;
    std::uint64_t m_outlinerSelection = 0U;
    double m_frameWorkSeconds = 0.0;
    std::size_t m_frameCount = 0U;
    std::chrono::steady_clock::time_point m_frameReportAt{};
    std::chrono::steady_clock::time_point m_outlinerCheckedAt{};
    bool m_uploadBudgetSet = false;
    std::vector<std::string> m_renderLoadNames;
    EditorHost::ViewImage m_lastView{};

    // Widgets the frame updates.
    std::shared_ptr<prism::Border> m_safeFrame;
    std::shared_ptr<prism::Box> m_titleBarRow;
    std::array<float, 4> m_safeArea{0.0F, 0.0F, 0.0F, 0.0F};
    std::shared_ptr<prism::TreeView> m_outliner;
    std::shared_ptr<prism::TextInput> m_outlinerSearch;
    std::shared_ptr<prism::Button> m_addButton;
    std::shared_ptr<prism::Button> m_filterButton;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<EditorViewportGizmos> m_gizmos;
    std::shared_ptr<CameraScreen> m_cameraScreen;
    std::shared_ptr<prism::ComboBox> m_selectionMode;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_pauseButton;
    std::shared_ptr<prism::Button> m_stopButton;
    std::shared_ptr<prism::PropertyGrid> m_details;
    std::shared_ptr<prism::TextInput> m_detailsSearch;
    std::shared_ptr<prism::Label> m_detailsTitle;
    std::shared_ptr<prism::Label> m_statusLabel;
    std::shared_ptr<prism::Button> m_rendererProfileButton;
    std::shared_ptr<prism::Widget> m_dockArea;
    std::shared_ptr<prism::StackSwitcher> m_dockPages;
    std::vector<std::shared_ptr<prism::Button>> m_dockTabs;
    std::shared_ptr<prism::Widget> m_viewportNameTab;
    std::shared_ptr<prism::Widget> m_outlinerColumn;
    std::shared_ptr<prism::Widget> m_detailsColumn;
    // The two permanent dock areas; panels can be dragged between them, split off or floated.
    std::shared_ptr<prism::TabView> m_outlinerTabs;
    std::shared_ptr<prism::TabView> m_detailsTabs;

    // Outliner tree id <-> entity (TreeView ids are ints; 0 means "no id").
    std::vector<Scene::Entity> m_treeEntities;
    std::uint64_t m_outlinerSignature = 0;
    bool m_syncingSelection = false;

    // Details panel follows the selection.
    Scene::Entity m_detailsEntity = Scene::kInvalidEntity;
    std::uint64_t m_detailsSignature = 0;
    std::string m_detailsFilter;

    // Kinds the Outliner '+' offers (filter #8). Empty set = the default trio.
    /// The kinds placed last from the object menu, newest first.
    std::vector<int> m_recentKinds;
    bool m_addAllKinds = false;

    bool m_dockVisible = true;
};

} // namespace UVE::Editor
