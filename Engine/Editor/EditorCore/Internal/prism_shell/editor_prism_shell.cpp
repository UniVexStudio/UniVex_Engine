// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_shell.h"

#include "editor.h"
#include "scene_object_registry.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Menu.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"
#include "prism/WindowChrome.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using SceneObjectKindUVE = Scene::Objects::SceneObjectKindUVE;

struct AddEntryUVE final {
    SceneObjectKindUVE kind;
    const char* label;
    prism::Icon icon;
};

/// What the Outliner '+' (#7) can create. The first three are the default offer (folder,
/// environment, sun); the filter (#8) adds any of the rest, or all of them.
constexpr std::array<AddEntryUVE, 14> kAddEntriesUVE{{
    {SceneObjectKindUVE::Folder, "Folder", prism::Icon::Folder},
    {SceneObjectKindUVE::WorldEnvironment3D, "Environment", prism::Icon::Globe},
    {SceneObjectKindUVE::DirectionalLight3D, "Directional Light", prism::Icon::Light},
    {SceneObjectKindUVE::Object3D, "Object3D", prism::Icon::Dot},
    {SceneObjectKindUVE::MeshInstance3D, "MeshInstance3D", prism::Icon::Cube},
    {SceneObjectKindUVE::BoxMesh3D, "Box", prism::Icon::Cube},
    {SceneObjectKindUVE::SphereMesh3D, "Sphere", prism::Icon::Sphere},
    {SceneObjectKindUVE::PlaneMesh3D, "Plane", prism::Icon::Plane},
    {SceneObjectKindUVE::Camera3D, "Camera3D", prism::Icon::Camera},
    {SceneObjectKindUVE::Light3D, "Light3D (Omni / Spot)", prism::Icon::Light},
    {SceneObjectKindUVE::Marker3D, "Marker3D", prism::Icon::Frame},
    {SceneObjectKindUVE::Rigid3D, "Rigid3D", prism::Icon::Cube},
    {SceneObjectKindUVE::Static3D, "Static3D", prism::Icon::Cube},
    {SceneObjectKindUVE::Collider3D, "Collider3D", prism::Icon::Wireframe},
}};
constexpr std::size_t kDefaultAddCountUVE = 3;

[[nodiscard]] prism::Ref<prism::Widget> PanelUVE(prism::Ref<prism::Widget> child) {
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::panel));
    border->setChild(std::move(child));
    return border;
}

[[nodiscard]] prism::Ref<prism::Button> ToolButtonUVE(const prism::Icon icon, std::string tooltip,
                                                      std::string text = {}) {
    auto button = prism::make<prism::Button>(std::move(text), icon, prism::ButtonStyle::Toolbar);
    button->setTooltip(std::move(tooltip));
    return button;
}

[[nodiscard]] prism::Ref<prism::Widget> HSeparatorUVE() {
    return prism::make<prism::Separator>(prism::Orientation::Vertical);
}

void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] std::string FormatFloatUVE(const float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3f", static_cast<double>(v));
    return buffer;
}

} // namespace

EditorPrismShellUVE::EditorPrismShellUVE(EditorUVE& editor) : m_editor(editor) {}

EditorPrismShellUVE::~EditorPrismShellUVE() {
    ShutdownUVE();
}

bool EditorPrismShellUVE::InitializeUVE(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (!m_host.InitializeUVE(window, assetDir)) {
        return false;
    }
    m_host.SetRootUVE(BuildLayoutUVE());
    return true;
}

void EditorPrismShellUVE::ShutdownUVE() {
    m_host.ShutdownUVE();
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildLayoutUVE() {
    auto content = prism::vbox();
    content->add(BuildToolbarRowUVE());

    // Outliner | centre (viewport over the dock area) | Details. The splitter gaps are the panel
    // resize handles (#13, #22).
    auto centre = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centre->add(BuildViewportUVE(), prism::Splitter::Pane{400.0F, 200.0F, 1e9F, true});
    m_dockArea = BuildDockUVE();
    centre->add(m_dockArea, prism::Splitter::Pane{260.0F, 120.0F, 900.0F, false});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(BuildOutlinerUVE(), prism::Splitter::Pane{280.0F, 180.0F, 700.0F, false});
    columns->add(centre, prism::Splitter::Pane{800.0F, 320.0F, 1e9F, true});
    columns->add(BuildDetailsUVE(), prism::Splitter::Pane{340.0F, 220.0F, 800.0F, false});
    content->add(columns, prism::Slot{1.0F});

    content->add(BuildDockBarUVE());

    // #1 logo + #15 title bar (drag strip, minimize / maximize-restore / close).
    prism::WindowChromeOptions chrome{};
    chrome.title = "UniVex Engine";
    chrome.icon = prism::Icon::Layers;
    return prism::wrapWithWindowChrome(content, 0, chrome);
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildToolbarRowUVE() {
    // Sub row 1: #4 open folder, #5 save, #3 selection mode, #14 blueprint (beta) | transport.
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));

    auto openFolder = ToolButtonUVE(prism::Icon::FolderOpen, "Open folder (Content)");
    openFolder->onClick = [this] {
        m_dockVisible = true;
        if (m_dockArea) {
            m_dockArea->setVisible(true);
        }
    };
    row->add(openFolder, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    auto save = ToolButtonUVE(prism::Icon::Save, "Save all (Ctrl+S)");
    save->onClick = [this] {
        const std::size_t saved = m_editor.SaveAllUVE();
        if (prism::Application::exists()) {
            prism::Application::get().notify(saved > 0 ? "Saved" : "Nothing to save", prism::ToastLevel::Info);
        }
    };
    row->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparatorUVE());

    m_selectionMode = prism::make<prism::ComboBox>(
        std::vector<std::string>{"Universal", "Move", "Rotate", "Scale"}, 0);
    m_selectionMode->setTooltip("Selection mode");
    m_selectionMode->onChange = [this](const int index) {
        using Mode = EditorUVE::ViewportGizmoModeUVE;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    row->add(m_selectionMode, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparatorUVE());

    auto blueprint = ToolButtonUVE(prism::Icon::Blueprint, "Blueprint editor (beta)", "Blueprint");
    blueprint->onClick = [] {
        if (prism::Application::exists()) {
            prism::Application::get().notify("Blueprint editor (beta) - coming in the blueprint milestone",
                                             prism::ToastLevel::Info);
        }
    };
    row->add(blueprint, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    // Transport, centred.
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_playButton = ToolButtonUVE(prism::Icon::Play, "Play");
    m_playButton->onClick = [this] {
        if (m_editor.GetPlayModeStateUVE() == EditorPlayModeStateUVE::Paused) {
            static_cast<void>(m_editor.ResumePlayModeUVE());
        } else {
            static_cast<void>(m_editor.EnterPlayModeUVE());
        }
    };
    m_pauseButton = ToolButtonUVE(prism::Icon::Pause, "Pause");
    m_pauseButton->onClick = [this] { static_cast<void>(m_editor.PausePlayModeUVE()); };
    m_stopButton = ToolButtonUVE(prism::Icon::Stop, "Stop");
    m_stopButton->onClick = [this] { static_cast<void>(m_editor.StopPlayModeUVE()); };
    row->add(m_playButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(m_pauseButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(m_stopButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::header));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    border->setChild(row);
    return border;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildOutlinerUVE() {
    // #6 tab, #7 '+', #8 filter, #10 search, #11 tree (#12 its scrollbar).
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));

    auto bar = prism::hbox(4.0F);
    m_addButton = ToolButtonUVE(prism::Icon::Plus, "Add to the level");
    m_addButton->onClick = [this] { ShowAddMenuUVE(); };
    m_filterButton = ToolButtonUVE(prism::Icon::Filter, "Choose what '+' offers");
    m_filterButton->onClick = [this] { ShowAddFilterMenuUVE(); };
    m_outlinerSearch = prism::make<prism::TextInput>("", "Search");
    m_outlinerSearch->onChanged = [this](const std::string& text) {
        if (m_outliner) {
            m_outliner->setFilter(text);
        }
    };
    bar->add(m_addButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_filterButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_outlinerSearch, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    page->add(bar);

    m_outliner = prism::make<prism::TreeView>();
    m_outliner->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingSelection) {
            return;
        }
        if (nodes.empty()) {
            return;
        }
        bool first = true;
        for (const prism::TreeNode* node : nodes) {
            const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
            if (entity == Scene::kInvalidEntityUVE) {
                continue;
            }
            if (first) {
                m_editor.SelectEntityUVE(entity);
                first = false;
            } else if (!m_editor.IsEntitySelectedUVE(entity)) {
                m_editor.ToggleEntitySelectionUVE(entity);
            }
        }
    };
    m_outliner->onVisibilityToggle = [this](prism::TreeNode* node) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE) {
            static_cast<void>(m_editor.SetEntityVisibleUVE(entity, node->visible));
        }
    };
    m_outliner->onRename = [this](prism::TreeNode* node, const std::string& name) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(entity);
            static_cast<void>(m_editor.SetSelectedEntityNameUVE(name));
        }
    };
    m_outliner->onDelete = [this](prism::TreeNode* node) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(entity);
            static_cast<void>(m_editor.DeleteSelectedEntityUVE());
        }
    };
    page->add(m_outliner, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Outliner", prism::Icon::Layers, PanelUVE(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildViewportUVE() {
    // #2 viewport tab over sub row 2 (gizmo / view settings) and #25 the viewport itself.
    auto page = prism::vbox();

    auto settings = prism::hbox(4.0F);
    settings->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    m_gizmoModes = prism::make<prism::Segmented>();
    m_gizmoModes->add("", prism::Icon::Select, "Universal")
        .add("", prism::Icon::Move, "Move")
        .add("", prism::Icon::Rotate, "Rotate")
        .add("", prism::Icon::Scale, "Scale");
    m_gizmoModes->onSelect = [this](const int index) {
        using Mode = EditorUVE::ViewportGizmoModeUVE;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    settings->add(m_gizmoModes, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparatorUVE());
    m_snapButton = ToolButtonUVE(prism::Icon::Magnet, "Snap to grid");
    m_snapButton->setCheckable(true);
    m_snapButton->onToggle = [this](const bool on) {
        EditorTransformSnappingSettingsUVE snap = m_editor.m_transformSnappingSettings;
        snap.enabled = on;
        static_cast<void>(m_editor.SetTransformSnappingSettingsUVE(snap));
    };
    m_gridButton = ToolButtonUVE(prism::Icon::Grid, "Show grid");
    m_gridButton->setCheckable(true);
    m_gridButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.gridVisible = on; };
    m_orthoButton = ToolButtonUVE(prism::Icon::Frame, "Orthographic");
    m_orthoButton->setCheckable(true);
    m_orthoButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.orthographic = on; };
    settings->add(m_snapButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_gridButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_orthoButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto settingsBar = prism::make<prism::Border>();
    settingsBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    settingsBar->setChild(settings);
    page->add(settingsBar);

    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    // Input reaches the viewport through the host's raw state (CaptureViewInputUVE); onInput only
    // makes the view focusable so clicking it takes keyboard focus away from text fields.
    m_viewport->onInput = [](const prism::EngineInput&) {};
    page->add(m_viewport, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Viewport", prism::Icon::Camera, page);
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDetailsUVE() {
    // #17 tab, #18 search, #20 details (#21 scroll).
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    m_detailsSearch = prism::make<prism::TextInput>("", "Search details");
    m_detailsSearch->onChanged = [this](const std::string& text) {
        m_detailsFilter = text;
        m_detailsSignature = 0; // rebuild
    };
    page->add(m_detailsSearch);
    m_detailsTitle = prism::make<prism::Label>("Nothing selected", prism::FontRole::Semibold);
    page->add(m_detailsTitle);
    m_details = prism::make<prism::PropertyGrid>();
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_details);
    page->add(scroll, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Details", prism::Icon::Properties, PanelUVE(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDockUVE() {
    // #16 dock area: the panels docked under the viewport.
    auto tabs = prism::make<prism::TabView>();
    tabs->dockGroup = "level-editor";
    auto content = prism::make<prism::Label>("Content Browser", prism::FontRole::Regular);
    content->setColor(prism::ThemeColor(&prism::Theme::textDim));
    content->setPadding(prism::Margin(12.0F));
    tabs->addTab("Content Browser", prism::Icon::Folder, PanelUVE(content));
    auto log = prism::make<prism::LogView>();
    log->add(prism::LogLevel::Info, "PRISM editor shell ready", "Editor");
    tabs->addTab("Output Log", prism::Icon::Terminal, PanelUVE(log));
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDockBarUVE() {
    auto bar = prism::hbox(6.0F);
    bar->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 2.0F));
    auto toggle = ToolButtonUVE(prism::Icon::Layers, "Show / hide the dock", "Dock");
    toggle->onClick = [this] {
        m_dockVisible = !m_dockVisible;
        if (m_dockArea) {
            m_dockArea->setVisible(m_dockVisible);
        }
    };
    bar->add(toggle, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_statusLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    m_statusLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    bar->add(m_statusLabel, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    border->setChild(bar);
    return border;
}

// ---------------------------------------------------------------------------------- add / filter

void EditorPrismShellUVE::ShowAddMenuUVE() {
    std::vector<prism::MenuItem> items;
    for (std::size_t i = 0; i < kAddEntriesUVE.size(); ++i) {
        const bool offered = i < kDefaultAddCountUVE || m_addAllKinds || m_addKinds.count(static_cast<int>(i)) > 0;
        if (!offered) {
            continue;
        }
        if (i == kDefaultAddCountUVE) {
            items.push_back(prism::MenuItem::Sep());
        }
        const AddEntryUVE entry = kAddEntriesUVE[i];
        items.push_back(prism::MenuItem::Item(
            entry.label,
            [this, kind = entry.kind] { static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind)); }, "",
            entry.icon));
    }
    prism::showMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorPrismShellUVE::ShowAddFilterMenuUVE() {
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Check("All", m_addAllKinds, [this] { m_addAllKinds = !m_addAllKinds; }));
    items.push_back(prism::MenuItem::Sep());
    for (std::size_t i = kDefaultAddCountUVE; i < kAddEntriesUVE.size(); ++i) {
        const int key = static_cast<int>(i);
        const bool on = m_addAllKinds || m_addKinds.count(key) > 0;
        items.push_back(prism::MenuItem::Check(kAddEntriesUVE[i].label, on, [this, key] {
            if (m_addKinds.count(key) > 0) {
                m_addKinds.erase(key);
            } else {
                m_addKinds.insert(key);
            }
        }));
    }
    prism::showMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{},
                    prism::PopupPlacement::Below);
}

// ---------------------------------------------------------------------------------------- refresh

int EditorPrismShellUVE::TreeIdForUVE(const Scene::EntityUVE entity) {
    m_treeEntities.push_back(entity);
    return static_cast<int>(m_treeEntities.size());
}

Scene::EntityUVE EditorPrismShellUVE::EntityForTreeIdUVE(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntityUVE;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

std::uint64_t EditorPrismShellUVE::OutlinerSignatureUVE() {
    std::uint64_t h = 1469598103934665603ULL;
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::function<void(Scene::EntityUVE, int)> visit = [&](const Scene::EntityUVE e, const int depth) {
        HashUVE(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
        HashUVE(h, static_cast<std::uint64_t>(depth));
        HashUVE(h, m_editor.GetEntityDisplayLabelUVE(e));
        HashUVE(h, m_editor.IsEntitySelectedUVE(e) ? 1U : 0U);
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            visit(child, depth + 1);
        }
    };
    for (const Scene::EntityUVE root : m_editor.GetDocumentRootsUVE()) {
        visit(root, 0);
    }
    return h;
}

void EditorPrismShellUVE::RefreshOutlinerUVE() {
    if (!m_outliner) {
        return;
    }
    const std::uint64_t signature = OutlinerSignatureUVE();
    if (signature == m_outlinerSignature) {
        return;
    }
    m_outlinerSignature = signature;
    m_syncingSelection = true;
    m_outliner->clear();
    m_treeEntities.clear();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    std::vector<int> selected;
    const std::function<void(prism::TreeNode*, Scene::EntityUVE)> add = [&](prism::TreeNode* parent,
                                                                           const Scene::EntityUVE e) {
        const int id = TreeIdForUVE(e);
        const std::string_view typeName = m_editor.GetObjectTypeNameUVE(e);
        prism::Icon icon = prism::Icon::Dot;
        if (typeName.find("Light") != std::string_view::npos) { icon = prism::Icon::Light; }
        else if (typeName.find("Camera") != std::string_view::npos) { icon = prism::Icon::Camera; }
        else if (typeName.find("Mesh") != std::string_view::npos) { icon = prism::Icon::Cube; }
        else if (typeName.find("Folder") != std::string_view::npos) { icon = prism::Icon::Folder; }
        else if (typeName.find("Environment") != std::string_view::npos) { icon = prism::Icon::Globe; }
        prism::TreeNode* node = m_outliner->addNode(parent, m_editor.GetEntityDisplayLabelUVE(e), icon, id);
        node->badge = std::string(typeName);
        if (m_editor.IsEntitySelectedUVE(e)) {
            selected.push_back(id);
        }
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::EntityUVE root : m_editor.GetDocumentRootsUVE()) {
        add(nullptr, root);
    }
    m_outliner->setSelection(selected);
    if (m_outlinerSearch) {
        m_outliner->setFilter(m_outlinerSearch->text());
    }
    m_outliner->refresh();
    m_syncingSelection = false;
}

void EditorPrismShellUVE::RefreshDetailsUVE() {
    if (!m_details) {
        return;
    }
    const Scene::EntityUVE entity = m_editor.GetSelectedEntityUVE();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const bool alive = entity != Scene::kInvalidEntityUVE && em.IsAliveUVE(entity);
    std::uint64_t signature = 7U;
    if (alive) {
        HashUVE(signature, (static_cast<std::uint64_t>(entity.index) << 32U) | entity.generation);
        HashUVE(signature, m_editor.GetEntityDisplayLabelUVE(entity));
        HashUVE(signature, m_detailsFilter);
        if (em.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
            const auto& t = em.GetComponentUVE<Scene::TransformComponentUVE>(entity);
            HashUVE(signature, FormatFloatUVE(t.localPosition.x) + FormatFloatUVE(t.localPosition.y) +
                                   FormatFloatUVE(t.localPosition.z) + FormatFloatUVE(t.localScale.x) +
                                   FormatFloatUVE(t.localScale.y) + FormatFloatUVE(t.localScale.z));
        }
    }
    if (signature == m_detailsSignature) {
        return;
    }
    m_detailsSignature = signature;
    m_detailsEntity = entity;
    m_details->clear();
    if (!alive) {
        m_detailsTitle->setText("Nothing selected");
        return;
    }
    m_detailsTitle->setText(m_editor.GetEntityDisplayLabelUVE(entity));
    const auto matches = [this](const std::string& label) {
        if (m_detailsFilter.empty()) {
            return true;
        }
        std::string a = label;
        std::string b = m_detailsFilter;
        std::transform(a.begin(), a.end(), a.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a.find(b) != std::string::npos;
    };

    const int object = m_details->addCategory("Object");
    if (matches("Name")) {
        auto name = prism::make<prism::TextInput>(m_editor.GetEntityDisplayLabelUVE(entity));
        name->onCommit = [this](const std::string& text) { static_cast<void>(m_editor.SetSelectedEntityNameUVE(text)); };
        m_details->addRow(object, "Name", name);
    }
    if (matches("Type")) {
        m_details->addRow(object, "Type", prism::make<prism::Label>(std::string(m_editor.GetObjectTypeNameUVE(entity))));
    }

    if (em.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        const Scene::TransformComponentUVE transform = em.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        const int section = m_details->addCategory("Transform");
        const auto applyAxis = [this](const int which, const int axis, const float value) {
            Scene::IEntityManagerUVE& entityManager = m_editor.m_services->GetEntityManagerUVE();
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            if (selected == Scene::kInvalidEntityUVE ||
                !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
                return;
            }
            Scene::TransformComponentUVE t = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(selected);
            Math::Vector3UVE& v = which == 0 ? t.localPosition : t.localScale;
            (axis == 0 ? v.x : axis == 1 ? v.y : v.z) = value;
            static_cast<void>(m_editor.SetSelectedLocalTransformUVE(t));
        };
        if (matches("Location")) {
            auto location = prism::make<prism::Vec3Field>(0.1F, 3);
            location->setValue(transform.localPosition.x, transform.localPosition.y, transform.localPosition.z);
            location->onChange = [applyAxis](const int axis, const float v) { applyAxis(0, axis, v); };
            m_details->addRow(section, "Location", location);
        }
        if (matches("Rotation")) {
            Math::Vector3UVE euler{};
            static_cast<void>(Math::TryToEulerUVE(transform.localRotation, euler));
            constexpr float kRadToDeg = 57.2957795F;
            auto rotation = prism::make<prism::Vec3Field>(1.0F, 2);
            rotation->setValue(euler.x * kRadToDeg, euler.y * kRadToDeg, euler.z * kRadToDeg);
            rotation->setSuffix("\xC2\xB0");
            rotation->onChange = [this](const int axis, const float degrees) {
                Scene::IEntityManagerUVE& entityManager = m_editor.m_services->GetEntityManagerUVE();
                const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
                if (selected == Scene::kInvalidEntityUVE ||
                    !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
                    return;
                }
                Scene::TransformComponentUVE t = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(selected);
                Math::Vector3UVE e{};
                static_cast<void>(Math::TryToEulerUVE(t.localRotation, e));
                constexpr float kDegToRad = 0.0174532925F;
                (axis == 0 ? e.x : axis == 1 ? e.y : e.z) = degrees * kDegToRad;
                Math::QuaternionUVE q{};
                if (Math::TryMakeEulerUVE(e, q)) {
                    t.localRotation = q;
                    static_cast<void>(m_editor.SetSelectedLocalTransformUVE(t));
                }
            };
            m_details->addRow(section, "Rotation", rotation);
        }
        if (matches("Scale")) {
            auto scale = prism::make<prism::Vec3Field>(0.05F, 3);
            scale->setValue(transform.localScale.x, transform.localScale.y, transform.localScale.z);
            scale->onChange = [applyAxis](const int axis, const float v) { applyAxis(1, axis, v); };
            m_details->addRow(section, "Scale", scale);
        }
    }
}

void EditorPrismShellUVE::RefreshToolbarUVE() {
    const EditorPlayModeStateUVE play = m_editor.GetPlayModeStateUVE();
    if (m_playButton) {
        m_playButton->setChecked(play == EditorPlayModeStateUVE::Playing);
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(play == EditorPlayModeStateUVE::Paused);
    }
    if (m_snapButton) {
        m_snapButton->setChecked(m_editor.m_transformSnappingSettings.enabled);
    }
    if (m_gridButton) {
        m_gridButton->setChecked(m_editor.m_viewportOverlayState.gridVisible);
    }
    if (m_orthoButton) {
        m_orthoButton->setChecked(m_editor.m_viewportOverlayState.orthographic);
    }
    using Mode = EditorUVE::ViewportGizmoModeUVE;
    int mode = 0;
    switch (m_editor.m_viewportOverlayState.gizmoMode) {
        case Mode::Universal: mode = 0; break;
        case Mode::Move: mode = 1; break;
        case Mode::Rotate: mode = 2; break;
        case Mode::Scale: mode = 3; break;
    }
    if (m_gizmoModes && m_gizmoModes->selected() != mode) {
        m_gizmoModes->setSelected(mode);
    }
    if (m_selectionMode && m_selectionMode->selected() != mode) {
        m_selectionMode->setSelected(mode);
    }
    if (m_statusLabel) {
        const char* const state = play == EditorPlayModeStateUVE::Edit      ? "Editing"
                                  : play == EditorPlayModeStateUVE::Playing ? "Playing"
                                                                            : "Paused";
        std::string text = state;
        text += m_editor.m_sceneDirty ? "  \xE2\x80\xA2  unsaved changes" : "";
        if (m_statusLabel->text() != text) {
            m_statusLabel->setText(text);
        }
    }
}

// ------------------------------------------------------------------------------------------ frame

void EditorPrismShellUVE::FrameUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    RefreshOutlinerUVE();
    RefreshDetailsUVE();
    RefreshToolbarUVE();
    m_host.BeginFrameUVE();

    std::vector<EditorPrismHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible() && m_editor.m_viewportPanelRenderer) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
            state.gameWorkspaceActive = m_editor.m_activeWorkspace == EditorUVE::EditorWorkspaceUVE::Game;
            state.studioView = false;
            state.entityAuthoringView = false;
            state.bones.clear();
            state.colliderLines.clear();
            if (!state.gameWorkspaceActive) {
                m_editor.BuildSkeletonOverlayUVE(state.bones);
                m_editor.BuildColliderOverlayUVE(state.colliderLines);
            }
            state.pointerOverOverlay = false;
            state.input = m_host.CaptureViewInputUVE(rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.m_viewportPanelRenderer(
                EditorUVE::ViewportContextUVE::Main, Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used,
                state);
            m_host.ApplyViewCursorUVE(state.input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorPrismHostUVE::ViewImageUVE view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x, used.y};
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
            }
        }
    }
    m_host.RenderUVE(views);
}

} // namespace UVE::Editor
