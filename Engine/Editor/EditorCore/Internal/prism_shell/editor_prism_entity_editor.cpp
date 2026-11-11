// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_entity_editor.h"

#include "editor.h"
#include "editor_prism_chrome.h"
#include "editor_prism_host.h"
#include "scene_object_type.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TableView.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
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

/// What the Components '+' adds under the selected object (or the entity's root).
constexpr std::array<AddEntryUVE, 11> kAddEntriesUVE{{
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

constexpr int kWindowWidthUVE = 1240;
constexpr int kWindowHeightUVE = 760;

void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] prism::Slot CentreUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

} // namespace

EditorPrismEntityEditorUVE::EditorPrismEntityEditorUVE(EditorUVE& editor, EditorPrismHostUVE& host)
    : m_editor(editor), m_host(host), m_details(editor), m_viewToolbar(editor) {}

EditorPrismEntityEditorUVE::~EditorPrismEntityEditorUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

// ------------------------------------------------------------------------------------- lifetime

void EditorPrismEntityEditorUVE::SyncUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    const bool open = m_editor.IsEntityEditorOpenUVE();
    const std::filesystem::path path = open ? m_editor.GetEntityEditorAssetPathUVE() : std::filesystem::path{};
    // Closed, or another entity opened in its place: this window's tree described the old one.
    if (m_window != 0 && (!open || path != m_openPath)) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_openPath.clear();
    }
    if (open && m_window == 0) {
        m_treeEntities.clear();
        m_componentsSignature = 0;
        m_problemsShown = ~std::size_t{0};
        m_closeAsked = false;
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Entity Editor - " + path.filename().string(), kWindowWidthUVE,
                                        kWindowHeightUVE);
        if (m_window == 0) {
            // No window: the entity cannot be edited here, so it is not left open unseen.
            static_cast<void>(m_editor.CloseEntityEditorUVE(false));
            return;
        }
        m_openPath = path;
        // Its input runs as the entity's: the panels, shortcuts and the view act on its document.
        m_host.SetWindowScopeUVE(m_window, [this](const std::function<void()>& run) {
            const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
            run();
        });
        m_host.FocusWindowUVE(m_window);
    }
    if (m_window == 0) {
        return;
    }
    if (m_editor.m_entityEditSession.has_value() && m_editor.m_entityEditSession->focusPending) {
        m_editor.m_entityEditSession->focusPending = false;
        m_host.FocusWindowUVE(m_window);
    }
    if (m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        RequestCloseUVE();
    }
}

void EditorPrismEntityEditorUVE::RequestCloseUVE() {
    if (m_window == 0 || m_closeAsked) {
        return;
    }
    if (!m_editor.HasEntityEditorUnsavedChangesUVE()) {
        static_cast<void>(m_editor.CloseEntityEditorUVE(false));
        return;
    }
    m_closeAsked = true;
    const std::string name = m_openPath.filename().string();
    m_host.AppUVE().runInWindow(m_window, [this, name] {
        auto text = prism::make<prism::Label>("Save the changes to " + name + " before closing?");
        prism::showDialog(
            "Close Entity", text,
            {prism::DialogButton{"Save", true,
                                 [this] {
                                     m_closeAsked = false;
                                     static_cast<void>(m_editor.CloseEntityEditorUVE(true));
                                 }},
             prism::DialogButton{"Don't Save", false,
                                 [this] {
                                     m_closeAsked = false;
                                     static_cast<void>(m_editor.CloseEntityEditorUVE(false));
                                 }},
             prism::DialogButton{"Cancel", false, [this] { m_closeAsked = false; }}},
            420.0F);
    });
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());
    window->add(BuildToolbarRowUVE());
    window->add(m_viewToolbar.BuildUVE());

    // Left: Components over Details (a split, like the level's Outliner and Details stacked);
    // centre: the view, and Compile's results under it while it has some.
    auto left = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    m_componentsPanel = BuildComponentsUVE();
    auto detailsTabs = prism::make<prism::TabView>();
    detailsTabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details.BuildUVE()));
    m_detailsPanel = detailsTabs;
    left->add(m_componentsPanel, prism::Splitter::Pane{260.0F, 120.0F, 1e9F, false});
    left->add(m_detailsPanel, prism::Splitter::Pane{400.0F, 160.0F, 1e9F, true});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(left, prism::Splitter::Pane{340.0F, 240.0F, 900.0F, false});
    columns->add(BuildCentreUVE(), prism::Splitter::Pane{860.0F, 320.0F, 1e9F, true});
    window->add(columns, prism::Slot{1.0F});

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildTitleBarUVE() {
    const auto command = [this](const char* id) {
        EditorCommandUVE* const found = m_editor.FindEditorCommandUVE(id);
        prism::MenuItem item = prism::MenuItem::Item(
            found != nullptr ? found->label : std::string(id),
            [this, commandId = std::string(id)] { static_cast<void>(m_editor.RunEditorCommandUVE(commandId)); },
            found != nullptr ? DisplayEditorShortcutUVE(found->shortcuts[0]) : std::string());
        item.enabled = found != nullptr && (!found->isEnabled || found->isEnabled());
        return item;
    };

    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(12.0F);
    menus->addMenu("File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Save", [this] { static_cast<void>(m_editor.SaveEntityEditorUVE()); }, "Ctrl+S",
                                              prism::Icon::Save));
        prism::MenuItem revert = prism::MenuItem::Item("Revert", [this] { static_cast<void>(m_editor.RevertEntityEditorUVE()); });
        revert.enabled = m_editor.HasEntityEditorUnsavedChangesUVE();
        items.push_back(std::move(revert));
        items.push_back(prism::MenuItem::Item("Show in Content Browser", [this] {
            if (onBrowse) {
                onBrowse(m_openPath);
            }
        }, "", prism::Icon::FolderOpen));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { RequestCloseUVE(); }, "", prism::Icon::Close));
        return items;
    });
    menus->addMenu("Edit", [command] {
        return std::vector<prism::MenuItem>{command("edit.undo"), command("edit.redo"), prism::MenuItem::Sep(),
                                            command("edit.duplicate"), command("edit.delete")};
    });
    menus->addMenu("Window", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Components", m_componentsPanel && m_componentsPanel->visible(), [this] {
            if (m_componentsPanel) {
                m_componentsPanel->setVisible(!m_componentsPanel->visible());
            }
        }));
        items.push_back(prism::MenuItem::Check("Details", m_detailsPanel && m_detailsPanel->visible(), [this] {
            if (m_detailsPanel) {
                m_detailsPanel->setVisible(!m_detailsPanel->visible());
            }
        }));
        return items;
    });
    menus->addMenu("Tools", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Find in Components", [this] {
            if (m_componentsSearch) {
                prism::Application::get().setFocus(m_componentsSearch.get());
            }
        }, "", prism::Icon::Search));
        return items;
    });
    menus->addMenu("Build", [command] { return std::vector<prism::MenuItem>{command("entity.compile")}; });
    menus->addMenu("Help", [] {
        return std::vector<prism::MenuItem>{prism::MenuItem::Item("UVE Editor Reference", {}).disabled()};
    });
    row->add(menus, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*row, [this] { RequestCloseUVE(); });

    // [#1 logo] [ title row          ]
    //           [ #2 the entity's tab ]
    auto tabRow = prism::hbox();
    m_tab = prism::make<DocumentTabUVE>();
    m_tab->setFallbackIcon(prism::Icon::Cube);
    m_tab->setName(m_editor.GetEntityEditorAssetPathUVE().stem().string());
    m_tab->onClick = [this] {
        const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
        if (root != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(root);
        }
    };
    tabRow->add(m_tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    tabRow->add(prism::make<DragStripUVE>(true), prism::Slot{1.0F});
    auto titleRow = prism::make<prism::SizeBox>();
    titleRow->setHeightOverride(kTitleRowUVE);
    titleRow->setContent(row);
    auto tabRowBox = prism::make<prism::SizeBox>();
    tabRowBox->setHeightOverride(kTabRowUVE);
    tabRowBox->setContent(tabRow);
    auto right = prism::vbox();
    right->add(titleRow);
    right->add(tabRowBox);
    auto top = prism::hbox();
    top->add(prism::make<LogoBoxUVE>());
    top->add(right, prism::Slot{1.0F});

    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(top);
    return bar;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildToolbarRowUVE() {
    // Sub row 1: compile and its result, save, revert | browse | status.
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));

    auto compile = prism::make<prism::Button>("Compile", prism::Icon::Refresh, prism::ButtonStyle::Toolbar);
    compile->setTooltip("Check the whole entity: one root, and every object's script against its object (F7)");
    compile->onClick = [this] { CompileUVE(); };
    row->add(compile, CentreUVE());
    m_compileState = prism::make<prism::Label>("Not compiled", prism::FontRole::Small);
    m_compileState->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_compileState, prism::Slot{0.0F, prism::Margin(2.0F, 0.0F, 6.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparatorUVE());

    auto save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Toolbar);
    save->setTooltip("Save the entity (Ctrl+S)");
    save->onClick = [this] { static_cast<void>(m_editor.SaveEntityEditorUVE()); };
    row->add(save, CentreUVE());
    m_revertButton = prism::make<prism::Button>("Revert", prism::Icon::Undo, prism::ButtonStyle::Toolbar);
    m_revertButton->setTooltip("Throw away the unsaved changes and read the file again");
    m_revertButton->onClick = [this] { static_cast<void>(m_editor.RevertEntityEditorUVE()); };
    row->add(m_revertButton, CentreUVE());
    row->add(HSeparatorUVE());

    auto browse = ToolButtonUVE(prism::Icon::FolderOpen, "Show the entity in the Content Browser", "Browse");
    browse->onClick = [this] {
        if (onBrowse) {
            onBrowse(m_openPath);
        }
    };
    row->add(browse, CentreUVE());

    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_statusLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    m_statusLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_statusLabel, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::header));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    border->setChild(row);
    return border;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildComponentsUVE() {
    // The entity's objects, as the level's Outliner shows the level's: '+', search, the tree.
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    auto bar = prism::hbox(4.0F);
    m_addButton = ToolButtonUVE(prism::Icon::Plus, "Add an object to the entity");
    m_addButton->onClick = [this] { ShowAddMenuUVE(); };
    m_componentsSearch = prism::make<prism::TextInput>("", "Search");
    m_componentsSearch->onChanged = [this](const std::string& text) {
        if (m_components) {
            m_components->setFilter(text);
        }
    };
    bar->add(m_addButton, CentreUVE());
    bar->add(m_componentsSearch, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    page->add(bar);

    m_components = prism::make<prism::TreeView>();
    m_components->setShowGuides(true);
    m_components->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingSelection || nodes.empty()) {
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
    m_components->onVisibilityToggle = [this](prism::TreeNode* node) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE) {
            static_cast<void>(m_editor.SetEntityVisibleUVE(entity, node->visible));
        }
    };
    m_components->onRename = [this](prism::TreeNode* node, const std::string& name) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(entity);
            static_cast<void>(m_editor.SetSelectedEntityNameUVE(name));
        }
    };
    m_components->onDelete = [this](prism::TreeNode* node) {
        const Scene::EntityUVE entity = EntityForTreeIdUVE(node->id);
        if (entity != Scene::kInvalidEntityUVE && entity != m_editor.GetEntityEditorRootUVE()) {
            m_editor.SelectEntityUVE(entity);
            static_cast<void>(m_editor.DeleteSelectedEntityUVE());
        }
    };
    page->add(m_components, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Components", prism::Icon::Layers, PanelUVE(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildCentreUVE() {
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};
    m_gizmos = prism::make<EditorPrismViewportGizmosUVE>(m_editor);
    auto view = prism::make<prism::Overlay>();
    view->add(m_viewport);
    view->add(m_gizmos);

    // Compile's results: object, line, message; picking one selects its object.
    m_problems = prism::make<prism::TableView>();
    m_problems->addColumn(prism::TableColumn{"object", "Object", 160.0F});
    m_problems->addColumn(prism::TableColumn{"line", "Line", 60.0F});
    m_problems->addColumn(prism::TableColumn{"message", "Message", 520.0F});
    m_problems->cellText = [this](const int row, const int col) -> std::string {
        const std::vector<EditorUVE::EntityCompileProblemUVE>& problems = m_editor.GetEntityEditorProblemsUVE();
        if (row < 0 || static_cast<std::size_t>(row) >= problems.size()) {
            return {};
        }
        const EditorUVE::EntityCompileProblemUVE& p = problems[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return p.objectName.empty() ? std::string("Entity") : p.objectName;
            case 1: return p.at.line > 0U ? std::to_string(p.at.line) : std::string();
            default: return p.message;
        }
    };
    m_problems->onSelect = [this](const std::vector<int>& rows) {
        const std::vector<EditorUVE::EntityCompileProblemUVE>& problems = m_editor.GetEntityEditorProblemsUVE();
        if (rows.empty() || rows.front() < 0 || static_cast<std::size_t>(rows.front()) >= problems.size()) {
            return;
        }
        const Scene::EntityUVE entity = problems[static_cast<std::size_t>(rows.front())].entity;
        if (entity != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(entity);
        }
    };
    auto results = prism::make<prism::TabView>();
    results->addTab("Compile Results", prism::Icon::Warning, PanelUVE(m_problems));
    auto resultsBox = prism::make<prism::SizeBox>();
    resultsBox->setHeightOverride(170.0F);
    resultsBox->setContent(results);
    m_problemsPanel = resultsBox;
    m_problemsPanel->setVisible(false);

    auto centre = prism::vbox();
    centre->add(view, prism::Slot{1.0F});
    centre->add(m_problemsPanel);
    return centre;
}

void EditorPrismEntityEditorUVE::ShowAddMenuUVE() {
    std::vector<prism::MenuItem> items;
    for (const AddEntryUVE& entry : kAddEntriesUVE) {
        prism::MenuItem item = prism::MenuItem::Item(
            entry.label, [this, kind = entry.kind] { static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind)); }, "",
            entry.icon);
        if (const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(entry.kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    }
    prism::showMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorPrismEntityEditorUVE::CompileUVE() {
    const std::size_t problems = m_editor.CompileEntityEditorUVE();
    m_problemsShown = ~std::size_t{0};
    if (problems == 0U && m_problemsPanel) {
        m_problemsPanel->setVisible(false);
    }
}

// --------------------------------------------------------------------------------------- refresh

Scene::EntityUVE EditorPrismEntityEditorUVE::EntityForTreeIdUVE(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntityUVE;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

void EditorPrismEntityEditorUVE::RefreshComponentsUVE() {
    if (!m_components) {
        return;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& graph = m_editor.m_services->GetSceneGraphUVE();
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    const std::vector<Scene::EntityUVE> roots =
        root != Scene::kInvalidEntityUVE ? std::vector<Scene::EntityUVE>{root} : m_editor.GetDocumentRootsUVE();

    std::uint64_t h = 1469598103934665603ULL;
    const std::function<void(Scene::EntityUVE, int)> visit = [&](const Scene::EntityUVE e, const int depth) {
        HashUVE(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
        HashUVE(h, static_cast<std::uint64_t>(depth));
        HashUVE(h, m_editor.GetEntityDisplayLabelUVE(e));
        HashUVE(h, m_editor.IsEntitySelectedUVE(e) ? 1U : 0U);
        for (const Scene::EntityUVE child : graph.GetChildrenUVE(em, e)) {
            visit(child, depth + 1);
        }
    };
    for (const Scene::EntityUVE r : roots) {
        visit(r, 0);
    }
    if (h == m_componentsSignature) {
        return;
    }
    m_componentsSignature = h;

    m_syncingSelection = true;
    m_components->clear();
    m_treeEntities.clear();
    std::vector<int> selected;
    const std::function<void(prism::TreeNode*, Scene::EntityUVE)> add = [&](prism::TreeNode* parent,
                                                                           const Scene::EntityUVE e) {
        m_treeEntities.push_back(e);
        const int id = static_cast<int>(m_treeEntities.size());
        prism::TreeNode* node = m_components->addNode(parent, m_editor.GetEntityDisplayLabelUVE(e), prism::Icon::Dot, id);
        if (const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(Scene::ResolveSceneObjectKindUVE(em, e), 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(m_editor.GetObjectTypeNameUVE(e));
        if (m_editor.IsEntitySelectedUVE(e)) {
            selected.push_back(id);
        }
        for (const Scene::EntityUVE child : graph.GetChildrenUVE(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::EntityUVE r : roots) {
        add(nullptr, r);
    }
    m_components->setSelection(selected);
    if (m_componentsSearch) {
        m_components->setFilter(m_componentsSearch->text());
    }
    m_components->refresh();
    m_syncingSelection = false;
}

void EditorPrismEntityEditorUVE::RefreshProblemsUVE() {
    if (!m_problems || !m_problemsPanel) {
        return;
    }
    const std::vector<EditorUVE::EntityCompileProblemUVE>& problems = m_editor.GetEntityEditorProblemsUVE();
    const bool compiled = m_editor.HasEntityEditorCompiledUVE();
    if (problems.size() == m_problemsShown && compiled == m_problemsCompiled) {
        return;
    }
    m_problemsShown = problems.size();
    m_problemsCompiled = compiled;
    m_problems->setRowCount(static_cast<int>(problems.size()));
    m_problemsPanel->setVisible(!problems.empty());
}

void EditorPrismEntityEditorUVE::RefreshToolbarUVE() {
    const bool unsaved = m_editor.HasEntityEditorUnsavedChangesUVE();
    if (m_tab) {
        m_tab->setName(m_openPath.stem().string());
        m_tab->setDirty(unsaved);
        const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
        if (root != Scene::kInvalidEntityUVE) {
            const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(
                Scene::ResolveSceneObjectKindUVE(m_editor.m_services->GetEntityManagerUVE(), root), 16);
            m_tab->setIcon(icon.pixels, icon.size);
        }
    }
    if (m_revertButton) {
        m_revertButton->setEnabled(unsaved);
    }
    if (m_compileState) {
        const std::vector<EditorUVE::EntityCompileProblemUVE>& problems = m_editor.GetEntityEditorProblemsUVE();
        std::string text = "Not compiled";
        prism::Color color = prism::theme().textDim;
        if (m_editor.HasEntityEditorCompiledUVE()) {
            if (problems.empty()) {
                text = "Compiled";
                color = prism::Color(115, 204, 140);
            } else {
                text = std::to_string(problems.size()) + (problems.size() == 1U ? " problem" : " problems");
                color = prism::Color(242, 158, 89);
            }
        }
        if (m_compileState->text() != text) {
            m_compileState->setText(text);
        }
        m_compileState->setColor(color);
    }
    if (m_statusLabel) {
        std::string text = unsaved ? "unsaved" : "saved";
        text += " | " + std::to_string(m_editor.m_selectedEntities.size()) + " selected";
        if (!m_editor.m_contentStatusMessage.empty()) {
            text = m_editor.m_contentStatusMessage + "    " + text;
        }
        if (m_statusLabel->text() != text) {
            m_statusLabel->setText(text);
        }
    }
}

void EditorPrismEntityEditorUVE::RefreshUVE() {
    if (m_window == 0 || !m_editor.IsEntityDocumentActiveUVE()) {
        return;
    }
    m_host.AppUVE().runInWindow(m_window, [this] {
        RefreshComponentsUVE();
        m_details.RefreshUVE();
        m_viewToolbar.RefreshUVE();
        RefreshToolbarUVE();
        RefreshProblemsUVE();
    });
}

// ---------------------------------------------------------------------------------------- render

void EditorPrismEntityEditorUVE::RenderUVE() {
    if (m_window == 0 || !m_editor.IsEntityDocumentActiveUVE()) {
        return;
    }
    std::vector<EditorPrismHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible() && m_editor.m_viewportPanelRenderer) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            // Inside the entity's scope this is the entity view's own state.
            EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
            state.gameWorkspaceActive = false;
            state.studioView = false;
            state.entityAuthoringView = true;
            state.bones.clear();
            state.colliderLines.clear();
            m_editor.BuildSkeletonOverlayUVE(state.bones);
            m_editor.BuildColliderOverlayUVE(state.colliderLines);
            state.pointerOverOverlay = false;
            state.hostDrawsGizmos = true;
            if (m_gizmos) {
                const std::array<float, 2> orbit = m_gizmos->TakeOrbitRequestUVE();
                state.orbitRequestX = orbit[0];
                state.orbitRequestY = orbit[1];
            }
            state.input = m_host.CaptureViewInputUVE(m_window, rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.m_viewportPanelRenderer(
                EditorUVE::ViewportContextUVE::EntityEditor, Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used,
                state);
            m_host.ApplyViewCursorUVE(m_window, state.input);
            state.orbitRequestX = 0.0F;
            state.orbitRequestY = 0.0F;
            if (m_gizmos) {
                m_gizmos->SyncUVE();
            }
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
    m_host.RenderWindowUVE(m_window, views);
}

bool EditorPrismEntityEditorUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    return m_window != 0 && m_host.SaveWindowScreenshotUVE(m_window, path);
}

} // namespace UVE::Editor
