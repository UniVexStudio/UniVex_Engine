// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_entity_editor.h"
#include "editor_prism_object_menu.h"

#include "editor.h"
#include "editor_prism_chrome.h"
#include "editor_prism_host.h"
#include "editor_prism_shell.h"
#include "animation_graph_component.h"
#include "animation_sequencer.h"
#include "scene_object_type.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/SearchMenu.h"
#include "editor_prism_add_objects.h"
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
    : m_editor(editor), m_host(host), m_details(editor), m_viewToolbar(editor), m_dockPanels(editor), m_sequencer(editor), m_animGraphPanel(editor) {}

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
    // centre: the view over the dock pages; the dock bar runs along the bottom, as in the level.
    auto left = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    m_componentsPanel = BuildComponentsUVE();
    auto detailsTabs = prism::make<prism::TabView>();
    detailsTabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details.BuildUVE()));
    m_detailsPanel = detailsTabs;
    m_detailsTabs = detailsTabs;
    m_graphDetailsShown = false;
    left->add(m_componentsPanel, prism::Splitter::Pane{260.0F, 120.0F, 1e9F, false});
    left->add(m_detailsPanel, prism::Splitter::Pane{400.0F, 160.0F, 1e9F, true});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(left, prism::Splitter::Pane{340.0F, 240.0F, 900.0F, false});
    columns->add(BuildCentreUVE(), prism::Splitter::Pane{860.0F, 320.0F, 1e9F, true});
    window->add(columns, prism::Slot{1.0F});
    window->add(BuildDockBarUVE());

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
                                            command("edit.cut"), command("edit.copy"), command("edit.paste"),
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
        items.push_back(prism::MenuItem::Item("Find in Entity", [this] {
            if (m_findInput) {
                prism::Application::get().setFocus(m_findInput.get());
            }
        }, "", prism::Icon::Search));
        items.push_back(prism::MenuItem::Item("Diff Against Saved", [this] { RunDiffUVE(); }, "", prism::Icon::DiffView));
        items.push_back(prism::MenuItem::Item("Class Settings", [this] {
            ShowClassSettingsUVE(prism::Rect{360.0F, 64.0F, 0.0F, 0.0F});
        }, "", prism::Icon::Settings));
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
    m_tab->setFallbackIcon(prism::Icon::EntityAsset);
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

    auto compile = prism::make<prism::Button>("Compile", prism::Icon::Compile, prism::ButtonStyle::Toolbar);
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

    // Find: every place the text occurs in the entity, listed under the view (Enter).
    m_findInput = prism::make<prism::TextInput>("", "Find in entity");
    m_findInput->setTooltip("Object names and types, components, property names and values (Enter lists them)");
    m_findInput->onCommit = [this](const std::string& text) { RunFindUVE(text); };
    auto findBox = prism::make<prism::SizeBox>();
    findBox->setWidthOverride(220.0F);
    findBox->setContent(m_findInput);
    row->add(findBox, CentreUVE());
    auto diff = prism::make<prism::Button>("Diff", prism::Icon::DiffView, prism::ButtonStyle::Toolbar);
    diff->setTooltip("What changed since the entity was last saved");
    diff->onClick = [this] { RunDiffUVE(); };
    row->add(diff, CentreUVE());
    row->add(HSeparatorUVE());

    auto classSettings = prism::make<prism::Button>("Class Settings", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    classSettings->setTooltip("The entity's own settings");
    std::weak_ptr<prism::Button> weakSettings = classSettings;
    classSettings->onClick = [this, weakSettings] {
        if (auto anchor = weakSettings.lock()) {
            ShowClassSettingsUVE(anchor->rect());
        }
    };
    row->add(classSettings, CentreUVE());
    row->add(HSeparatorUVE());

    // Simulate: the entity's scripts run on its own scene; Stop puts it back as it was.
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_playButton = ToolButtonUVE(prism::Icon::Play, "Simulate the entity (its scripts run here, the level is untouched)");
    m_playButton->onClick = [this] {
        m_lastTick = std::chrono::steady_clock::now();
        static_cast<void>(m_editor.PlayEntitySimulationUVE());
    };
    m_pauseButton = ToolButtonUVE(prism::Icon::Pause, "Pause the simulation");
    m_pauseButton->onClick = [this] { m_editor.PauseEntitySimulationUVE(); };
    m_stopButton = ToolButtonUVE(prism::Icon::Stop, "Stop: the entity goes back to how it was before Simulate");
    m_stopButton->onClick = [this] { static_cast<void>(m_editor.StopEntitySimulationUVE()); };
    row->add(m_playButton, CentreUVE());
    row->add(m_pauseButton, CentreUVE());
    row->add(m_stopButton, CentreUVE());
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    // A fixed width, so a long message never moves the Simulate buttons.
    m_statusLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    m_statusLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    m_statusLabel->setAlign(prism::HAlign::Right);
    auto statusBox = prism::make<prism::SizeBox>();
    statusBox->setWidthOverride(380.0F);
    statusBox->setContent(m_statusLabel);
    row->add(statusBox, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

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
    m_components->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        const Scene::EntityUVE entity = node != nullptr ? EntityForTreeIdUVE(node->id) : Scene::kInvalidEntityUVE;
        EditorPrismObjectMenuUVE::HooksUVE hooks;
        hooks.tree = m_components.get();
        hooks.addChild = [this] { ShowAddMenuUVE(); };
        EditorPrismObjectMenuUVE::ShowUVE(m_editor, entity, node, at, hooks);
    };
    page->add(m_components, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Components", prism::Icon::Components, PanelUVE(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildCentreUVE() {
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};
    m_gizmos = prism::make<EditorPrismViewportGizmosUVE>(m_editor);
    m_cameraScreen = prism::make<CameraScreenUVE>();
    m_cameraScreen->setVisible(false);
    auto view = prism::make<prism::Overlay>();
    view->add(m_viewport);
    view->add(m_gizmos);
    view->add(m_cameraScreen);

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
    // Find's results: object, where, what matched; picking one selects its object.
    m_findTable = prism::make<prism::TableView>();
    m_findTable->addColumn(prism::TableColumn{"object", "Object", 160.0F});
    m_findTable->addColumn(prism::TableColumn{"where", "Where", 220.0F});
    m_findTable->addColumn(prism::TableColumn{"text", "Found", 360.0F});
    m_findTable->cellText = [this](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_findRows.size()) {
            return {};
        }
        const EditorUVE::EntityFindRowUVE& r = m_findRows[static_cast<std::size_t>(row)];
        return col == 0 ? r.object : col == 1 ? r.where : r.text;
    };
    m_findTable->onSelect = [this](const std::vector<int>& rows) {
        if (!rows.empty() && rows.front() >= 0 && static_cast<std::size_t>(rows.front()) < m_findRows.size()) {
            const Scene::EntityUVE entity = m_findRows[static_cast<std::size_t>(rows.front())].entity;
            if (m_editor.m_services->GetEntityManagerUVE().IsAliveUVE(entity)) {
                m_editor.SelectEntityUVE(entity);
            }
        }
    };

    // Diff: what changed since the last save, object by object.
    m_diffTable = prism::make<prism::TableView>();
    m_diffTable->addColumn(prism::TableColumn{"change", "Change", 90.0F});
    m_diffTable->addColumn(prism::TableColumn{"object", "Object", 180.0F});
    m_diffTable->addColumn(prism::TableColumn{"what", "Component / Property", 220.0F});
    m_diffTable->addColumn(prism::TableColumn{"before", "Saved", 160.0F});
    m_diffTable->addColumn(prism::TableColumn{"after", "Now", 160.0F});
    m_diffTable->cellText = [this](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_diffRows.size()) {
            return {};
        }
        const EntityDiffRowUVE& r = m_diffRows[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return r.kind == EntityDiffRowUVE::KindUVE::Added   ? "Added"
                           : r.kind == EntityDiffRowUVE::KindUVE::Removed ? "Removed"
                                                                          : "Changed";
            case 1: return r.object;
            case 2: return r.what.empty() ? std::string("(object)") : r.what;
            case 3: return r.before;
            default: return r.after;
        }
    };

    // The dock pages (the level's layout): one page per tab of the bar along the window's bottom.
    m_dockPanels.AttachLogUVE();
    m_dockPages = prism::make<prism::StackSwitcher>();
    m_dockPages->addPage(BuildSequencerPageUVE());
    m_dockPages->addPage(BuildGraphPageUVE());
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildOutputLogUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildConsoleUVE()));
    m_dockPages->addPage(PanelUVE(m_problems));
    m_dockPages->addPage(PanelUVE(m_findTable));
    m_dockPages->addPage(PanelUVE(m_diffTable));
    m_dockPages->setActiveIndex(kConsolePageUVE);
    m_dockArea = m_dockPages;
    m_problemsPanel = m_dockPages;
    m_dockVisible = true;

    auto centre = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centre->add(view, prism::Splitter::Pane{400.0F, 160.0F, 1e9F, true});
    centre->add(m_dockArea, prism::Splitter::Pane{290.0F, 110.0F, 900.0F, false});
    m_viewArea = view;
    m_centreSplit = centre;
    m_dockFull = false;
    return centre;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildDockBarUVE() {
    // The level's dock bar: each tab shows its page, the active one hides it.
    auto bar = prism::hbox(2.0F);
    bar->setPadding(prism::Margin(4.0F, 1.0F, 8.0F, 1.0F));
    struct DockTabUVE { const char* label; prism::Icon icon; };
    static constexpr std::array<DockTabUVE, kDockPageCountUVE> kTabs{{
        {"Animation Sequencer", prism::Icon::AnimSequencer},
        {"Animation Graph", prism::Icon::AnimGraph},
        {"Output Log", prism::Icon::OutputLog},
        {"Console", prism::Icon::Terminal},
        {"Compile Results", prism::Icon::CompileResults},
        {"Find Results", prism::Icon::FindResults},
        {"Diff", prism::Icon::DiffView},
    }};
    m_dockTabs.clear();
    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        auto tab = prism::make<prism::Button>(kTabs[i].label, kTabs[i].icon, prism::ButtonStyle::Toolbar);
        tab->setCheckable(true);
        tab->setChecked(static_cast<int>(i) == kConsolePageUVE);
        tab->onClick = [this, index = static_cast<int>(i)] { ShowDockPanelUVE(index, false); };
        // The animation tabs come with their objects (SyncAnimationTabsUVE).
        tab->setVisible(i != kSequencerPageUVE && i != kGraphPageUVE);
        m_dockTabs.push_back(tab);
        bar->add(tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    bar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    // Full: the dock takes the viewport's place; Viewport brings the default layout back.
    m_fullButton = prism::make<prism::Button>("Full", prism::Icon::LayoutFull, prism::ButtonStyle::Toolbar);
    m_fullButton->setTooltip("Let the dock fill the viewport's space");
    m_fullButton->onClick = [this] { SetDockFullUVE(true); };
    m_viewportButton = prism::make<prism::Button>("Viewport", prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
    m_viewportButton->setTooltip("Back to the default layout: the viewport over the dock");
    m_viewportButton->onClick = [this] { SetDockFullUVE(false); };
    m_viewportButton->setVisible(false);
    bar->add(m_fullButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    bar->add(m_viewportButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    border->setChild(bar);
    return border;
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildSequencerPageUVE() {
    return PanelUVE(m_sequencer.BuildUVE());
}

prism::Ref<prism::Widget> EditorPrismEntityEditorUVE::BuildGraphPageUVE() {
    return PanelUVE(m_animGraphPanel.BuildUVE());
}

void EditorPrismEntityEditorUVE::SetDockFullUVE(const bool full) {
    if (!m_centreSplit || !m_viewArea) {
        return;
    }
    if (full && !m_dockVisible && m_dockPages) {
        ShowDockPanelUVE(m_dockPages->activeIndex(), true);   // nothing to fill with while hidden
    }
    m_dockFull = full;
    m_viewArea->setVisible(!full);
    if (prism::Splitter::Pane* const pane = m_centreSplit->paneAt(1)) {
        pane->flex = full;
        pane->maxSize = full ? 1e9F : 900.0F;
    }
    if (m_fullButton) {
        m_fullButton->setVisible(!full);
    }
    if (m_viewportButton) {
        m_viewportButton->setVisible(full);
    }
    m_centreSplit->relayout();
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorPrismEntityEditorUVE::SyncGraphDetailsUVE(const bool graphShown) {
    // The graph's settings sit in the Details column while its page is up, picked when it opens.
    if (!m_detailsTabs || graphShown == m_graphDetailsShown) {
        return;
    }
    const prism::Ref<prism::Widget> side = m_animGraphPanel.SideUVE();
    if (!side) {
        return;
    }
    if (graphShown) {
        static_cast<void>(m_detailsTabs->insertTab(m_detailsTabs->count(), prism::TabView::Page{"Animation Graph", prism::Icon::AnimGraph, side, false}));
    } else if (const int index = m_detailsTabs->indexOf("Animation Graph"); index >= 0) {
        static_cast<void>(m_detailsTabs->takeTab(index));
        m_detailsTabs->setCurrent(0);
    }
    m_graphDetailsShown = graphShown;
}

void EditorPrismEntityEditorUVE::ShowDockPanelUVE(const int index, const bool forceVisible) {
    if (!m_dockPages || index < 0 || index >= m_dockPages->count()) {
        return;
    }
    const bool sameTab = m_dockPages->activeIndex() == index;
    if (sameTab && m_dockVisible && !forceVisible) {
        m_dockVisible = false;
    } else {
        m_dockVisible = true;
        m_dockPages->setActiveIndex(index);
    }
    if (m_dockArea) {
        m_dockArea->setVisible(m_dockVisible);
    }
    if (!m_dockVisible && m_dockFull) {
        SetDockFullUVE(false);
    }
    for (std::size_t i = 0; i < m_dockTabs.size(); ++i) {
        m_dockTabs[i]->setChecked(m_dockVisible && static_cast<int>(i) == m_dockPages->activeIndex());
    }
}

void EditorPrismEntityEditorUVE::SyncAnimationTabsUVE() {
    if (!m_dockPages || m_dockTabs.size() != kDockPageCountUVE) {
        return;
    }
    // The tabs follow the objects: an AnimationSequencer / AnimationGraph in the entity brings its tab.
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    bool hasSequencer = false;
    bool hasGraph = false;
    for (const Scene::EntityUVE entity : m_editor.CollectEntityEditorObjectsUVE()) {
        hasSequencer = hasSequencer || em.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(entity);
        hasGraph = hasGraph || em.HasComponentUVE<Scene::AnimationGraphComponentUVE>(entity);
    }
    const auto sync = [this](const int index, const bool want) {
        prism::Button& tab = *m_dockTabs[static_cast<std::size_t>(index)];
        if (tab.visible() == want) {
            return;
        }
        tab.setVisible(want);
        if (want) {
            ShowDockPanelUVE(index, true);   // a new object opens its tab
        } else if (m_dockPages->activeIndex() == index) {
            ShowDockPanelUVE(kConsolePageUVE, true);   // its object went: back to the Console
        }
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
    };
    sync(kSequencerPageUVE, hasSequencer);
    sync(kGraphPageUVE, hasGraph);
}

void EditorPrismEntityEditorUVE::ShowAddMenuUVE() {
    // What an entity is built from (no level environment, sky or streaming), grouped and searchable.
    std::vector<prism::MenuItem> items = BuildAddObjectMenuUVE(
        AddObjectContextUVE::Entity, m_icons,
        [this](const SceneObjectKindUVE kind) { static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind)); });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search components");
}

void EditorPrismEntityEditorUVE::CompileUVE() {
    static_cast<void>(m_editor.CompileEntityEditorUVE());
    m_problemsShown = ~std::size_t{0};
}

void EditorPrismEntityEditorUVE::ShowResultsUVE(const int tab) {
    if (tab >= 0 && tab < 3) {
        ShowDockPanelUVE(kCompilePageUVE + tab, true);
    }
}

void EditorPrismEntityEditorUVE::RunFindUVE(const std::string& query) {
    m_findRows = m_editor.FindInEntityEditorUVE(query);
    if (m_findTable) {
        m_findTable->setRowCount(static_cast<int>(m_findRows.size()));
    }
    m_editor.m_contentStatusMessage = query.empty()        ? std::string()
                                      : m_findRows.empty() ? "\"" + query + "\" is not in the entity."
                                                           : std::to_string(m_findRows.size()) + " found for \"" + query + "\".";
    if (!query.empty()) {
        ShowResultsUVE(1);
    }
}

void EditorPrismEntityEditorUVE::RunDiffUVE() {
    m_diffRows = m_editor.DiffEntityEditorUVE();
    if (m_diffTable) {
        m_diffTable->setRowCount(static_cast<int>(m_diffRows.size()));
    }
    m_editor.m_contentStatusMessage = m_diffRows.empty() ? "No changes since " + m_openPath.filename().string() + " was saved."
                                                         : std::to_string(m_diffRows.size()) +
                                                               (m_diffRows.size() == 1U ? " change" : " changes") +
                                                               " since the last save.";
    ShowResultsUVE(2);
}

void EditorPrismEntityEditorUVE::ShowClassSettingsUVE(const prism::Rect anchor) {
    // The entity's own settings: what it is, where it lives, its root, and the project roles it can take.
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    if (root != Scene::kInvalidEntityUVE) {
        m_editor.SelectEntityUVE(root);   // the root's components follow in Details
    }
    auto grid = prism::make<prism::PropertyGrid>();
    const int entity = grid->addCategory("Entity", true);
    const auto text = [](const std::string& value) {
        auto label = prism::make<prism::Label>(value);
        label->setColor(prism::ThemeColor(&prism::Theme::textBright));
        return label;
    };
    grid->addRow(entity, "File", text(m_openPath.filename().string()));
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(m_openPath, snapshot.contentRoot, error);
    grid->addRow(entity, "Folder", text(error ? m_openPath.parent_path().string()
                                       : relative.parent_path().empty() ? std::string("Content")
                                                                        : "Content/" + relative.parent_path().generic_string()));
    grid->addRow(entity, "Root", text(root != Scene::kInvalidEntityUVE ? m_editor.GetEntityDisplayLabelUVE(root) : "(none)"));
    grid->addRow(entity, "Root Type",
                 text(root != Scene::kInvalidEntityUVE ? std::string(m_editor.GetObjectTypeNameUVE(root)) : "-"));
    grid->addRow(entity, "Objects", text(std::to_string(m_editor.CollectEntityEditorObjectsUVE().size())));

    const int project = grid->addCategory("Project", true);
    if (!error && m_openPath.extension() == ".uventity") {
        const bool isDefault = m_editor.GetDefaultPlayerEntityUVE() == relative.generic_string();
        auto player = prism::make<prism::CheckBox>("", isDefault);
        player->setTooltip("Play spawns this entity as the player");
        player->onChange = [this, relative](const bool on) {
            if (!m_editor.SetDefaultPlayerEntityUVE(on ? relative : std::filesystem::path{})) {
                m_editor.m_contentStatusMessage = "Could not change the default player.";
            }
        };
        grid->addRow(project, "Default Player", player);
    }
    grid->addRow(project, "Compiled", text(!m_editor.HasEntityEditorCompiledUVE()             ? "Not yet"
                                           : m_editor.GetEntityEditorProblemsUVE().empty() ? "Yes, no problems"
                                                                                            : std::to_string(m_editor.GetEntityEditorProblemsUVE().size()) + " problems"));

    auto size = prism::make<prism::SizeBox>();
    size->setWidthOverride(360.0F);
    size->setHeightOverride(260.0F);
    size->setContent(grid);
    auto panel = prism::make<prism::Border>();
    panel->setBackground(prism::ThemeColor(&prism::Theme::panel));
    panel->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    panel->setChild(size);
    prism::PopupOptions options{};
    options.placement = prism::PopupPlacement::Below;
    options.anchor = anchor;
    prism::Application::get().showPopup(panel, options);
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
    const bool newlyCompiled = compiled && (problems.size() != m_problemsShown || !m_problemsCompiled);
    m_problemsShown = problems.size();
    m_problemsCompiled = compiled;
    m_problems->setRowCount(static_cast<int>(problems.size()));
    // A Compile with problems opens them; a clean one leaves the area as it was.
    if (newlyCompiled && !problems.empty()) {
        ShowResultsUVE(0);
    }
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
    using SimulationUVE = EditorUVE::EntitySimulationStateUVE;
    const SimulationUVE simulation = m_editor.GetEntitySimulationStateUVE();
    if (m_playButton) {
        m_playButton->setChecked(simulation == SimulationUVE::Running);
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(simulation == SimulationUVE::Paused);
        m_pauseButton->setEnabled(simulation != SimulationUVE::Stopped);
    }
    if (m_stopButton) {
        m_stopButton->setEnabled(simulation != SimulationUVE::Stopped);
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
    // A running simulation advances by the time since the last frame (paused, it keeps the clock).
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTick.time_since_epoch().count() == 0
                          ? 0.0
                          : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    if (m_editor.GetEntitySimulationStateUVE() == EditorUVE::EntitySimulationStateUVE::Running) {
        m_editor.TickEntitySimulationUVE(dt);
    }
    m_host.AppUVE().runInWindow(m_window, [this, dt] {
        RefreshComponentsUVE();
        m_details.RefreshUVE();
        m_viewToolbar.RefreshUVE();
        RefreshToolbarUVE();
        RefreshProblemsUVE();
        SyncAnimationTabsUVE();
        const bool sequencerShown = m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kSequencerPageUVE &&
                                    m_dockTabs.size() == kDockPageCountUVE && m_dockTabs[kSequencerPageUVE]->visible();
        m_sequencer.RefreshUVE(dt, sequencerShown);
        const bool graphShown = m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kGraphPageUVE &&
                                m_dockTabs.size() == kDockPageCountUVE && m_dockTabs[kGraphPageUVE]->visible();
        m_animGraphPanel.RefreshUVE(dt, graphShown);
        SyncGraphDetailsUVE(graphShown);
        m_dockPanels.RefreshUVE(m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kConsolePageUVE, false);
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
    EditorPrismShellUVE::AppendCameraScreenUVE(m_editor, m_details, m_cameraScreen.get(), views);
    m_host.RenderWindowUVE(m_window, views);
}

bool EditorPrismEntityEditorUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    return m_window != 0 && m_host.SaveWindowScreenshotUVE(m_window, path);
}

} // namespace UVE::Editor
