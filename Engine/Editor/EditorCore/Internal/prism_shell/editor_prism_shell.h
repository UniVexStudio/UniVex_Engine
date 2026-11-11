// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_prism_chrome.h"
#include "editor_prism_content_browser.h"
#include "editor_prism_details.h"
#include "editor_prism_dock_panels.h"
#include "editor_prism_entity_editor.h"
#include "editor_prism_file_dialog.h"
#include "editor_prism_host.h"
#include "editor_prism_icons.h"
#include "editor_prism_view_toolbar.h"
#include "editor_prism_viewport_gizmos.h"

#include "entity.h"

#include "prism/Menu.h"
#include "prism/Widget.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct GLFWwindow;

namespace prism {
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

class EditorUVE;

/// The Level Editor built with PRISM (the layout of the editor design sheet):
///
///   title bar (logo tab, drag strip, minimize / maximize / close)
///   sub row 1: open folder, save, selection mode, blueprint (beta) | play / pause / stop
///   [ Outliner          | Viewport tab, sub row 2 (gizmo / view settings), viewport | Details   ]
///   [ + / filter / search| dock area (Content Browser, Output Log, ...)               | search    ]
///   dock bar
///
/// It is a thin view over EditorUVE: every action goes through the same EditorUVE entry points
/// the previous shell used, and the panels re-read editor state each frame.
class EditorPrismShellUVE final {
public:
    explicit EditorPrismShellUVE(EditorUVE& editor);
    ~EditorPrismShellUVE();
    EditorPrismShellUVE(const EditorPrismShellUVE&) = delete;
    EditorPrismShellUVE& operator=(const EditorPrismShellUVE&) = delete;

    [[nodiscard]] bool InitializeUVE(GLFWwindow* window, const std::filesystem::path& assetDir);
    void ShutdownUVE();
    [[nodiscard]] bool IsReadyUVE() const noexcept { return m_host.IsReadyUVE(); }

    /// One editor frame: input -> layout -> viewport render -> UI render -> composite.
    void FrameUVE();

    bool SaveScreenshotUVE(const std::filesystem::path& path) { return m_host.SaveScreenshotUVE(path); }
    /// Saves the Entity Editor window's frame (false while no entity is open).
    bool SaveEntityEditorScreenshotUVE(const std::filesystem::path& path);
    void SetForeignInputUVE(std::function<bool()> wantsMouse, std::function<bool()> wantsKeyboard) {
        m_host.SetForeignInputUVE(std::move(wantsMouse), std::move(wantsKeyboard));
    }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildLayoutUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBarUVE();
    [[nodiscard]] std::vector<prism::MenuItem> CommandItemsUVE(std::initializer_list<const char*> ids);
    [[nodiscard]] bool DispatchShortcutUVE(prism::Key key, std::uint8_t mods);
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarRowUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildOutlinerUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildViewportUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDetailsUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockBarUVE();
    void ShowDockPanelUVE(int index, bool forceVisible);
    /// Shows a side column only while it holds a panel (a panel can be docked elsewhere) and the
    /// Window menu has not hidden it.
    void UpdateDockColumnsUVE();
    /// Puts the Outliner and Details back in their own columns and closes split / floating areas.
    void ResetPanelLayoutUVE();
    /// Window > Layout: 0 Default, 1 Details under Outliner, 2 Details beside Outliner, 3 Wide Viewport.
    void ApplyLayoutPresetUVE(int preset);

public:
    /// Renders the selected camera's Screen of `details` (when it is shown) and adds it to `views`.
    static void AppendCameraScreenUVE(EditorUVE& editor, const EditorPrismDetailsUVE& details, CameraScreenUVE* screen,
                                      std::vector<EditorPrismHostUVE::ViewImageUVE>& views);
    /// Places content dropped on the Outliner (entity assets, models) under the row at `at`.
    bool DropContentOnOutlinerUVE(const std::string& relativePath, prism::Vec2 at);

private:

    void ShowAddMenuUVE();
    void ShowAddFilterMenuUVE();
    void RefreshOutlinerUVE();
    void RefreshDetailsUVE();
    void RefreshToolbarUVE();
    [[nodiscard]] std::uint64_t OutlinerSignatureUVE();
    [[nodiscard]] int TreeIdForUVE(Scene::EntityUVE entity);
    [[nodiscard]] std::vector<Scene::EntityUVE> OutlinerRootsUVE();
    [[nodiscard]] Scene::EntityUVE EntityForTreeIdUVE(int id) const;

    EditorUVE& m_editor;
    // Outlives the host's widget tree (declared first, destroyed last): the TabViews it wires call back into it.
    std::unique_ptr<prism::DockSpace> m_dockSpace;
    EditorPrismHostUVE m_host;
    // The Entity Editor's window (opened while an entity is open); shares the host.
    EditorPrismEntityEditorUVE m_entityEditor;
    // The file window every Load / Quick Import opens.
    EditorPrismFileDialogUVE m_fileDialog;
    EditorPrismContentBrowserUVE m_contentBrowser;
    EditorPrismDetailsUVE m_details2;
    EditorPrismDockPanelsUVE m_dockPanels;
    EditorPrismIconsUVE m_icons;
    EditorPrismViewToolbarUVE m_viewToolbar;

    // Widgets the frame updates.
    std::shared_ptr<prism::TreeView> m_outliner;
    std::shared_ptr<prism::TextInput> m_outlinerSearch;
    std::shared_ptr<prism::Button> m_addButton;
    std::shared_ptr<prism::Button> m_filterButton;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<EditorPrismViewportGizmosUVE> m_gizmos;
    std::shared_ptr<CameraScreenUVE> m_cameraScreen;
    std::shared_ptr<prism::ComboBox> m_selectionMode;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_pauseButton;
    std::shared_ptr<prism::Button> m_stopButton;
    std::shared_ptr<prism::PropertyGrid> m_details;
    std::shared_ptr<prism::TextInput> m_detailsSearch;
    std::shared_ptr<prism::Label> m_detailsTitle;
    std::shared_ptr<prism::Label> m_statusLabel;
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
    std::vector<Scene::EntityUVE> m_treeEntities;
    std::uint64_t m_outlinerSignature = 0;
    bool m_syncingSelection = false;

    // Details panel follows the selection.
    Scene::EntityUVE m_detailsEntity = Scene::kInvalidEntityUVE;
    std::uint64_t m_detailsSignature = 0;
    std::string m_detailsFilter;

    // Kinds the Outliner '+' offers (filter #8). Empty set = the default trio.
    std::set<int> m_addKinds;
    bool m_addAllKinds = false;

    bool m_dockVisible = true;
};

} // namespace UVE::Editor
