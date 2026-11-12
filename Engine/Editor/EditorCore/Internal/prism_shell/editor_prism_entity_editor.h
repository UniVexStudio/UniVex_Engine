// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_prism_details.h"
#include "editor_prism_dock_panels.h"
#include "editor_prism_sequencer_panel.h"
#include "editor_prism_anim_graph_panel.h"
#include "editor_prism_blueprint_panel.h"
#include "editor_prism_icons.h"
#include "editor_prism_view_toolbar.h"
#include "editor_prism_viewport_gizmos.h"

#include "entity.h"

#include "prism/Widget.h"

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
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorPrismHostUVE;
class DocumentTabUVE;

/// The Entity Editor built with PRISM, in a window of its own beside the level editor, over the
/// open entity's own scene (EditorUVE's entity document; the level stays open and editable):
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
/// compile / close), the document's objects, selection and undo, the Details rows, the viewport
/// backend of ViewportContextUVE::EntityEditor. It reads and acts inside the entity's scope.
class EditorPrismEntityEditorUVE final {
public:
    EditorPrismEntityEditorUVE(EditorUVE& editor, EditorPrismHostUVE& host);
    ~EditorPrismEntityEditorUVE();
    EditorPrismEntityEditorUVE(const EditorPrismEntityEditorUVE&) = delete;
    EditorPrismEntityEditorUVE& operator=(const EditorPrismEntityEditorUVE&) = delete;

    /// "Browse" shows the open file in the level editor's Content Browser through this.
    std::function<void(const std::filesystem::path&)> onBrowse;

    /// Opens the window when an entity is open and closes it when the session ends; turns the
    /// window's close button into a close request. Call with the level active, before BeginFrame.
    void SyncUVE();
    /// Re-reads the entity document into the panels (inside the entity's scope).
    void RefreshUVE();
    /// Renders the entity's view and presents the window (inside the entity's scope, after BeginFrame).
    void RenderUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }
    bool SaveScreenshotUVE(const std::filesystem::path& path);

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarRowUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildComponentsUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildCentreUVE();
    void ShowAddMenuUVE();
    void ShowClassSettingsUVE(prism::Rect anchor);
    void RunFindUVE(const std::string& query);
    void RunDiffUVE();
    /// Shows the results area under the view on `tab` (0 Compile Results, 1 Find Results, 2 Diff).
    void ShowResultsUVE(int tab);
    void RequestCloseUVE();
    void CompileUVE();
    void RefreshComponentsUVE();
    void RefreshToolbarUVE();
    void RefreshProblemsUVE();
    [[nodiscard]] Scene::EntityUVE EntityForTreeIdUVE(int id) const;

    EditorUVE& m_editor;
    EditorPrismHostUVE& m_host;
    EditorPrismDetailsUVE m_details;
    EditorPrismViewToolbarUVE m_viewToolbar;
    EditorPrismIconsUVE m_icons;
    int m_window = 0;
    std::filesystem::path m_openPath;
    bool m_closeAsked = false;

    std::shared_ptr<DocumentTabUVE> m_tab;
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
    std::shared_ptr<EditorPrismViewportGizmosUVE> m_gizmos;
    std::shared_ptr<class CameraScreenUVE> m_cameraScreen;
    std::shared_ptr<prism::Widget> m_problemsPanel;
    /// The dock, as the level's: pages over the view's foot, a bar of tabs along the window's bottom.
    /// Pages: Animation Sequencer and Animation Graph (their tabs show while the entity has one),
    /// Output Log, Console, Compile Results, Find Results, Diff.
    enum DockPageUVE : int { kSequencerPageUVE, kGraphPageUVE, kOutputLogPageUVE, kConsolePageUVE, kCompilePageUVE,
                             kFindPageUVE, kDiffPageUVE, kDockPageCountUVE };
    std::shared_ptr<prism::StackSwitcher> m_dockPages;
    std::shared_ptr<prism::Widget> m_dockArea;
    std::vector<std::shared_ptr<prism::Button>> m_dockTabs;
    bool m_dockVisible = true;
    EditorPrismDockPanelsUVE m_dockPanels;
    EditorPrismSequencerPanelUVE m_sequencer;
    EditorPrismAnimGraphPanelUVE m_animGraphPanel;
    /// The Blueprint page: one object's Blueprint Class, picked from the sub row's Blueprint button.
    EditorPrismBlueprintPanelUVE m_blueprintPanel;
    std::shared_ptr<prism::Button> m_blueprintButton;
    bool m_blueprintDetailsShown = false;
    /// Blueprint mode: the graph takes the whole centre (no viewport, no dock); Components and
    /// Details stay on the left.
    std::shared_ptr<prism::StackSwitcher> m_centreStack;
    std::shared_ptr<prism::Widget> m_dockBar;
    std::shared_ptr<prism::Button> m_blueprintTab;
    bool m_blueprintMode = false;
    void SetBlueprintModeUVE(bool on);
    [[nodiscard]] prism::Ref<prism::Widget> BuildBlueprintViewUVE();
    void SaveAllUVE();
    /// The sub row's Blueprint button: the entity's objects (the root and its children), each to
    /// open its Blueprint or make one.
    void ShowBlueprintMenuUVE();
    /// The create dialog: the Blueprint's name and the folder it goes in.
    void ShowCreateBlueprintUVE(Scene::EntityUVE target);
    void OpenBlueprintUVE(const std::string& path, Scene::EntityUVE target);
    void SyncBlueprintTabUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockBarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSequencerPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildGraphPageUVE();
    /// Shows `index` (the active tab again hides the dock unless `forceVisible`).
    void ShowDockPanelUVE(int index, bool forceVisible);
    void SyncAnimationTabsUVE();
    /// Full: the dock fills the viewport's space (a Viewport button brings the default back).
    void SetDockFullUVE(bool full);
    void SyncGraphDetailsUVE(bool graphShown);
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
    std::vector<EditorUVE::EntityFindRowUVE> m_findRows;
    std::vector<EntityDiffRowUVE> m_diffRows;
    std::shared_ptr<prism::Widget> m_componentsPanel;
    std::shared_ptr<prism::Widget> m_detailsPanel;

    std::vector<Scene::EntityUVE> m_treeEntities;
    std::uint64_t m_componentsSignature = 0;
    std::size_t m_problemsShown = ~std::size_t{0};
    bool m_problemsCompiled = false;
    bool m_syncingSelection = false;
};

} // namespace UVE::Editor
