// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_details.h"
#include "editor_dock_panels.h"
#include "editor_sequencer_panel.h"
#include "editor_anim_graph_panel.h"
#include "editor_blueprint_panel.h"
#include "editor_icons.h"
#include "editor_view_toolbar.h"
#include "editor_viewport_gizmos.h"
#include "editor_widget_designer.h"
#include "editor_asset_table.h"

#include "entity.h"

#include "prism/Widget.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class Button;
class EngineView;
class Label;
class TabView;
class TableView;
class TextInput;
class TreeView;
class Splitter;
class StackSwitcher;
class Box;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;
class DocumentTab;

/// The Entity Editor built with PRISM, in a window of its own beside the level editor, over the
/// open entity's own scene (Editor's entity document; the level stays open and editable):
///
///   [#1 logo] [title bar: File Edit Window Tools Build Help | drag strip | - [] x]
///   [       ] [#2 the entity's tab                                               ]
///   sub row 1: compile + its result, save, revert | browse, find, diff | class settings | simulate
///   sub row 2: the viewport settings (gizmo mode, snap, grid, projection, shading, preview)
///   [ Components (the entity's objects)  | viewport                     ]
///   [ ---------------------------------- |                              ]
///   [ Details                            | compile results (when any)   ]
///
/// Everything it shows and does is the editor's: the entity session (open / save / revert /
/// compile / close), the document's objects, selection and undo, the Details rows, and the
/// dedicated entity viewport (`EditorViewports::entity`). It reads and acts inside the entity's scope.
class EditorEntityEditor final {
public:
    EditorEntityEditor(Editor& editor, EditorHost& host);
    ~EditorEntityEditor();
    EditorEntityEditor(const EditorEntityEditor&) = delete;
    EditorEntityEditor& operator=(const EditorEntityEditor&) = delete;

    /// "Browse" shows the open file in the level editor's Asset Table through this.
    std::function<void(const std::filesystem::path&)> onBrowse;

    /// Opens the window when an entity is open and closes it when the session ends; turns the
    /// window's close button into a close request. Call with the level active, before BeginFrame.
    void Sync();
    /// Re-reads the entity document into the panels (inside the entity's scope).
    void Refresh();
    /// Renders the entity's view and presents the window (inside the entity's scope, after BeginFrame).
    void Render();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }
    bool SaveScreenshot(const std::filesystem::path& path);

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarRow();
    [[nodiscard]] prism::Ref<prism::Widget> BuildComponents();
    [[nodiscard]] prism::Ref<prism::Widget> BuildCentre();
    void ShowAddMenu(Scene::Entity explicitParent = Scene::kInvalidEntity);
    void ShowClassSettings(prism::Rect anchor);
    void RunFind(const std::string& query);
    void RunDiff();
    /// Shows the results area under the view on `tab` (0 Compile Results, 1 Find Results, 2 Diff).
    void ShowResults(int tab);
    void RequestClose();
    void Compile();
    void RefreshComponents();
    void RefreshToolbar();
    void RefreshProblems();
    [[nodiscard]] Scene::Entity EntityForTreeId(int id) const;

    Editor& m_editor;
    EditorHost& m_host;
    EditorDetails m_details;
    EditorViewToolbar m_viewToolbar;
    EditorIcons m_icons;
    int m_window = 0;
    std::filesystem::path m_openPath;
    bool m_closeAsked = false;

    std::shared_ptr<DocumentTab> m_tab;
    std::shared_ptr<prism::Label> m_compileState;
    std::shared_ptr<prism::Label> m_statusLabel;
    std::shared_ptr<prism::Button> m_revertButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_pauseButton;
    std::shared_ptr<prism::Button> m_stopButton;
    std::chrono::steady_clock::time_point m_lastTick{};
    std::shared_ptr<prism::Button> m_addButton;
    std::shared_ptr<prism::TreeView> m_components;
    std::shared_ptr<prism::TextInput> m_componentsSearch;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<EditorViewportGizmos> m_gizmos;
    std::shared_ptr<class CameraScreen> m_cameraScreen;
    std::shared_ptr<prism::Widget> m_problemsPanel;
    /// The dock, as the level's: pages over the view's foot, a bar of tabs along the window's bottom.
    /// Pages: Animation Sequencer and Animation Graph (their tabs show while the entity has one),
    /// Output Log, Console, Compile Results, Find Results, Diff.
    enum DockPage : int { kSequencerPage, kGraphPage, kOutputLogPage, kConsolePage, kCompilePage,
                             kFindPage, kDiffPage, kContentPage, kDockPageCount };
    /// The project's content in the entity window: a drawer that slides up over the bottom (its
    /// button on the dock bar, or Ctrl+Space), or docked as a tab of the dock (Dock in Layout).
    EditorAssetTable m_assetTable;
    prism::Ref<prism::Widget> m_contentFrame;
    std::shared_ptr<prism::Border> m_contentDockHolder;
    std::shared_ptr<prism::Button> m_contentDockToggle;
    std::shared_ptr<prism::Button> m_drawerButton;
    std::shared_ptr<prism::Widget> m_drawerPopup;
    bool m_contentDocked = false;
    [[nodiscard]] prism::Ref<prism::Widget> BuildContentFrame();
    void ToggleContentDrawer();
    void CloseContentDrawer();
    void SetContentDocked(bool docked);
    std::shared_ptr<prism::StackSwitcher> m_dockPages;
    std::shared_ptr<prism::Widget> m_dockArea;
    std::vector<std::shared_ptr<prism::Button>> m_dockTabs;
    bool m_dockVisible = true;
    EditorDockPanels m_dockPanels;
    EditorSequencerPanel m_sequencer;
    EditorAnimGraphPanel m_animGraphPanel;
    /// The Blueprint page: one object's Blueprint Class, picked from the sub row's Blueprint button.
    EditorBlueprintPanel m_blueprintPanel;
    std::shared_ptr<prism::Button> m_blueprintButton;
    bool m_blueprintDetailsShown = false;
    /// Blueprint mode: the graph takes the whole centre (no viewport, no dock); Components and
    /// Details stay on the left.
    std::shared_ptr<prism::StackSwitcher> m_centreStack;
    std::shared_ptr<prism::Widget> m_leftColumn;   // Components + Details
    bool m_centreFocus = false;                    // the Designer / Graph alone, full width
    void SetCentreFocus(bool on);
    std::shared_ptr<prism::Widget> m_dockBar;
    bool m_blueprintMode = false;
    /// The sub row's trail beside Simulate: Viewport | <folder> > BP_A > BP_B ..., one crumb per
    /// Blueprint opened in this entity (the Blueprint button adds them).
    struct OpenBlueprintEntry {
        std::string path;
        Scene::Entity target = Scene::kInvalidEntity;
    };
    std::vector<OpenBlueprintEntry> m_openBlueprints;
    std::shared_ptr<prism::Box> m_crumbRow;
    std::string m_crumbSignature;
    void SyncBlueprintCrumbs();
    void CloseBlueprint(const std::string& path);
    void SetBlueprintMode(bool on);
    [[nodiscard]] prism::Ref<prism::Widget> BuildBlueprintView();
    void SaveAll();
    /// The sub row's Blueprint button: the entity's objects (the root and its children), each to
    /// open its Blueprint or make one.
    void ShowBlueprintMenu();
    /// Makes a Blueprint Class at `file` on the object named by `objectPath` (names from the root
    /// down), opens it and saves the entity: the Asset Table's Add > Blueprint Class.
    void CreateBlueprintOn(const std::filesystem::path& file, const std::vector<std::string>& objectPath);
    /// Whether a Script slot's Blueprint file is still there.
    [[nodiscard]] bool BlueprintFileExists(const std::string& path) const;
    /// The Asset Table deleted `absolute` (a file or folder): graphs from it close unsaved and
    /// the open entity's Script slots that named it empty.
    void ForgetDeletedContent(const std::filesystem::path& absolute);
    /// The create dialog: the Blueprint's name and the folder it goes in.
    void ShowCreateBlueprint(Scene::Entity target);
    void OpenBlueprint(const std::string& path, Scene::Entity target);
    void SyncBlueprintTab();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockBar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSequencerPage();
    [[nodiscard]] prism::Ref<prism::Widget> BuildGraphPage();
    /// Shows `index` (the active tab again hides the dock unless `forceVisible`).
    void ShowDockPanel(int index, bool forceVisible);
    void SyncAnimationTabs();
    /// Full: the dock fills the viewport's space (a Viewport button brings the default back).
    void SetDockFull(bool full);
    void SyncGraphDetails(bool graphShown);
    std::shared_ptr<prism::TabView> m_detailsTabs;
    std::shared_ptr<prism::Splitter> m_centreSplit;
    std::shared_ptr<prism::Widget> m_viewArea;
    std::shared_ptr<prism::Button> m_fullButton;
    std::shared_ptr<prism::Button> m_viewportButton;
    bool m_dockFull = false;
    bool m_graphDetailsShown = false;
    std::shared_ptr<prism::TableView> m_problems;
    std::shared_ptr<prism::TableView> m_findTable;
    std::shared_ptr<prism::TableView> m_diffTable;
    std::shared_ptr<prism::TextInput> m_findInput;
    std::vector<Editor::EntityFindRow> m_findRows;
    std::vector<EntityDiffRow> m_diffRows;
    std::shared_ptr<prism::Widget> m_componentsPanel;
    std::shared_ptr<prism::Widget> m_detailsPanel;

    /// Widget mode: the open file is a Widget Blueprint (.uvwbp). The centre is the Designer (its
    /// screen drawn as the game draws it) or the widget's Graph; the sub row switches them; "+"
    /// is the widget Palette.
    bool m_widgetMode = false;
    std::shared_ptr<WidgetDesignerView> m_designer;
    std::uint64_t m_designerSignature = 0;
    /// Where the widget being dragged started (its undo step is from there to where it is let go).
    Scene::Entity m_dragWidget = Scene::kInvalidEntity;
    /// Each widget moving with it (it and the widgets inside it), and where each started.
    std::vector<std::pair<Scene::Entity, std::array<float, 2>>> m_dragStarts;
    void RefreshDesigner();
    void MoveWidget(Scene::Entity entity, float dx, float dy, bool finished);
    /// A corner-handle resize: live while held, one undo step when let go.
    void ResizeWidget(Scene::Entity entity, prism::Rect rect, bool finished);
    Scene::Entity m_resizeWidget = Scene::kInvalidEntity;
    prism::Rect m_resizeStartRect{};
    /// The Designer's screen size, picked from the sub row (Desktop, Phone ...).
    std::shared_ptr<prism::Button> m_resolutionButton;
    void ShowResolutionMenu();
    void ShowWidgetPalette();
    /// The Stage tab (2): the widget's 3D world. 0 Designer, 1 Graph.
    bool m_stageMode = false;
    void SetWidgetPage(int page);
    [[nodiscard]] Scene::Entity FindWidgetStage() const;
    void EnsureWidgetStage();
    void ShowStageAddMenu();
    /// Screens: a widget's Designer tabs (Main Menu, Settings ...), each a Canvas right under its
    /// root, one graph for all of them. None: the root itself is the one screen.
    Scene::Entity m_activeScreen = Scene::kInvalidEntity;
    [[nodiscard]] std::vector<Scene::Entity> WidgetScreens() const;
    /// Where the Designer shows and places widgets: the active screen, or the root.
    [[nodiscard]] Scene::Entity DesignerParent();
    void AddWidgetScreen();
    void ShowScreenSettings(Scene::Entity screen);
    /// The screen size the Designer shows: the widget's reference resolution, else this device's.
    [[nodiscard]] Math::Vector2 DesignResolution() const;
    void SetDesignResolution(Math::Vector2 size);
    /// The Stage as the Designer's live 3D View last saw it.
    std::vector<std::byte> m_stageSnapshot;
    std::chrono::steady_clock::time_point m_stageSnapshotTime{};
    void OpenWidgetGraph();

    std::vector<Scene::Entity> m_treeEntities;
    std::uint64_t m_componentsSignature = 0;
    std::size_t m_problemsShown = ~std::size_t{0};
    bool m_problemsCompiled = false;
    bool m_syncingSelection = false;
};

} // namespace UVE::Editor
