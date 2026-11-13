// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_entity_editor.h"
#include "editor_object_menu.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_shell.h"
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
#include <cctype>
#include "script_component.h"
#include "canvas_component.h"
#include "name_component.h"
#include "hierarchy_component.h"
#include "scene_component_metadata.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_image_component.h"
#include "ui_progress_bar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "visibility_component.h"
#include "widget_blueprint_asset.h"
#include "editor_widget_icons.h"
#include "ui_dropdown_component.h"
#include "ui_layout.h"
#include "ui_font_atlas.h"
#include "ui_layout_container_component.h"
#include "ui_scroll_container_component.h"
#include "ui_view_3d_component.h"
#include "monitor_info.h"
#include "i_window_manager.h"
#include "i_scene_serializer.h"
#include "engine_services.h"
#include "i_scene_graph.h"
#include "scalar.h"
#include "quaternion.h"
#include "transform_component.h"
#include "primitive_mesh_component.h"
#include "light_component.h"
#include "camera_component.h"
#include "ui_scrollbar_component.h"
#include <typeindex>
#include <type_traits>
#include "editor_add_objects.h"
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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

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

EditorEntityEditorUVE::EditorEntityEditorUVE(EditorUVE& editor, EditorHostUVE& host)
    : m_editor(editor), m_host(host), m_details(editor), m_viewToolbar(editor), m_dockPanels(editor), m_sequencer(editor), m_animGraphPanel(editor), m_blueprintPanel(editor) {}

EditorEntityEditorUVE::~EditorEntityEditorUVE() {
    m_editor.m_openBlueprintProviderUVE = nullptr;
    m_editor.m_saveOpenBlueprintUVE = nullptr;
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

// ------------------------------------------------------------------------------------- lifetime

void EditorEntityEditorUVE::SyncUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    const bool open = m_editor.IsEntityEditorOpenUVE();
    const std::filesystem::path path = open ? m_editor.GetEntityEditorAssetPathUVE() : std::filesystem::path{};
    // Closed, or another entity opened in its place: this window's tree described the old one.
    if (m_window != 0 && (!open || path != m_openPath)) {
        m_editor.EndWidgetPreviewUVE();
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_openPath.clear();
    }
    if (open && m_window == 0) {
        m_treeEntities.clear();
        m_componentsSignature = 0;
        m_problemsShown = ~std::size_t{0};
        m_closeAsked = false;
        m_widgetMode = Asset::IsWidgetBlueprintPathUVE(path.generic_string());
        m_designerSignature = 0;
        m_window = m_host.OpenWindowUVE(BuildUVE(), (m_widgetMode ? "Widget Blueprint - " : "Entity Editor - ") + path.filename().string(),
                                        kWindowWidthUVE, kWindowHeightUVE);
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

void EditorEntityEditorUVE::RequestCloseUVE() {
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

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());
    window->add(BuildToolbarRowUVE());
    // Beside the gizmo settings: Blueprint - pick one of the entity's objects, open or make its class.
    m_blueprintButton = prism::make<prism::Button>("Blueprint", prism::Icon::BlueprintClass, prism::ButtonStyle::Toolbar);
    m_blueprintButton->setTooltip("Blueprint Class: open or create the Blueprint of one of this entity's objects");
    m_blueprintButton->onClick = [this] { ShowBlueprintMenuUVE(); };
    // Simulate beside it: one toggle (Simulate / Stop) in place of play, pause and stop.
    m_playButton = prism::make<prism::Button>("Simulate", prism::Icon::Play, prism::ButtonStyle::Toolbar);
    m_playButton->setTooltip("Simulate the entity (its scripts and Blueprints run here, the level is untouched); Stop puts it back");
    m_playButton->onClick = [this] {
        if (m_editor.GetEntitySimulationStateUVE() == EditorUVE::EntitySimulationStateUVE::Stopped) {
            m_lastTick = std::chrono::steady_clock::now();
            static_cast<void>(m_editor.PlayEntitySimulationUVE());
        } else {
            static_cast<void>(m_editor.StopEntitySimulationUVE());
        }
    };
    auto subRowExtra = prism::hbox(4.0F);
    if (!m_widgetMode) {
        subRowExtra->add(m_blueprintButton, CentreUVE());
        subRowExtra->add(HSeparatorUVE());
    }
    subRowExtra->add(m_playButton, CentreUVE());
    subRowExtra->add(HSeparatorUVE());
    // Viewport | <folder> > BP_Player > BP_Camera ...: where the centre is, and the Blueprints opened.
    m_crumbRow = prism::hbox(0.0F);
    m_crumbSignature.clear();
    m_openBlueprints.clear();
    subRowExtra->add(m_crumbRow, CentreUVE());
    if (m_widgetMode) {
        // A widget has no 3D gizmos: the sub row is Simulate and Designer | Graph.
        auto subRow = prism::hbox(4.0F);
        subRow->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
        subRow->add(subRowExtra, CentreUVE());
        auto bar = prism::make<prism::Border>();
        bar->setBackground(prism::ThemeColor(&prism::Theme::header));
        bar->setChild(subRow);
        window->add(bar);
    } else {
        window->add(m_viewToolbar.BuildUVE(true, subRowExtra));
    }
    // Compile reads the Blueprint being edited, saved or not.
    m_editor.m_openBlueprintProviderUVE = [this](const std::string& path) { return m_blueprintPanel.UnsavedForUVE(path); };
    m_editor.m_saveOpenBlueprintUVE = [this] {
        if (m_blueprintPanel.IsOpenUVE() && m_blueprintPanel.DirtyUVE()) {
            static_cast<void>(m_blueprintPanel.SaveUVE());
        }
    };
    if (m_widgetMode) {
        m_blueprintPanel.widgetsProvider = [this] {
            std::vector<std::pair<std::string, WidgetKindUVE>> widgets;
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            for (const Scene::EntityUVE object : m_editor.CollectEntityEditorObjectsUVE()) {
                const std::optional<WidgetKindUVE> kind = ClassifyWidgetUVE(em, object);
                const bool screen = kind == WidgetKindUVE::Canvas && object != m_editor.GetEntityEditorRootUVE();
                if (kind.has_value() && (*kind != WidgetKindUVE::Canvas || screen) && em.HasComponentUVE<Scene::NameComponentUVE>(object)) {
                    widgets.emplace_back(em.GetComponentUVE<Scene::NameComponentUVE>(object).name, *kind);
                }
            }
            return widgets;
        };
    } else {
        m_blueprintPanel.widgetsProvider = nullptr;
    }

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
    m_centreStack = prism::make<prism::StackSwitcher>();
    m_centreStack->addPage(BuildCentreUVE());
    m_centreStack->addPage(BuildBlueprintViewUVE());
    m_centreStack->setActiveIndex(0);
    m_blueprintMode = false;
    columns->add(m_centreStack, prism::Splitter::Pane{860.0F, 320.0F, 1e9F, true});
    window->add(columns, prism::Slot{1.0F});
    m_dockBar = BuildDockBarUVE();
    window->add(m_dockBar);
    if (m_widgetMode && m_dockArea) {
        // The Designer takes the whole centre; the dock opens from its bar.
        m_dockVisible = false;
        m_dockArea->setVisible(false);
        for (const auto& tab : m_dockTabs) {
            tab->setChecked(false);
        }
    }

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildTitleBarUVE() {
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
        items.push_back(prism::MenuItem::Item("Save", [this] { SaveAllUVE(); }, "Ctrl+S",
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

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildToolbarRowUVE() {
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
    save->onClick = [this] { SaveAllUVE(); };
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

    // Simulate is on the sub row, beside the Blueprint button.
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    // A fixed width, so a long message never moves the buttons.
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

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildComponentsUVE() {
    // The entity's objects, as the level's Outliner shows the level's: '+', search, the tree.
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    auto bar = prism::hbox(4.0F);
    m_addButton = ToolButtonUVE(prism::Icon::Plus, m_widgetMode ? "Palette: add a widget" : "Add an object to the entity");
    m_addButton->onClick = [this] {
        if (m_widgetMode && m_stageMode) {
            ShowStageAddMenuUVE();
        } else if (m_widgetMode) {
            ShowWidgetPaletteUVE();
        } else {
            ShowAddMenuUVE();
        }
    };
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
        EditorObjectMenuUVE::HooksUVE hooks;
        hooks.tree = m_components.get();
        hooks.addChild = [this] { ShowAddMenuUVE(); };
        EditorObjectMenuUVE::ShowUVE(m_editor, entity, node, at, hooks);
    };
    page->add(m_components, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab(m_widgetMode ? "Hierarchy" : "Components", prism::Icon::Components, PanelUVE(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildCentreUVE() {
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};
    m_gizmos = prism::make<EditorViewportGizmosUVE>(m_editor);
    m_cameraScreen = prism::make<CameraScreenUVE>();
    m_cameraScreen->setVisible(false);
    auto view = prism::make<prism::Overlay>();
    if (m_widgetMode) {
        // A widget is designed flat, at its screen resolution: the Designer, not a 3D view.
        m_viewport->setVisible(false);
        m_designer = prism::make<WidgetDesignerViewUVE>();
        m_designer->onPicked = [this](const Scene::EntityUVE entity) {
            if (entity != Scene::kInvalidEntityUVE) {
                m_editor.SelectEntityUVE(entity);
            }
        };
        m_designer->onMoved = [this](const Scene::EntityUVE entity, const float dx, const float dy, const bool finished) {
            MoveWidgetUVE(entity, dx, dy, finished);
        };
        m_designer->onResized = [this](const Scene::EntityUVE entity, const prism::Rect rect, const bool finished) {
            ResizeWidgetUVE(entity, rect, finished);
        };
        view->add(m_designer);
        // The Stage tab: the widget's 3D world, in the entity's own 3D view and gizmos.
        view->add(m_viewport);
        view->add(m_gizmos);
        m_gizmos->setVisible(false);
    } else {
        view->add(m_viewport);
        view->add(m_gizmos);
        view->add(m_cameraScreen);
    }

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
        const EditorUVE::EntityCompileProblemUVE& problem = problems[static_cast<std::size_t>(rows.front())];
        if (problem.entity != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(problem.entity);
        }
        // A Blueprint's problem: its graph, with the node picked.
        if (problem.scriptPath.ends_with(".uvbp")) {
            OpenBlueprintUVE(problem.scriptPath, problem.entity);
            m_blueprintPanel.SelectNodeUVE(problem.blueprintNode);
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

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildDockBarUVE() {
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

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildSequencerPageUVE() {
    return PanelUVE(m_sequencer.BuildUVE());
}

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildGraphPageUVE() {
    return PanelUVE(m_animGraphPanel.BuildUVE());
}

void EditorEntityEditorUVE::SetDockFullUVE(const bool full) {
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

void EditorEntityEditorUVE::SyncGraphDetailsUVE(const bool graphShown) {
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

void EditorEntityEditorUVE::ShowDockPanelUVE(const int index, const bool forceVisible) {
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

void EditorEntityEditorUVE::SyncAnimationTabsUVE() {
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

// ---------------------------------------------------------------------------------------- blueprint

void EditorEntityEditorUVE::ShowBlueprintMenuUVE() {
    // Only this entity's own objects: its root, then the objects under it (a Player with nothing
    // under it lists just the Player).
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("BLUEPRINT FOR"));
    for (const Scene::EntityUVE entity : m_editor.CollectEntityEditorObjectsUVE()) {
        std::string label = m_editor.GetEntityDisplayLabelUVE(entity);
        std::string existing;
        if (em.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
            const std::string& path = em.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath;
            if (path.ends_with(".uvbp")) {
                existing = path;
            }
        }
        const std::string kind(m_editor.GetObjectTypeNameUVE(entity));
        prism::MenuItem item = prism::MenuItem::Item(
            existing.empty() ? label : label + "   (" + std::filesystem::path(existing).stem().string() + ")",
            [this, entity, existing] {
                if (existing.empty()) {
                    ShowCreateBlueprintUVE(entity);
                } else {
                    OpenBlueprintUVE(existing, entity);
                }
            },
            existing.empty() ? kind : std::string("Open"), ObjectKindGlyphUVE(Scene::ResolveSceneObjectKindUVE(em, entity)));
        if (const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(Scene::ResolveSceneObjectKindUVE(em, entity), 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    }
    prism::showMenu(std::move(items), m_blueprintButton ? m_blueprintButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorEntityEditorUVE::ShowCreateBlueprintUVE(const Scene::EntityUVE target) {
    // Name (BP_<Object>) and the folder it goes to: the entity's own folder first, then every folder of Content.
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::vector<std::filesystem::path> folders{snapshot.contentRoot};
    if (m_editor.m_entityEditSession.has_value() && !m_editor.m_entityEditSession->assetPath.empty()) {
        const std::filesystem::path own = m_editor.m_entityEditSession->assetPath.parent_path();
        if (own != snapshot.contentRoot) {
            folders.insert(folders.begin(), own);
        }
    }
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
            const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
            if (std::find(folders.begin(), folders.end(), absolute) == folders.end()) {
                folders.push_back(absolute);
            }
        }
    }
    std::vector<std::string> labels;
    for (const std::filesystem::path& folder : folders) {
        const std::filesystem::path relative = folder.lexically_relative(snapshot.contentRoot);
        labels.push_back(relative.empty() || relative == "." ? std::string("Content") : "Content/" + relative.generic_string());
    }
    std::string base = "BP_";
    for (const char c : m_editor.GetEntityDisplayLabelUVE(target)) {
        base.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
    }
    auto name = prism::make<prism::TextInput>(base, "Name");
    auto folder = prism::make<prism::ComboBox>(labels, 0);
    auto form = prism::make<prism::PropertyGrid>();
    const int cat = form->addCategory("New Blueprint Class");
    form->addRow(cat, "Name", name, "The file's name: the class other objects use");
    form->addRow(cat, "Folder", folder, "Where the file goes in Content");
    form->addRow(cat, "Parent", prism::make<prism::Label>(std::string(m_editor.GetObjectTypeNameUVE(target))), "The object kind it drives");
    std::weak_ptr<prism::TextInput> weakName = name;
    std::weak_ptr<prism::ComboBox> weakFolder = folder;
    // Enter in the name field and the Create button can both fire: one Blueprint, made once.
    auto made = std::make_shared<bool>(false);
    const auto create = [this, target, folders, weakName, weakFolder, made] {
        const auto n = weakName.lock();
        const auto f = weakFolder.lock();
        if (*made || !n || !f || n->text().empty()) {
            return;
        }
        *made = true;
        const std::filesystem::path dir = folders[static_cast<std::size_t>(std::clamp(f->selected(), 0, static_cast<int>(folders.size()) - 1))];
        const std::filesystem::path file = EditorUVE::MakeUniqueContentPathUVE(dir, n->text(), ".uvbp");
        const std::string path = m_blueprintPanel.CreateForUVE(target, file);
        if (path.empty()) {
            m_editor.m_contentStatusMessage = "Could not create the Blueprint.";
            return;
        }
        OpenBlueprintUVE(path, target);
        // On disk at once: the entity's Script slot now names the Blueprint, so the level's copies
        // and the game run it (it used to live only in the open window until a Save).
        if (m_editor.SaveEntityEditorUVE()) {
            m_editor.m_contentStatusMessage = "Made " + file.filename().string() + " and attached it to " +
                                              m_editor.GetEntityDisplayLabelUVE(target) + ".";
        }
    };
    name->onCommit = [create](const std::string&) {
        create();
        if (prism::Application::exists()) {
            prism::Application::get().closeAllPopups();
        }
    };
    prism::showDialog("Blueprint Class", form, {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Create", true, create}}, 420.0F);
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(name.get());
    }
}

void EditorEntityEditorUVE::OpenBlueprintUVE(const std::string& path, const Scene::EntityUVE target) {
    if (m_blueprintPanel.IsOpenUVE() && m_blueprintPanel.PathUVE() != path && m_blueprintPanel.DirtyUVE()) {
        static_cast<void>(m_blueprintPanel.SaveUVE());   // the one being left keeps its edits
    }
    if (m_blueprintPanel.PathUVE() != path && !m_blueprintPanel.OpenUVE(path, target)) {
        return;
    }
    if (std::none_of(m_openBlueprints.begin(), m_openBlueprints.end(),
                     [&path](const OpenBlueprintEntryUVE& entry) { return entry.path == path; })) {
        m_openBlueprints.push_back(OpenBlueprintEntryUVE{path, target});
    }
    SetBlueprintModeUVE(true);
}

prism::Ref<prism::Widget> EditorEntityEditorUVE::BuildBlueprintViewUVE() {
    // The graph over the whole centre; the sub row's crumbs switch between it and the viewport.
    return m_blueprintPanel.BuildUVE();
}

void EditorEntityEditorUVE::CloseBlueprintUVE(const std::string& path) {
    if (m_blueprintPanel.PathUVE() == path) {
        if (m_blueprintPanel.DirtyUVE()) {
            static_cast<void>(m_blueprintPanel.SaveUVE());   // closing keeps its edits
        }
        m_blueprintPanel.CloseUVE();
    }
    std::erase_if(m_openBlueprints, [&path](const OpenBlueprintEntryUVE& entry) { return entry.path == path; });
    // The one left of it (or the viewport) takes its place.
    if (!m_blueprintPanel.IsOpenUVE() && m_blueprintMode && !m_openBlueprints.empty()) {
        const OpenBlueprintEntryUVE next = m_openBlueprints.back();
        OpenBlueprintUVE(next.path, next.target);
        return;
    }
    SetBlueprintModeUVE(m_blueprintMode);
}

void EditorEntityEditorUVE::SyncBlueprintCrumbsUVE() {
    if (!m_crumbRow) {
        return;
    }
    // A crumb whose object came back under a new handle (Simulate, Revert) follows it; the rest,
    // whose object left the entity, are forgotten.
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::vector<Scene::EntityUVE> objects = m_editor.CollectEntityEditorObjectsUVE();
    for (OpenBlueprintEntryUVE& entry : m_openBlueprints) {
        if (!em.IsAliveUVE(entry.target)) {
            entry.target = m_blueprintPanel.FindUserUVE(entry.path);
        }
    }
    std::erase_if(m_openBlueprints, [&](const OpenBlueprintEntryUVE& entry) {
        return !em.IsAliveUVE(entry.target) || std::find(objects.begin(), objects.end(), entry.target) == objects.end();
    });
    const std::string active = m_blueprintMode ? m_blueprintPanel.PathUVE() : std::string{};
    std::string signature = active + (m_blueprintPanel.DirtyUVE() ? "*" : "") + (m_stageMode ? "S" : "") + "|";
    if (m_widgetMode) {
        const Scene::IEntityManagerUVE& screenEm = m_editor.m_services->GetEntityManagerUVE();
        for (const Scene::EntityUVE screen : WidgetScreensUVE()) {
            signature += (screen == m_activeScreen ? "*" : "") +
                         (screenEm.HasComponentUVE<Scene::NameComponentUVE>(screen) ? screenEm.GetComponentUVE<Scene::NameComponentUVE>(screen).name
                                                                                 : std::string()) + ";";
        }
        signature += std::to_string(static_cast<int>(m_designer ? m_designer->WidthUVE() : 0.0F));
    }
    for (const OpenBlueprintEntryUVE& entry : m_openBlueprints) {
        signature += entry.path + ";";
    }
    if (signature == m_crumbSignature) {
        return;
    }
    m_crumbSignature = signature;
    m_crumbRow->clearChildren();
    if (m_widgetMode) {
        // Designer | Graph: the widget's screen, or its graph.
        // One Designer tab per screen (none: the widget is one screen), then + for another.
        const std::vector<Scene::EntityUVE> screens = WidgetScreensUVE();
        const Scene::IEntityManagerUVE& screenEm = m_editor.m_services->GetEntityManagerUVE();
        const bool designerPage = !m_blueprintMode && !m_stageMode;
        for (const Scene::EntityUVE screen : screens) {
            const std::string name = screenEm.HasComponentUVE<Scene::NameComponentUVE>(screen)
                                         ? screenEm.GetComponentUVE<Scene::NameComponentUVE>(screen).name
                                         : std::string("Screen");
            auto tab = prism::make<prism::Button>(name, prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            tab->setCheckable(true);
            const bool active = screen == m_activeScreen;
            tab->setChecked(designerPage && active);
            tab->setTooltip("Designer: the " + name + " screen. Click it again to rename or delete it");
            tab->onClick = [this, screen, active, designerPage] {
                if (active && designerPage) {
                    ShowScreenSettingsUVE(screen);
                    return;
                }
                m_activeScreen = screen;
                m_designerSignature = 0;
                SetWidgetPageUVE(0);
            };
            m_crumbRow->add(tab, CentreUVE());
        }
        if (screens.empty()) {
            auto designer = prism::make<prism::Button>("Designer", prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            designer->setCheckable(true);
            designer->setChecked(designerPage);
            designer->setTooltip("Designer: lay out the widget's text, buttons and images");
            designer->onClick = [this] { SetWidgetPageUVE(0); };
            m_crumbRow->add(designer, CentreUVE());
        }
        auto addScreen = prism::make<prism::Button>("", prism::Icon::Plus, prism::ButtonStyle::Toolbar);
        addScreen->setTooltip("Add a screen: another Designer (Settings, Pause, Credits ...) run by the same Graph");
        addScreen->onClick = [this] { AddWidgetScreenUVE(); };
        m_crumbRow->add(addScreen, CentreUVE());
        auto bar0 = prism::make<prism::Label>("|", prism::FontRole::Small);
        bar0->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_crumbRow->add(bar0, CentreUVE());
        auto graph = prism::make<prism::Button>(std::string("Graph") + (m_blueprintPanel.DirtyUVE() ? " *" : ""), prism::Icon::BlueprintClass,
                                                prism::ButtonStyle::Toolbar);
        graph->setCheckable(true);
        graph->setChecked(m_blueprintMode);
        graph->setTooltip("Graph: what the widget does (On Button Clicked, Set Text ...)");
        graph->onClick = [this] { OpenWidgetGraphUVE(); };
        auto stage = prism::make<prism::Button>("Stage", prism::Icon::Cube, prism::ButtonStyle::Toolbar);
        stage->setCheckable(true);
        stage->setChecked(!m_blueprintMode && m_stageMode);
        stage->setTooltip("Stage: the 3D world a 3D View shows its asset in - floor, lights, props, StageCamera, ModelSpot");
        stage->onClick = [this] { SetWidgetPageUVE(2); };
        m_crumbRow->add(graph, CentreUVE());
        m_crumbRow->add(stage, CentreUVE());
        if (!m_blueprintMode && !m_stageMode && m_designer) {
            // The screen the widget is designed for.
            auto bar = prism::make<prism::Label>("|", prism::FontRole::Small);
            bar->setColor(prism::ThemeColor(&prism::Theme::textDim));
            m_crumbRow->add(bar, CentreUVE());
            m_resolutionButton = prism::make<prism::Button>(
                std::to_string(static_cast<int>(m_designer->WidthUVE())) + " x " + std::to_string(static_cast<int>(m_designer->HeightUVE())),
                prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            m_resolutionButton->setTooltip("The screen size the Designer shows the widget at");
            m_resolutionButton->onClick = [this] { ShowResolutionMenuUVE(); };
            m_crumbRow->add(m_resolutionButton, CentreUVE());
        }
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
        return;
    }

    auto viewport = prism::make<prism::Button>("Viewport", prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
    viewport->setCheckable(true);
    viewport->setChecked(!m_blueprintMode);
    viewport->setTooltip("The entity's viewport");
    viewport->onClick = [this] { SetBlueprintModeUVE(false); };
    m_crumbRow->add(viewport, CentreUVE());
    if (!m_openBlueprints.empty()) {
        const auto chevron = [this] {
            auto label = prism::make<prism::Label>(">", prism::FontRole::Small);
            label->setColor(prism::ThemeColor(&prism::Theme::textDim));
            m_crumbRow->add(label, CentreUVE());
        };
        auto bar = prism::make<prism::Label>("|", prism::FontRole::Small);
        bar->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_crumbRow->add(bar, CentreUVE());
        // The folder of the Blueprint shown (or the first one): Content, or the folder it was made in.
        const std::string& shown = active.empty() ? m_openBlueprints.front().path : active;
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        const std::filesystem::path contentProject = m_blueprintPanel.ProjectPathUVE(snapshot.contentRoot);
        const std::filesystem::path folderRelative = std::filesystem::path(shown).parent_path().lexically_relative(contentProject);
        const bool atRoot = folderRelative.empty() || folderRelative == "." || *folderRelative.begin() == "..";
        auto folder = prism::make<prism::Button>(atRoot ? std::string("Content") : folderRelative.filename().string(), prism::Icon::Folder,
                                                 prism::ButtonStyle::Toolbar);
        folder->setTooltip(atRoot ? std::string("Content: show it in the Content Browser")
                                  : "Content/" + folderRelative.generic_string() + ": show it in the Content Browser");
        folder->onClick = [this, folderRelative, atRoot] {
            m_editor.m_contentBrowserDirectory = atRoot ? std::filesystem::path{} : folderRelative;
            m_editor.m_contentStatusMessage = "Content Browser: " + (atRoot ? std::string("Content") : "Content/" + folderRelative.generic_string());
        };
        m_crumbRow->add(folder, CentreUVE());
        for (const OpenBlueprintEntryUVE& entry : m_openBlueprints) {
            chevron();
            const bool isActive = entry.path == active;
            const std::string stem = std::filesystem::path(entry.path).stem().string();
            auto crumb = prism::make<prism::Button>(stem + (isActive && m_blueprintPanel.DirtyUVE() ? " *" : ""), prism::Icon::BlueprintClass,
                                                    prism::ButtonStyle::Toolbar);
            crumb->setCheckable(true);
            crumb->setChecked(isActive);
            crumb->setTooltip(entry.path + "  (" + m_editor.GetEntityDisplayLabelUVE(entry.target) + ")");
            crumb->onClick = [this, entry] { OpenBlueprintUVE(entry.path, entry.target); };
            m_crumbRow->add(crumb, CentreUVE());
            if (isActive) {
                auto close = ToolButtonUVE(prism::Icon::Close, "Close " + stem + " (its edits are saved)");
                close->onClick = [this, path = entry.path] { CloseBlueprintUVE(path); };
                m_crumbRow->add(close, CentreUVE());
            }
        }
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorEntityEditorUVE::SetBlueprintModeUVE(const bool on) {
    const bool want = on && m_blueprintPanel.IsOpenUVE();
    if (m_centreStack) {
        m_centreStack->setActiveIndex(want ? 1 : 0);
    }
    if (m_dockBar) {
        m_dockBar->setVisible(!want);
    }
    m_blueprintMode = want;
    SyncBlueprintTabUVE();
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorEntityEditorUVE::SaveAllUVE() {
    if (m_blueprintPanel.DirtyUVE()) {
        static_cast<void>(m_blueprintPanel.SaveUVE());
    }
    static_cast<void>(m_editor.SaveEntityEditorUVE());
}

void EditorEntityEditorUVE::SyncBlueprintTabUVE() {
    // The strip's tab names the Blueprint (with * while unsaved); its Details tab is there in Blueprint mode.
    const bool open = m_blueprintPanel.IsOpenUVE();
    if (!open && m_blueprintMode) {
        SetBlueprintModeUVE(false);   // its object went away
        return;
    }
    SyncBlueprintCrumbsUVE();
    const bool shown = open && m_blueprintMode;
    if (!m_detailsTabs || shown == m_blueprintDetailsShown) {
        return;
    }
    if (shown) {
        static_cast<void>(m_detailsTabs->insertTab(m_detailsTabs->count(),
                                                   prism::TabView::Page{"Blueprint", prism::Icon::BlueprintClass, m_blueprintPanel.SideUVE(), false}));
        m_detailsTabs->setCurrent(m_detailsTabs->count() - 1);
    } else if (const int index = m_detailsTabs->indexOf("Blueprint"); index >= 0) {
        static_cast<void>(m_detailsTabs->takeTab(index));
        m_detailsTabs->setCurrent(0);
    }
    m_blueprintDetailsShown = shown;
}

// --------------------------------------------------------------------------------- widget mode

void EditorEntityEditorUVE::OpenWidgetGraphUVE() {
    // The graph is the widget's own, kept in its file; it runs on the root Canvas.
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        return;
    }
    const std::string path = m_blueprintPanel.ProjectPathUVE(m_openPath);
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    // An older widget without its Script slot runs its graph once it has one (an edit like any other:
    // the widget is unsaved until it is written, and Undo takes it back).
    if (!em.HasComponentUVE<Scene::ScriptComponentUVE>(root) ||
        em.GetComponentUVE<Scene::ScriptComponentUVE>(root).scriptAssetPath != path) {
        Scene::ScriptComponentUVE script;
        if (em.HasComponentUVE<Scene::ScriptComponentUVE>(root)) {
            script = em.GetComponentUVE<Scene::ScriptComponentUVE>(root);
        }
        script.scriptAssetPath = path;
        m_editor.SelectEntityUVE(root);
        static_cast<void>(m_editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Script, script));
    }
    OpenBlueprintUVE(path, root);
}

void EditorEntityEditorUVE::RefreshDesignerUVE() {
    if (!m_designer) {
        return;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (m_dragWidget == Scene::kInvalidEntityUVE) {
        // Boxes place their children as the game will, so the Designer shows what the player sees.
        static const UI::UIFontAtlasUVE atlas;
        UI::LayoutUIContainersUVE(em, atlas);
    }
    std::vector<WidgetDesignerViewUVE::ItemUVE> items;
    std::uint64_t signature = 1469598103934665603ULL;
    const auto toColor = [](const Math::Vector3UVE& c, const float alpha = 1.0F) {
        const auto byte = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
        return prism::Color(byte(c.x), byte(c.y), byte(c.z), byte(alpha));
    };
    const auto fraction = [](const float value, const float lo, const float hi) { return hi > lo ? (value - lo) / (hi - lo) : 0.0F; };
    // Only the active screen's widgets (a widget with screens shows one at a time).
    const Scene::EntityUVE screenRoot = DesignerParentUVE();
    const bool screened = screenRoot != m_editor.GetEntityEditorRootUVE();
    const auto onScreen = [&](Scene::EntityUVE entity) {
        if (!screened) {
            return true;
        }
        for (int depth = 0; depth < 256 && em.IsAliveUVE(entity); ++depth) {
            if (entity == screenRoot) {
                return true;
            }
            if (!em.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
                return false;
            }
            entity = em.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
        }
        return false;
    };
    HashUVE(signature, (static_cast<std::uint64_t>(screenRoot.generation) << 32U) | screenRoot.index);
    // The screen size: the widget's reference resolution, else this device's.
    if (const Math::Vector2UVE design = DesignResolutionUVE();
        design.x != m_designer->WidthUVE() || design.y != m_designer->HeightUVE()) {
        m_designer->SetResolutionUVE(design.x, design.y);
        m_crumbSignature.clear();
        m_designerSignature = 0;
    }
    for (const Scene::EntityUVE entity : m_editor.CollectEntityEditorObjectsUVE()) {
        if (!em.IsAliveUVE(entity) || !onScreen(entity)) {
            continue;
        }
        WidgetDesignerViewUVE::ItemUVE item;
        item.entity = entity;
        if (em.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
            item.parent = em.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
        }
        item.name = em.HasComponentUVE<Scene::NameComponentUVE>(entity) ? em.GetComponentUVE<Scene::NameComponentUVE>(entity).name : std::string{};
        item.visible = !em.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) || em.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visible;
        const auto fromRect = [](const Math::RectUVE& r) { return prism::Rect{r.position.x, r.position.y, r.size.x, r.size.y}; };
        const std::optional<WidgetKindUVE> kind = ClassifyWidgetUVE(em, entity);
        if (!kind.has_value() || *kind == WidgetKindUVE::Canvas) {
            continue;
        }
        item.kind = *kind;
        switch (*kind) {
            case WidgetKindUVE::Button: {
                const auto& b = em.GetComponentUVE<Scene::UIButtonComponentUVE>(entity);
                item.rect = fromRect(b.rect);
                item.color = toColor(b.normalColor);
                break;
            }
            case WidgetKindUVE::Text: {
                const auto& t = em.GetComponentUVE<Scene::UITextComponentUVE>(entity);
                item.text = t.text;
                item.fontSize = t.fontSize;
                item.color = toColor(t.color, t.alpha);
                item.rect = prism::Rect{t.positionPixels.x, t.positionPixels.y,
                                        std::max(8.0F, static_cast<float>(t.text.size()) * t.fontSize * 0.55F), t.fontSize * 1.25F};
                break;
            }
            case WidgetKindUVE::Image:
            case WidgetKindUVE::Border:
            case WidgetKindUVE::Spacer: {
                const auto& i = em.GetComponentUVE<Scene::UIImageComponentUVE>(entity);
                item.rect = fromRect(i.rect);
                item.color = toColor(i.tintColor, *kind == WidgetKindUVE::Spacer ? 1.0F : i.alpha);
                break;
            }
            case WidgetKindUVE::ProgressBar: {
                const auto& b = em.GetComponentUVE<Scene::UIProgressBarComponentUVE>(entity);
                item.rect = fromRect(b.rect);
                item.value = fraction(b.value, b.minValue, b.maxValue);
                break;
            }
            case WidgetKindUVE::Slider: {
                const auto& sl = em.GetComponentUVE<Scene::UISliderComponentUVE>(entity);
                item.rect = fromRect(sl.rect);
                item.value = fraction(sl.value, sl.minValue, sl.maxValue);
                break;
            }
            case WidgetKindUVE::CheckBox: {
                const auto& cb = em.GetComponentUVE<Scene::UICheckboxComponentUVE>(entity);
                item.rect = fromRect(cb.rect);
                item.checked = cb.checked;
                break;
            }
            case WidgetKindUVE::TextBox: {
                const auto& ti = em.GetComponentUVE<Scene::UITextInputComponentUVE>(entity);
                item.rect = fromRect(ti.rect);
                item.text = ti.text.empty() ? ti.placeholder : ti.text;
                item.fontSize = ti.fontSize;
                break;
            }
            case WidgetKindUVE::ComboBox: {
                const auto& box = em.GetComponentUVE<Scene::UIDropdownComponentUVE>(entity);
                item.rect = fromRect(box.rect);
                item.fontSize = box.fontSize;
                item.color = toColor(box.textColor);
                // The chosen option, as the game shows it closed.
                std::vector<std::string> options;
                for (std::size_t begin = 0U; begin <= box.options.size();) {
                    const std::size_t end = box.options.find('\n', begin);
                    const std::string line = box.options.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                    if (!line.empty()) {
                        options.push_back(line);
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    begin = end + 1U;
                }
                item.text = box.selectedIndex >= 0 && static_cast<std::size_t>(box.selectedIndex) < options.size()
                                ? options[static_cast<std::size_t>(box.selectedIndex)]
                                : box.placeholder;
                break;
            }
            case WidgetKindUVE::VerticalBox:
            case WidgetKindUVE::HorizontalBox:
            case WidgetKindUVE::WrapBox:
                item.rect = fromRect(em.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(entity).rect);
                break;
            case WidgetKindUVE::ScrollBox:
                item.rect = fromRect(em.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(entity).rect);
                break;
            case WidgetKindUVE::View3D: {
                const auto& view = em.GetComponentUVE<Scene::UIView3DComponentUVE>(entity);
                item.rect = fromRect(view.rect);
                item.text = view.assetPath.empty() ? std::string{} : std::filesystem::path(view.assetPath).filename().string();
                break;
            }
            case WidgetKindUVE::Canvas:
                break;
        }
        HashUVE(signature, (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index);
        HashUVE(signature, item.text + "|" + item.name);
        for (const float v : {item.rect.x, item.rect.y, item.rect.w, item.rect.h, item.value, item.fontSize}) {
            HashUVE(signature, static_cast<std::uint64_t>(std::lround(v * 100.0F)));
        }
        HashUVE(signature, (static_cast<std::uint64_t>(item.color.r) << 24U) | (static_cast<std::uint64_t>(item.color.g) << 16U) |
                               (static_cast<std::uint64_t>(item.color.b) << 8U) | item.color.a);
        HashUVE(signature, static_cast<std::uint64_t>(item.visible) * 2U + static_cast<std::uint64_t>(item.checked));
        items.push_back(std::move(item));
    }
    if (signature != m_designerSignature && m_dragWidget == Scene::kInvalidEntityUVE) {
        m_designerSignature = signature;
        m_designer->SetItemsUVE(std::move(items));
    }
    m_designer->SetSelectedUVE(m_editor.GetSelectedEntityUVE());
    // One 3D View shows its asset live: the selected one, else the first with an asset.
    Scene::EntityUVE live = Scene::kInvalidEntityUVE;
    for (const WidgetDesignerViewUVE::ItemUVE& item : m_designer->ItemsUVE()) {
        if (item.kind != WidgetKindUVE::View3D || item.text.empty() || !item.visible) {
            continue;
        }
        if (item.entity == m_editor.GetSelectedEntityUVE()) {
            live = item.entity;
            break;
        }
        if (live == Scene::kInvalidEntityUVE) {
            live = item.entity;
        }
    }
    m_designer->SetLivePreviewUVE(m_blueprintMode ? Scene::kInvalidEntityUVE : live);
}

namespace {

/// A widget's design position: a Text's pen, a control's rect corner. Nothing for anything else.
[[nodiscard]] Math::Vector2UVE* WidgetPositionUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity) {
    if (em.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIButtonComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UITextComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UITextComponentUVE>(entity).positionPixels;
    }
    if (em.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIImageComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UIProgressBarComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIProgressBarComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UISliderComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UISliderComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UICheckboxComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UICheckboxComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UITextInputComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UITextInputComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UIDropdownComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIDropdownComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(entity).rect.position;
    }
    if (em.HasComponentUVE<Scene::UIView3DComponentUVE>(entity)) {
        return &em.GetComponentUVE<Scene::UIView3DComponentUVE>(entity).rect.position;
    }
    return nullptr;
}

/// The component holding a widget's position, for its undo step.
[[nodiscard]] std::type_index WidgetComponentTypeUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity) {
    if (em.HasComponentUVE<Scene::UIView3DComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UIView3DComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UIButtonComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UITextComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UITextComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UIImageComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UIProgressBarComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UIProgressBarComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UISliderComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UISliderComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UICheckboxComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UICheckboxComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UITextInputComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UITextInputComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UIDropdownComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UIDropdownComponentUVE));
    }
    if (em.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(entity)) {
        return std::type_index(typeid(Scene::UILayoutContainerComponentUVE));
    }
    return std::type_index(typeid(Scene::UIScrollContainerComponentUVE));
}

template <typename Component>
bool CommitWidgetComponentUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity, const std::type_index type,
                              const Math::Vector2UVE start, const std::function<void(const Core::TypeMetadataEntryUVE&, const void*)>& set) {
    if (type != std::type_index(typeid(Component))) {
        return false;
    }
    auto& component = em.GetComponentUVE<Component>(entity);
    Component next = component;
    // Back where it started, then set where it was let go: one step from there to here.
    if constexpr (std::is_same_v<Component, Scene::UITextComponentUVE>) {
        component.positionPixels = start;
    } else {
        component.rect.position = start;
    }
    if (const Core::TypeMetadataEntryUVE* const entry = Scene::FindSceneComponentMetadataUVE(type)) {
        set(*entry, &next);
    } else {
        component = next;
    }
    return true;
}

} // namespace

void EditorEntityEditorUVE::MoveWidgetUVE(const Scene::EntityUVE entity, const float dx, const float dy, const bool finished) {
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (!em.IsAliveUVE(entity)) {
        m_dragWidget = Scene::kInvalidEntityUVE;
        m_dragStarts.clear();
        return;
    }
    if (finished) {
        if (m_dragWidget != entity) {
            return;
        }
        const auto set = [this](const Core::TypeMetadataEntryUVE& entry, const void* value) {
            static_cast<void>(m_editor.SetSelectedComponentValueUVE(entry, value));
        };
        for (const auto& [moved, start] : m_dragStarts) {
            if (!em.IsAliveUVE(moved) || WidgetPositionUVE(em, moved) == nullptr) {
                continue;
            }
            m_editor.SelectEntityUVE(moved);
            const std::type_index type = WidgetComponentTypeUVE(em, moved);
            const Math::Vector2UVE from{start[0], start[1]};
            static_cast<void>(CommitWidgetComponentUVE<Scene::UIButtonComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UITextComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UIImageComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UIProgressBarComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UISliderComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UICheckboxComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UITextInputComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UIDropdownComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UILayoutContainerComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UIScrollContainerComponentUVE>(em, moved, type, from, set) ||
                              CommitWidgetComponentUVE<Scene::UIView3DComponentUVE>(em, moved, type, from, set));
        }
        m_editor.SelectEntityUVE(entity);
        m_dragWidget = Scene::kInvalidEntityUVE;
        m_dragStarts.clear();
        m_designerSignature = 0;
        return;
    }
    if (m_dragWidget != entity) {
        // It, and every widget inside it (a Button's label), move together.
        m_dragWidget = entity;
        m_dragStarts.clear();
        for (const Scene::EntityUVE object : m_editor.CollectEntityEditorObjectsUVE()) {
            bool inside = object == entity;
            for (Scene::EntityUVE cursor = object; !inside && em.HasComponentUVE<Scene::HierarchyComponentUVE>(cursor);) {
                cursor = em.GetComponentUVE<Scene::HierarchyComponentUVE>(cursor).parent;
                if (cursor == Scene::kInvalidEntityUVE || !em.IsAliveUVE(cursor)) {
                    break;
                }
                inside = cursor == entity;
            }
            if (const Math::Vector2UVE* position = inside ? WidgetPositionUVE(em, object) : nullptr) {
                m_dragStarts.emplace_back(object, std::array<float, 2>{position->x, position->y});
            }
        }
    }
    for (const auto& [moved, start] : m_dragStarts) {
        if (Math::Vector2UVE* position = em.IsAliveUVE(moved) ? WidgetPositionUVE(em, moved) : nullptr) {
            position->x += dx;
            position->y += dy;
            // A Scroll Box's scrollbar rides along its edge.
            if (em.HasComponentUVE<Scene::UIScrollbarComponentUVE>(moved)) {
                auto& bar = em.GetComponentUVE<Scene::UIScrollbarComponentUVE>(moved).rect.position;
                bar.x += dx;
                bar.y += dy;
            }
        }
    }
}

namespace {

/// A rect widget's rect (not a Text's: it has a pen, not a rect).
[[nodiscard]] const Math::RectUVE* WidgetRectUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE e) {
    if (em.HasComponentUVE<Scene::UIView3DComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIView3DComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UIButtonComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIButtonComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UIImageComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIImageComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UIProgressBarComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIProgressBarComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UISliderComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UISliderComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UICheckboxComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UICheckboxComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UITextInputComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UITextInputComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UIDropdownComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIDropdownComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(e).rect;
    }
    if (em.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(e)) {
        return &em.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(e).rect;
    }
    return nullptr;
}

template <typename Component>
void SetWidgetRectOnUVE(Component& component, const Math::RectUVE& rect) {
    component.rect = rect;
}

void SetWidgetRectOnUVE(Scene::UITextComponentUVE& text, const Math::RectUVE& rect) {
    // A Text is sized by its letters: the handle's height is its font size.
    text.positionPixels = rect.position;
    text.fontSize = std::clamp(rect.size.y / 1.25F, 6.0F, 400.0F);
}

using SetWidgetComponentUVE = std::function<void(const Core::TypeMetadataEntryUVE&, const void*)>;

/// Sets `rect` live, or (commit) puts `start` back and records `rect` as one undo step.
template <typename Component>
bool ResizeWidgetComponentUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity, const Math::RectUVE& rect,
                              const Math::RectUVE& start, const bool commit, const SetWidgetComponentUVE& set) {
    if (!em.HasComponentUVE<Component>(entity)) {
        return false;
    }
    Component& component = em.GetComponentUVE<Component>(entity);
    if (!commit) {
        SetWidgetRectOnUVE(component, rect);
        return true;
    }
    Component next = component;
    SetWidgetRectOnUVE(next, rect);
    SetWidgetRectOnUVE(component, start);
    if (const Core::TypeMetadataEntryUVE* const entry = Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Component)))) {
        set(*entry, &next);
    } else {
        component = next;
    }
    return true;
}

bool ResizeAnyWidgetUVE(Scene::IEntityManagerUVE& em, const Scene::EntityUVE e, const Math::RectUVE& r, const Math::RectUVE& start,
                        const bool commit, const SetWidgetComponentUVE& set) {
    return ResizeWidgetComponentUVE<Scene::UIView3DComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UIButtonComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UIImageComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UIProgressBarComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UISliderComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UICheckboxComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UITextInputComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UIDropdownComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UILayoutContainerComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UIScrollContainerComponentUVE>(em, e, r, start, commit, set) ||
           ResizeWidgetComponentUVE<Scene::UITextComponentUVE>(em, e, r, start, commit, set);
}

[[nodiscard]] Math::RectUVE ToEngineRectUVE(const prism::Rect& r) {
    return Math::RectUVE{Math::Vector2UVE{r.x, r.y}, Math::Vector2UVE{r.w, r.h}};
}

} // namespace

void EditorEntityEditorUVE::ResizeWidgetUVE(const Scene::EntityUVE entity, const prism::Rect rect, const bool finished) {
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (!em.IsAliveUVE(entity)) {
        m_resizeWidget = Scene::kInvalidEntityUVE;
        return;
    }
    if (m_resizeWidget != entity) {
        // Where it started: the component as it is before this first change.
        m_resizeWidget = entity;
        m_resizeStartRect = rect;
        if (em.HasComponentUVE<Scene::UITextComponentUVE>(entity) && !em.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
            const auto& text = em.GetComponentUVE<Scene::UITextComponentUVE>(entity);
            m_resizeStartRect = prism::Rect{text.positionPixels.x, text.positionPixels.y, rect.w, text.fontSize * 1.25F};
        } else if (const Math::RectUVE* current = WidgetRectUVE(em, entity)) {
            m_resizeStartRect = prism::Rect{current->position.x, current->position.y, current->size.x, current->size.y};
        }
    }
    const SetWidgetComponentUVE set = [this](const Core::TypeMetadataEntryUVE& entry, const void* value) {
        static_cast<void>(m_editor.SetSelectedComponentValueUVE(entry, value));
    };
    if (!finished) {
        static_cast<void>(ResizeAnyWidgetUVE(em, entity, ToEngineRectUVE(rect), ToEngineRectUVE(m_resizeStartRect), false, set));
        return;
    }
    m_editor.SelectEntityUVE(entity);
    static_cast<void>(ResizeAnyWidgetUVE(em, entity, ToEngineRectUVE(rect), ToEngineRectUVE(m_resizeStartRect), true, set));
    m_resizeWidget = Scene::kInvalidEntityUVE;
    m_designerSignature = 0;
}

void EditorEntityEditorUVE::ShowResolutionMenuUVE() {
    struct PresetUVE {
        const char* label;
        float width;
        float height;
    };
    static constexpr PresetUVE kPresetsUVE[] = {
        {"Desktop  1920 x 1080", 1920.0F, 1080.0F}, {"Desktop  2560 x 1440", 2560.0F, 1440.0F},
        {"Desktop  1280 x 720", 1280.0F, 720.0F},   {"Ultrawide  3440 x 1440", 3440.0F, 1440.0F},
        {"Phone  2340 x 1080", 2340.0F, 1080.0F},   {"Phone (portrait)  1080 x 2340", 1080.0F, 2340.0F},
        {"Tablet  2048 x 1536", 2048.0F, 1536.0F},
    };
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("SCREEN SIZE"));
    {
        Math::Vector2UVE device{1920.0F, 1080.0F};
        for (const Window::MonitorInfoUVE& monitor : m_editor.m_services->GetWindowManagerUVE().EnumerateMonitorsUVE()) {
            if (monitor.width > 0U && monitor.height > 0U) {
                device = Math::Vector2UVE{static_cast<float>(monitor.width), static_cast<float>(monitor.height)};
                if (monitor.isPrimary) {
                    break;
                }
            }
        }
        prism::MenuItem automatic = prism::MenuItem::Item(
            "Auto - this device  " + std::to_string(static_cast<int>(device.x)) + " x " + std::to_string(static_cast<int>(device.y)),
            [this] { SetDesignResolutionUVE(Math::Vector2UVE{0.0F, 0.0F}); });
        items.push_back(std::move(automatic));
    }
    for (const PresetUVE& preset : kPresetsUVE) {
        prism::MenuItem item = prism::MenuItem::Item(preset.label, [this, preset] {
            SetDesignResolutionUVE(Math::Vector2UVE{preset.width, preset.height});
        });
        item.checked = m_designer && m_designer->WidthUVE() == preset.width && m_designer->HeightUVE() == preset.height;
        items.push_back(std::move(item));
    }
    prism::showSearchMenu(std::move(items), m_resolutionButton ? m_resolutionButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search sizes");
}

namespace {

/// Where a new widget of `size` goes on the Canvas: the first place, reading left to right and
/// top to bottom on a 16-pixel grid, where it overlaps nothing already there (with a gap around
/// it), the way a layout tool places a new piece. Off the edge of a full Canvas, it steps down
/// from the last widget instead of sitting on top of it.
[[nodiscard]] Math::Vector2UVE FindFreeSpotUVE(const std::vector<WidgetDesignerViewUVE::ItemUVE>& items, const Math::Vector2UVE size,
                                               const float canvasW, const float canvasH) {
    constexpr float kMarginUVE = 64.0F;
    constexpr float kGapUVE = 24.0F;
    constexpr float kStepUVE = 16.0F;
    const auto overlaps = [&items, size](const float x, const float y) {
        for (const WidgetDesignerViewUVE::ItemUVE& item : items) {
            if (!item.visible || item.kind == WidgetKindUVE::Spacer) {
                continue;
            }
            const prism::Rect& r = item.rect;
            if (x < r.x + r.w + kGapUVE && x + size.x + kGapUVE > r.x && y < r.y + r.h + kGapUVE && y + size.y + kGapUVE > r.y) {
                return true;
            }
        }
        return false;
    };
    for (float y = kMarginUVE; y + size.y <= canvasH - kMarginUVE * 0.5F; y += kStepUVE) {
        for (float x = kMarginUVE; x + size.x <= canvasW - kMarginUVE * 0.5F; x += kStepUVE) {
            if (!overlaps(x, y)) {
                return Math::Vector2UVE{x, y};
            }
        }
    }
    float bottom = kMarginUVE;
    for (const WidgetDesignerViewUVE::ItemUVE& item : items) {
        bottom = std::max(bottom, item.rect.y + item.rect.h + kGapUVE);
    }
    return Math::Vector2UVE{kMarginUVE, bottom};
}

} // namespace

void EditorEntityEditorUVE::ShowWidgetPaletteUVE() {
    // The Palette: what a widget is built from, each with its own icon. A new widget goes into the
    // selected panel (a Vertical Box, a Scroll Box ...), else onto the Canvas at the first free
    // place for its size - never on top of another.
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    Scene::EntityUVE parent = DesignerParentUVE();
    bool intoPanel = false;
    if (const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE(); em.IsAliveUVE(selected)) {
        if (const std::optional<WidgetKindUVE> kind = ClassifyWidgetUVE(em, selected);
            kind == WidgetKindUVE::VerticalBox || kind == WidgetKindUVE::HorizontalBox || kind == WidgetKindUVE::WrapBox ||
            kind == WidgetKindUVE::ScrollBox) {
            parent = selected;
            intoPanel = true;
        }
    }
    // A shape is given where the widget goes.
    using Shape = std::function<void(Scene::IEntityManagerUVE&, Scene::EntityUVE, Math::Vector2UVE)>;
    const auto place = [this, parent, intoPanel](const std::string& name, const SceneObjectKindUVE kind, const Math::Vector2UVE size,
                                                 const Shape& shape) {
        Math::Vector2UVE at{64.0F, 64.0F};
        if (intoPanel) {
            // The panel lays it out; it starts at the panel's corner.
            if (m_designer) {
                for (const WidgetDesignerViewUVE::ItemUVE& item : m_designer->ItemsUVE()) {
                    if (item.entity == parent) {
                        at = Math::Vector2UVE{item.rect.x + 8.0F, item.rect.y + 8.0F};
                    }
                }
            }
        } else if (m_designer) {
            at = FindFreeSpotUVE(m_designer->ItemsUVE(), size, m_designer->WidthUVE(), m_designer->HeightUVE());
        }
        const Scene::EntityUVE made = m_editor.CreateDocumentSceneObjectUVE(kind, parent);
        if (made == Scene::kInvalidEntityUVE) {
            return;
        }
        Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
        if (entities.HasComponentUVE<Scene::NameComponentUVE>(made)) {
            entities.GetComponentUVE<Scene::NameComponentUVE>(made).name = name;
        }
        if (shape) {
            shape(entities, made, at);
        }
        // A Button comes with its label, centred on it, as a Button does in a layout tool.
        if (kind == SceneObjectKindUVE::UIButton) {
            const Scene::EntityUVE label = m_editor.CreateDocumentSceneObjectUVE(SceneObjectKindUVE::UIText, made);
            if (label != Scene::kInvalidEntityUVE && entities.HasComponentUVE<Scene::UITextComponentUVE>(label)) {
                if (entities.HasComponentUVE<Scene::NameComponentUVE>(label)) {
                    entities.GetComponentUVE<Scene::NameComponentUVE>(label).name = name + "Label";
                }
                auto& text = entities.GetComponentUVE<Scene::UITextComponentUVE>(label);
                text.text = "Button";
                text.fontSize = 22.0F;
                const float width = static_cast<float>(text.text.size()) * text.fontSize * 0.55F;
                text.positionPixels = Math::Vector2UVE{at.x + (size.x - width) * 0.5F, at.y + (size.y - text.fontSize * 1.1F) * 0.5F};
            }
        }
        m_editor.SelectEntityUVE(made);
        m_designerSignature = 0;
    };
    // A widget that is not a Text, Button or Image starts as an Image and takes its own component.
    const auto instead = [](auto component) -> Shape {
        return [component](Scene::IEntityManagerUVE& entities, const Scene::EntityUVE entity, const Math::Vector2UVE at) mutable {
            if (entities.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
                entities.RemoveComponentUVE<Scene::UIImageComponentUVE>(entity);
            }
            component.rect.position = at;
            entities.AddComponentUVE<decltype(component)>(entity, component);
        };
    };
    const auto image = [](Scene::UIImageComponentUVE component) -> Shape {
        return [component](Scene::IEntityManagerUVE& entities, const Scene::EntityUVE entity, const Math::Vector2UVE at) mutable {
            if (entities.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
                component.rect.position = at;
                entities.GetComponentUVE<Scene::UIImageComponentUVE>(entity) = component;
            }
        };
    };
    std::vector<prism::MenuItem> items;
    const auto entry = [&items, place](const WidgetKindUVE kind, const std::string& name, const SceneObjectKindUVE objectKind,
                                       const Math::Vector2UVE size, Shape shape) {
        prism::MenuItem item =
            prism::MenuItem::Item(WidgetKindLabelUVE(kind), [place, name, objectKind, size, shape] { place(name, objectKind, size, shape); });
        if (const EditorIconsUVE::ImageUVE icon = WidgetIconUVE(kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    };
    const auto sized = [](auto component, const Math::Vector2UVE size) {
        component.rect.size = size;
        return component;
    };

    items.push_back(prism::MenuItem::Header("COMMON"));
    entry(WidgetKindUVE::Text, "Text", SceneObjectKindUVE::UIText, {170.0F, 30.0F},
          [](Scene::IEntityManagerUVE& entities, const Scene::EntityUVE e, const Math::Vector2UVE at) {
              if (entities.HasComponentUVE<Scene::UITextComponentUVE>(e)) {
                  auto& text = entities.GetComponentUVE<Scene::UITextComponentUVE>(e);
                  text.text = "Text Block";
                  text.positionPixels = at;
                  text.fontSize = 24.0F;
              }
          });
    entry(WidgetKindUVE::Button, "Button", SceneObjectKindUVE::UIButton, {200.0F, 52.0F},
          [](Scene::IEntityManagerUVE& entities, const Scene::EntityUVE e, const Math::Vector2UVE at) {
              if (entities.HasComponentUVE<Scene::UIButtonComponentUVE>(e)) {
                  auto& button = entities.GetComponentUVE<Scene::UIButtonComponentUVE>(e);
                  button.rect = Math::RectUVE{at, Math::Vector2UVE{200.0F, 52.0F}};
                  button.normalColor = Math::Vector3UVE{0.16F, 0.42F, 0.86F};
                  button.hoverColor = Math::Vector3UVE{0.24F, 0.52F, 0.95F};
                  button.pressedColor = Math::Vector3UVE{0.10F, 0.30F, 0.66F};
              }
          });
    entry(WidgetKindUVE::Image, "Image", SceneObjectKindUVE::UIImage, {128.0F, 128.0F}, image(sized(Scene::UIImageComponentUVE{}, {128.0F, 128.0F})));
    Scene::UIImageComponentUVE border = sized(Scene::UIImageComponentUVE{}, {320.0F, 200.0F});
    border.tintColor = Math::Vector3UVE{0.10F, 0.11F, 0.14F};
    border.alpha = 0.85F;
    border.nineSliceEnabled = true;
    entry(WidgetKindUVE::Border, "Border", SceneObjectKindUVE::UIImage, {320.0F, 200.0F}, image(border));
    Scene::UIImageComponentUVE spacer = sized(Scene::UIImageComponentUVE{}, {40.0F, 40.0F});
    spacer.alpha = 0.0F;
    entry(WidgetKindUVE::Spacer, "Spacer", SceneObjectKindUVE::UIImage, {40.0F, 40.0F}, image(spacer));

    items.push_back(prism::MenuItem::Header("INPUT"));
    entry(WidgetKindUVE::CheckBox, "CheckBox", SceneObjectKindUVE::UIImage, {28.0F, 28.0F},
          instead(sized(Scene::UICheckboxComponentUVE{}, {28.0F, 28.0F})));
    Scene::UISliderComponentUVE slider = sized(Scene::UISliderComponentUVE{}, {260.0F, 24.0F});
    slider.value = 0.5F;
    entry(WidgetKindUVE::Slider, "Slider", SceneObjectKindUVE::UIImage, {260.0F, 24.0F}, instead(slider));
    Scene::UITextInputComponentUVE input = sized(Scene::UITextInputComponentUVE{}, {300.0F, 36.0F});
    input.placeholder = "Type here...";
    entry(WidgetKindUVE::TextBox, "TextBox", SceneObjectKindUVE::UIImage, {300.0F, 36.0F}, instead(input));
    Scene::UIDropdownComponentUVE combo = sized(Scene::UIDropdownComponentUVE{}, {220.0F, 32.0F});
    combo.options = "Option 1\nOption 2\nOption 3";
    combo.selectedIndex = 0;
    entry(WidgetKindUVE::ComboBox, "ComboBox", SceneObjectKindUVE::UIImage, {220.0F, 32.0F}, instead(combo));

    items.push_back(prism::MenuItem::Header("FEEDBACK"));
    Scene::UIProgressBarComponentUVE bar = sized(Scene::UIProgressBarComponentUVE{}, {320.0F, 24.0F});
    bar.value = 0.6F;
    entry(WidgetKindUVE::ProgressBar, "ProgressBar", SceneObjectKindUVE::UIImage, {320.0F, 24.0F}, instead(bar));

    items.push_back(prism::MenuItem::Header("3D"));
    entry(WidgetKindUVE::View3D, "View3D", SceneObjectKindUVE::UIImage, {360.0F, 480.0F},
          instead(sized(Scene::UIView3DComponentUVE{}, {360.0F, 480.0F})));

    items.push_back(prism::MenuItem::Header("PANEL"));
    Scene::UILayoutContainerComponentUVE vertical = sized(Scene::UILayoutContainerComponentUVE{}, {260.0F, 300.0F});
    vertical.direction = Scene::UILayoutDirectionUVE::Vertical;
    vertical.padding = 8.0F;
    vertical.spacing = 8.0F;
    entry(WidgetKindUVE::VerticalBox, "VerticalBox", SceneObjectKindUVE::UIImage, {260.0F, 300.0F}, instead(vertical));
    Scene::UILayoutContainerComponentUVE horizontal = sized(vertical, {480.0F, 80.0F});
    horizontal.direction = Scene::UILayoutDirectionUVE::Horizontal;
    entry(WidgetKindUVE::HorizontalBox, "HorizontalBox", SceneObjectKindUVE::UIImage, {480.0F, 80.0F}, instead(horizontal));
    Scene::UILayoutContainerComponentUVE wrap = sized(horizontal, {420.0F, 260.0F});
    wrap.wrapAfter = 3U;
    entry(WidgetKindUVE::WrapBox, "WrapBox", SceneObjectKindUVE::UIImage, {420.0F, 260.0F}, instead(wrap));
    const Scene::UIScrollContainerComponentUVE scroll = sized(Scene::UIScrollContainerComponentUVE{}, {280.0F, 320.0F});
    entry(WidgetKindUVE::ScrollBox, "ScrollBox", SceneObjectKindUVE::UIImage, {280.0F, 320.0F},
          [scroll](Scene::IEntityManagerUVE& entities, const Scene::EntityUVE e, const Math::Vector2UVE at) {
              if (entities.HasComponentUVE<Scene::UIImageComponentUVE>(e)) {
                  entities.RemoveComponentUVE<Scene::UIImageComponentUVE>(e);
              }
              Scene::UIScrollContainerComponentUVE placed = scroll;
              placed.rect.position = at;
              entities.AddComponentUVE<Scene::UIScrollContainerComponentUVE>(e, placed);
              // Its scrollbar on the same widget, down its right edge.
              Scene::UIScrollbarComponentUVE track;
              track.rect = Math::RectUVE{Math::Vector2UVE{at.x + placed.rect.size.x - 12.0F, at.y}, Math::Vector2UVE{12.0F, placed.rect.size.y}};
              entities.AddComponentUVE<Scene::UIScrollbarComponentUVE>(e, track);
          });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search widgets");
}

std::vector<Scene::EntityUVE> EditorEntityEditorUVE::WidgetScreensUVE() const {
    std::vector<Scene::EntityUVE> screens;
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        return screens;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, root)) {
        if (em.HasComponentUVE<Scene::CanvasComponentUVE>(child)) {
            screens.push_back(child);
        }
    }
    return screens;
}

Scene::EntityUVE EditorEntityEditorUVE::DesignerParentUVE() {
    const std::vector<Scene::EntityUVE> screens = WidgetScreensUVE();
    if (screens.empty()) {
        m_activeScreen = Scene::kInvalidEntityUVE;
        return m_editor.GetEntityEditorRootUVE();
    }
    if (std::find(screens.begin(), screens.end(), m_activeScreen) == screens.end()) {
        m_activeScreen = screens.front();
    }
    return m_activeScreen;
}

void EditorEntityEditorUVE::AddWidgetScreenUVE() {
    // The first time, the widget's own widgets become its Main screen; then a new, hidden screen
    // (the Graph's Show Screen brings it up) opens in the Designer.
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const auto makeScreen = [&](const std::string& name, const bool visible) {
        const Scene::EntityUVE screen = m_editor.CreateDocumentSceneObjectUVE(SceneObjectKindUVE::Canvas, root);
        if (screen == Scene::kInvalidEntityUVE) {
            return screen;
        }
        if (em.HasComponentUVE<Scene::NameComponentUVE>(screen)) {
            em.GetComponentUVE<Scene::NameComponentUVE>(screen).name = name;
        }
        if (em.HasComponentUVE<Scene::CanvasComponentUVE>(screen)) {
            em.GetComponentUVE<Scene::CanvasComponentUVE>(screen).visible = visible;
        }
        return screen;
    };
    std::vector<Scene::EntityUVE> screens = WidgetScreensUVE();
    if (screens.empty()) {
        const std::vector<Scene::EntityUVE> children = m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, root);
        const Scene::EntityUVE main = makeScreen("Main", true);
        if (main == Scene::kInvalidEntityUVE) {
            return;
        }
        for (const Scene::EntityUVE child : children) {
            const bool stage = em.HasComponentUVE<Scene::NameComponentUVE>(child) &&
                               em.GetComponentUVE<Scene::NameComponentUVE>(child).name == Scene::kWidgetStageNameUVE;
            if (child != main && !stage) {
                static_cast<void>(m_editor.ReparentDocumentEntityUVE(child, main));
            }
        }
        screens.push_back(main);
    }
    std::string name = "Screen" + std::to_string(screens.size() + 1U);
    const Scene::EntityUVE made = makeScreen(name, false);
    if (made != Scene::kInvalidEntityUVE) {
        m_activeScreen = made;
        m_editor.SelectEntityUVE(made);
    }
    m_designerSignature = 0;
    SetWidgetPageUVE(0);
}

void EditorEntityEditorUVE::ShowScreenSettingsUVE(const Scene::EntityUVE screen) {
    // Rename or delete a screen (its name is what the Graph's Show Screen calls it by).
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::string current = em.HasComponentUVE<Scene::NameComponentUVE>(screen) ? em.GetComponentUVE<Scene::NameComponentUVE>(screen).name
                                                                                    : std::string("Screen");
    auto content = prism::vbox(6.0F);
    content->add(prism::make<prism::Label>("Screen name (Show Screen uses it):"));
    auto input = prism::make<prism::TextInput>(current, "Screen name");
    content->add(input);
    m_host.AppUVE().runInWindow(m_window, [this, screen, input, content] {
        prism::showDialog(
            "Screen", content,
            {prism::DialogButton{"Rename", true,
                                 [this, screen, input] {
                                     const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
                                     const std::string name = input->text();
                                     if (!name.empty()) {
                                         m_editor.SelectEntityUVE(screen);
                                         static_cast<void>(m_editor.SetSelectedEntityNameUVE(name));
                                     }
                                     m_crumbSignature.clear();
                                 }},
             prism::DialogButton{"Delete Screen", false,
                                 [this, screen] {
                                     const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
                                     m_editor.SelectEntityUVE(screen);
                                     static_cast<void>(m_editor.DeleteSelectedEntityUVE());
                                     m_activeScreen = Scene::kInvalidEntityUVE;
                                     m_designerSignature = 0;
                                     m_crumbSignature.clear();
                                 }},
             prism::DialogButton{"Cancel", false, [] {}}},
            380.0F);
    });
}

Math::Vector2UVE EditorEntityEditorUVE::DesignResolutionUVE() const {
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (root != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::CanvasComponentUVE>(root)) {
        const Math::Vector2UVE reference = em.GetComponentUVE<Scene::CanvasComponentUVE>(root).referenceResolution;
        if (reference.x > 0.0F && reference.y > 0.0F) {
            return reference;
        }
    }
    // Auto: this device's own screen.
    Math::Vector2UVE device{1920.0F, 1080.0F};
    for (const Window::MonitorInfoUVE& monitor : m_editor.m_services->GetWindowManagerUVE().EnumerateMonitorsUVE()) {
        if (monitor.width > 0U && monitor.height > 0U) {
            device = Math::Vector2UVE{static_cast<float>(monitor.width), static_cast<float>(monitor.height)};
            if (monitor.isPrimary) {
                break;
            }
        }
    }
    return device;
}

void EditorEntityEditorUVE::SetDesignResolutionUVE(const Math::Vector2UVE size) {
    // Kept on the widget (its root Canvas), one undo step; the game fits the canvas to any screen.
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (root == Scene::kInvalidEntityUVE || !em.HasComponentUVE<Scene::CanvasComponentUVE>(root)) {
        return;
    }
    const Scene::EntityUVE previous = m_editor.GetSelectedEntityUVE();
    Scene::CanvasComponentUVE next = em.GetComponentUVE<Scene::CanvasComponentUVE>(root);
    next.referenceResolution = size;
    next.scaleWithScreen = true;
    m_editor.SelectEntityUVE(root);
    if (const Core::TypeMetadataEntryUVE* const entry =
            Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::CanvasComponentUVE)))) {
        static_cast<void>(m_editor.SetSelectedComponentValueUVE(*entry, &next));
    }
    if (previous != Scene::kInvalidEntityUVE && em.IsAliveUVE(previous)) {
        m_editor.SelectEntityUVE(previous);
    }
    m_crumbSignature.clear();
    m_designerSignature = 0;
}

void EditorEntityEditorUVE::SetWidgetPageUVE(const int page) {
    // 0 Designer, 1 Graph, 2 Stage.
    if (page == 1) {
        OpenWidgetGraphUVE();
        return;
    }
    m_stageMode = page == 2;
    if (m_stageMode) {
        EnsureWidgetStageUVE();
    }
    SetBlueprintModeUVE(false);
    if (m_designer) {
        m_designer->setVisible(!m_stageMode);
    }
    if (m_viewport) {
        m_viewport->setVisible(m_stageMode);
    }
    if (m_gizmos) {
        m_gizmos->setVisible(m_stageMode);
    }
    if (m_addButton) {
        m_addButton->setTooltip(m_stageMode ? "Add to the Stage: lights, meshes, props, cameras" : "Palette: add a widget");
    }
    m_crumbSignature.clear();
    SyncBlueprintCrumbsUVE();
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

Scene::EntityUVE EditorEntityEditorUVE::FindWidgetStageUVE() const {
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (root == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, root)) {
        if (em.HasComponentUVE<Scene::NameComponentUVE>(child) &&
            em.GetComponentUVE<Scene::NameComponentUVE>(child).name == Scene::kWidgetStageNameUVE) {
            return child;
        }
    }
    return Scene::kInvalidEntityUVE;
}

void EditorEntityEditorUVE::EnsureWidgetStageUVE() {
    // A widget's first Stage: a floor, a key and a rim light, the camera a 3D View looks
    // through and the spot its asset stands on - a lit set, ready to dress.
    if (FindWidgetStageUVE() != Scene::kInvalidEntityUVE) {
        return;
    }
    const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
    if (root == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& graph = m_editor.m_services->GetSceneGraphUVE();
    const auto make = [&](const SceneObjectKindUVE kind, const std::string_view name, const Scene::EntityUVE parent,
                          const Math::Vector3UVE position, const Math::Vector3UVE eulerDegrees, const Math::Vector3UVE scale) {
        const Scene::EntityUVE entity = m_editor.CreateDocumentSceneObjectUVE(kind, parent);
        if (entity == Scene::kInvalidEntityUVE) {
            return entity;
        }
        if (em.HasComponentUVE<Scene::NameComponentUVE>(entity)) {
            em.GetComponentUVE<Scene::NameComponentUVE>(entity).name = std::string(name);
        }
        if (em.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
            Scene::TransformComponentUVE transform = em.GetComponentUVE<Scene::TransformComponentUVE>(entity);
            const float k = Math::kPiUVE / 180.0F;
            transform.localPosition = position;
            transform.localEulerRadians = Math::Vector3UVE{eulerDegrees.x * k, eulerDegrees.y * k, eulerDegrees.z * k};
            static_cast<void>(Math::TryMakeEulerUVE(transform.localEulerRadians, transform.localRotation));
            transform.localScale = scale;
            graph.SetLocalTransformUVE(em, entity, transform);
        }
        return entity;
    };
    const Math::Vector3UVE one{1.0F, 1.0F, 1.0F};
    const Scene::EntityUVE stage = make(SceneObjectKindUVE::Object3D, Scene::kWidgetStageNameUVE, root, {}, {}, one);
    if (stage == Scene::kInvalidEntityUVE) {
        return;
    }
    const Scene::EntityUVE floor = make(SceneObjectKindUVE::PlaneMesh3D, "Floor", stage, {}, {}, Math::Vector3UVE{8.0F, 1.0F, 8.0F});
    if (floor != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(floor)) {
        em.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(floor).baseColor = Math::Vector3UVE{0.34F, 0.36F, 0.42F};
    }
    const auto light = [&](const std::string_view name, const Math::Vector3UVE position, const Math::ColorUVE color, const float intensity) {
        const Scene::EntityUVE entity = make(SceneObjectKindUVE::Light3D, name, stage, position, {}, one);
        if (entity != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::LightComponentUVE>(entity)) {
            Scene::LightComponentUVE& l = em.GetComponentUVE<Scene::LightComponentUVE>(entity);
            l.type = Scene::LightTypeUVE::Point;
            l.color = color;
            l.intensity = intensity;
            l.range = 14.0F;
        }
    };
    light("KeyLight", Math::Vector3UVE{2.2F, 3.2F, 2.6F}, Math::ColorUVE{1.0F, 0.95F, 0.88F}, 14.0F);
    light("RimLight", Math::Vector3UVE{-2.6F, 2.4F, -2.2F}, Math::ColorUVE{0.55F, 0.7F, 1.0F}, 9.0F);
    light("FillLight", Math::Vector3UVE{-2.4F, 1.6F, 3.0F}, Math::ColorUVE{0.9F, 0.92F, 1.0F}, 5.0F);
    const Scene::EntityUVE camera =
        make(SceneObjectKindUVE::Camera3D, Scene::kWidgetStageCameraNameUVE, stage, Math::Vector3UVE{0.0F, 1.05F, 4.8F},
             Math::Vector3UVE{-2.0F, 0.0F, 0.0F}, one);
    if (camera != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::CameraComponentUVE>(camera)) {
        Scene::CameraComponentUVE& lens = em.GetComponentUVE<Scene::CameraComponentUVE>(camera);
        lens.fieldOfViewDegrees = 32.0F;
        lens.current = false;
    }
    make(SceneObjectKindUVE::Marker3D, Scene::kWidgetModelSpotNameUVE, stage, {}, {}, one);
    m_editor.SelectEntityUVE(stage);
}

void EditorEntityEditorUVE::ShowStageAddMenuUVE() {
    // What a Stage is dressed with: everything an entity is built from, under the Stage.
    const Scene::EntityUVE stage = FindWidgetStageUVE();
    std::vector<prism::MenuItem> items = BuildAddObjectMenuUVE(
        AddObjectContextUVE::Entity, m_icons, [this, stage](const SceneObjectKindUVE kind) {
            Scene::EntityUVE parent = stage;
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            // Into the selected Stage object when it is one, else onto the Stage.
            for (Scene::EntityUVE cursor = selected; cursor != Scene::kInvalidEntityUVE;) {
                if (cursor == stage) {
                    parent = selected;
                    break;
                }
                Scene::EntityUVE up = Scene::kInvalidEntityUVE;
                if (!m_editor.TryGetDocumentParentUVE(cursor, up) || up == cursor) {
                    break;
                }
                cursor = up;
            }
            static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind, parent));
        });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search stage objects");
}

void EditorEntityEditorUVE::ShowAddMenuUVE() {
    // What an entity is built from (no level environment, sky or streaming), grouped and searchable.
    std::vector<prism::MenuItem> items = BuildAddObjectMenuUVE(
        AddObjectContextUVE::Entity, m_icons,
        [this](const SceneObjectKindUVE kind) { static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind)); });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search components");
}

void EditorEntityEditorUVE::CompileUVE() {
    if (m_blueprintPanel.IsOpenUVE()) {
        m_blueprintPanel.CompileUVE();   // compiles the entity too, and marks the Blueprint's nodes
    } else {
        static_cast<void>(m_editor.CompileEntityEditorUVE());
    }
    m_problemsShown = ~std::size_t{0};
}

void EditorEntityEditorUVE::ShowResultsUVE(const int tab) {
    if (tab >= 0 && tab < 3) {
        ShowDockPanelUVE(kCompilePageUVE + tab, true);
    }
}

void EditorEntityEditorUVE::RunFindUVE(const std::string& query) {
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

void EditorEntityEditorUVE::RunDiffUVE() {
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

void EditorEntityEditorUVE::ShowClassSettingsUVE(const prism::Rect anchor) {
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

Scene::EntityUVE EditorEntityEditorUVE::EntityForTreeIdUVE(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntityUVE;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

void EditorEntityEditorUVE::RefreshComponentsUVE() {
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
        if (const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(Scene::ResolveSceneObjectKindUVE(em, e), 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(m_editor.GetObjectTypeNameUVE(e));
        if (m_widgetMode) {
            // A widget reads as what it is (Combo Box, Vertical Box ...), with the widget's own icon.
            if (const std::optional<WidgetKindUVE> kind = ClassifyWidgetUVE(em, e)) {
                const EditorIconsUVE::ImageUVE icon = WidgetIconUVE(*kind, 16);
                node->image = icon.pixels;
                node->imageSize = icon.size;
                node->badge = e == m_editor.GetEntityEditorRootUVE() ? std::string("Widget") : std::string(WidgetKindLabelUVE(*kind));
            }
        }
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

void EditorEntityEditorUVE::RefreshProblemsUVE() {
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

void EditorEntityEditorUVE::RefreshToolbarUVE() {
    const bool unsaved = m_editor.HasEntityEditorUnsavedChangesUVE();
    if (m_tab) {
        m_tab->setName(m_openPath.stem().string());
        m_tab->setDirty(unsaved);
        const Scene::EntityUVE root = m_editor.GetEntityEditorRootUVE();
        if (root != Scene::kInvalidEntityUVE) {
            const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(
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
        const bool running = simulation != SimulationUVE::Stopped;
        m_playButton->setChecked(running);
        if (m_playButton->text() != (running ? "Stop" : "Simulate")) {
            m_playButton->setText(running ? "Stop" : "Simulate");
            m_playButton->setIcon(running ? prism::Icon::Stop : prism::Icon::Play);
        }
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

void EditorEntityEditorUVE::RefreshUVE() {
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
        m_blueprintPanel.RefreshUVE();
        if (!m_editor.m_pendingBlueprintOpenUVE.empty()) {
            // Opened from the Content Browser: the object that runs it.
            const std::string pending = std::exchange(m_editor.m_pendingBlueprintOpenUVE, std::string{});
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            for (const Scene::EntityUVE object : m_editor.CollectEntityEditorObjectsUVE()) {
                if (em.HasComponentUVE<Scene::ScriptComponentUVE>(object) &&
                    em.GetComponentUVE<Scene::ScriptComponentUVE>(object).scriptAssetPath == pending) {
                    OpenBlueprintUVE(pending, object);
                    break;
                }
            }
        }
        SyncBlueprintTabUVE();
        if (m_widgetMode) {
            RefreshDesignerUVE();
        }
        m_dockPanels.RefreshUVE(m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kConsolePageUVE, false);
    });
}

// ---------------------------------------------------------------------------------------- render

void EditorEntityEditorUVE::RenderUVE() {
    if (m_window == 0 || !m_editor.IsEntityDocumentActiveUVE()) {
        return;
    }
    std::vector<EditorHostUVE::ViewImageUVE> views;
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
                EditorHostUVE::ViewImageUVE view{};
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
    if (m_widgetMode && m_designer && m_designer->visible() && !m_blueprintMode) {
        const Scene::EntityUVE live = m_designer->LivePreviewUVE();
        const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        const prism::Rect rect = m_designer->ViewRectOfUVE(live);
        if (live != Scene::kInvalidEntityUVE && rect.w > 0.0F && em.IsAliveUVE(live) &&
            em.HasComponentUVE<Scene::UIView3DComponentUVE>(live)) {
            const Scene::UIView3DComponentUVE& component = em.GetComponentUVE<Scene::UIView3DComponentUVE>(live);
            EditorUVE::WidgetPreviewRequestUVE request;
            request.assetPath = component.assetPath;
            request.yawDegrees = component.yawDegrees;
            request.spinDegreesPerSecond = component.spinDegreesPerSecond;
            request.lookAtHeight = component.lookAtHeight;
            request.cameraDistance = component.cameraDistance;
            request.pitchDegrees = component.pitchDegrees;
            if (component.useWidgetStage) {
                // The Stage as it is now, snapshotted a few times a second (not every frame).
                const auto now = std::chrono::steady_clock::now();
                if (now - m_stageSnapshotTime > std::chrono::milliseconds(250)) {
                    m_stageSnapshotTime = now;
                    m_stageSnapshot.clear();
                    const Scene::EntityUVE stage = FindWidgetStageUVE();
                    if (stage != Scene::kInvalidEntityUVE) {
                        if (const std::optional<Scene::SceneSnapshotUVE> snapshot = m_editor.m_services->GetSceneSerializerUVE().CaptureUVE(
                                m_editor.m_services->GetEntityManagerUVE(), {stage}, Scene::SceneAssetTypeUVE::Prefab)) {
                            m_stageSnapshot = snapshot->bytes;
                        }
                    }
                }
                request.stage = m_stageSnapshot;
            }
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderWidgetPreviewUVE(request, Math::Vector2UVE{rect.w, rect.h}, used);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorHostUVE::ViewImageUVE view{};
                view.rect = rect;
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
            }
        }
    }
    EditorShellUVE::AppendCameraScreenUVE(m_editor, m_details, m_cameraScreen.get(), views);
    m_host.RenderWindowUVE(m_window, views);
}

bool EditorEntityEditorUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    return m_window != 0 && m_host.SaveWindowScreenshotUVE(m_window, path);
}

} // namespace UVE::Editor
