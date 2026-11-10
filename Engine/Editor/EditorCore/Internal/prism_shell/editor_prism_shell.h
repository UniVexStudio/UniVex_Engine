// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_prism_host.h"

#include "entity.h"

#include "prism/Widget.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct GLFWwindow;

namespace prism {
class Button;
class EngineView;
class Label;
class PropertyGrid;
class Segmented;
class TabView;
class TextInput;
class TreeView;
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

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildLayoutUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarRowUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildOutlinerUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildViewportUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDetailsUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDockBarUVE();

    void ShowAddMenuUVE();
    void ShowAddFilterMenuUVE();
    void RefreshOutlinerUVE();
    void RefreshDetailsUVE();
    void RefreshToolbarUVE();
    [[nodiscard]] std::uint64_t OutlinerSignatureUVE();
    [[nodiscard]] int TreeIdForUVE(Scene::EntityUVE entity);
    [[nodiscard]] Scene::EntityUVE EntityForTreeIdUVE(int id) const;

    EditorUVE& m_editor;
    EditorPrismHostUVE m_host;

    // Widgets the frame updates.
    std::shared_ptr<prism::TreeView> m_outliner;
    std::shared_ptr<prism::TextInput> m_outlinerSearch;
    std::shared_ptr<prism::Button> m_addButton;
    std::shared_ptr<prism::Button> m_filterButton;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<prism::Segmented> m_gizmoModes;
    std::shared_ptr<prism::ComboBox> m_selectionMode;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_pauseButton;
    std::shared_ptr<prism::Button> m_stopButton;
    std::shared_ptr<prism::Button> m_snapButton;
    std::shared_ptr<prism::Button> m_gridButton;
    std::shared_ptr<prism::Button> m_orthoButton;
    std::shared_ptr<prism::PropertyGrid> m_details;
    std::shared_ptr<prism::TextInput> m_detailsSearch;
    std::shared_ptr<prism::Label> m_detailsTitle;
    std::shared_ptr<prism::Label> m_statusLabel;
    std::shared_ptr<prism::Widget> m_dockArea;

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
