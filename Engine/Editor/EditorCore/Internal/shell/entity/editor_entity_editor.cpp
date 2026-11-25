// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_entity_editor.h"
#include "editor_object_menu.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_touch_controls.h"
#include "editor_host.h"
#include "editor_shell.h"
#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "nav_seeker_3d.h"
#include "particle_emitter_component.h"
#include "surface_instance_component.h"
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
#include "ui_text.h"
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
#include "gameplay_tags.h"
#include "gameplay_attributes.h"
#include "audio_source_component.h"
#include "rigid_3d_component.h"
#include "character_controller_component.h"
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

/// A widget's rect in design pixels (the box a Fill Parent text is laid out in), if it has one.
[[nodiscard]] std::optional<Math::Rect> WidgetRectOf(const Scene::IEntityManager& em, const Scene::Entity entity) {
    if (em.HasComponent<Scene::UIButtonComponent>(entity)) { return em.GetComponent<Scene::UIButtonComponent>(entity).rect; }
    if (em.HasComponent<Scene::UIImageComponent>(entity)) { return em.GetComponent<Scene::UIImageComponent>(entity).rect; }
    if (em.HasComponent<Scene::UISliderComponent>(entity)) { return em.GetComponent<Scene::UISliderComponent>(entity).rect; }
    if (em.HasComponent<Scene::UICheckboxComponent>(entity)) { return em.GetComponent<Scene::UICheckboxComponent>(entity).rect; }
    if (em.HasComponent<Scene::UIProgressBarComponent>(entity)) { return em.GetComponent<Scene::UIProgressBarComponent>(entity).rect; }
    if (em.HasComponent<Scene::UIView3DComponent>(entity)) { return em.GetComponent<Scene::UIView3DComponent>(entity).rect; }
    return std::nullopt;
}

using SceneObjectKind = Scene::Objects::SceneObjectKind;


constexpr int kWindowWidth = 1240;
constexpr int kWindowHeight = 760;

void Hash(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void Hash(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        Hash(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] prism::Slot Centre() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

} // namespace

namespace {

/// The entity window's root: Ctrl+Space anywhere in it toggles the Content Drawer.
class ContentKeys final : public prism::Border {
public:
    std::function<void()> onToggle;
    prism::Reply onPreviewKeyDown(const prism::KeyEvent& e) override {
        if (e.key == prism::Key::Space && (e.mods & prism::ModCtrl) != 0U && !e.repeat && onToggle) {
            onToggle();
            return prism::Reply::handled();
        }
        return prism::Reply::unhandled();
    }
};

} // namespace

EditorEntityEditor::EditorEntityEditor(Editor& editor, EditorHost& host)
    : m_editor(editor), m_host(host), m_details(editor), m_viewToolbar(editor), m_dockPanels(editor), m_sequencer(editor), m_animGraphPanel(editor), m_blueprintPanel(editor), m_assetTable(editor) {}

EditorEntityEditor::~EditorEntityEditor() {
    m_editor.m_openBlueprintProvider = nullptr;
    m_editor.m_saveOpenBlueprint = nullptr;
    m_editor.m_openBlueprintDirty = nullptr;
    m_editor.m_onContentDeleted = nullptr;
    if (m_window != 0 && m_host.IsReady()) {
        m_host.CloseWindow(m_window);
    }
}

// ------------------------------------------------------------------------------------- lifetime

void EditorEntityEditor::Sync() {
    if (!m_host.IsReady()) {
        return;
    }
    const bool open = m_editor.IsEntityEditorOpen();
    const std::filesystem::path path = open ? m_editor.GetEntityEditorAssetPath() : std::filesystem::path{};
    // Closed, or another entity opened in its place: this window's tree described the old one.
    if (m_window != 0 && (!open || path != m_openPath)) {
        m_editor.EndWidgetPreview();
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_openPath.clear();
    }
    if (open && m_window == 0) {
        m_treeEntities.clear();
        m_componentsSignature = 0;
        m_problemsShown = ~std::size_t{0};
        m_closeAsked = false;
        m_widgetMode = Asset::IsWidgetBlueprintPath(path.generic_string());
        m_designerSignature = 0;
        m_window = m_host.OpenWindow(Build(), (m_widgetMode ? "Widget Blueprint - " : "Entity Editor - ") + path.filename().string(),
                                        kWindowWidth, kWindowHeight);
        if (m_window == 0) {
            // No window: the entity cannot be edited here, so it is not left open unseen.
            static_cast<void>(m_editor.CloseEntityEditor(false));
            return;
        }
        m_openPath = path;
        // Its input runs as the entity's: the panels, shortcuts and the view act on its document.
        m_host.SetWindowScope(m_window, [this](const std::function<void()>& run) {
            const Editor::EntityDocumentScope scope(m_editor);
            run();
        });
        m_host.FocusWindow(m_window);
    }
    if (m_window == 0) {
        return;
    }
    if (m_editor.m_entityEditSession.has_value() && m_editor.m_entityEditSession->focusPending) {
        m_editor.m_entityEditSession->focusPending = false;
        m_host.FocusWindow(m_window);
    }
    if (m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        RequestClose();
    }
}

void EditorEntityEditor::RequestClose() {
    if (m_window == 0 || m_closeAsked) {
        return;
    }
    if (!m_editor.HasEntityEditorUnsavedChanges()) {
        static_cast<void>(m_editor.CloseEntityEditor(false));
        return;
    }
    m_closeAsked = true;
    const std::string name = m_openPath.filename().string();
    m_host.App().runInWindow(m_window, [this, name] {
        auto text = prism::make<prism::Label>("Save the changes to " + name + " before closing?");
        prism::showDialog(
            "Close Entity", text,
            {prism::DialogButton{"Save", true,
                                 [this] {
                                     m_closeAsked = false;
                                     static_cast<void>(m_editor.CloseEntityEditor(true));
                                 }},
             prism::DialogButton{"Don't Save", false,
                                 [this] {
                                     m_closeAsked = false;
                                     static_cast<void>(m_editor.CloseEntityEditor(false));
                                 }},
             prism::DialogButton{"Cancel", false, [this] { m_closeAsked = false; }}},
            420.0F);
    });
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorEntityEditor::Build() {
    auto window = prism::vbox();
    window->add(BuildTitleBar());
    // Beside the gizmo settings: Blueprint - pick one of the entity's objects, open or make its class.
    m_blueprintButton = prism::make<prism::Button>("Blueprint", prism::Icon::BlueprintClass, prism::ButtonStyle::Toolbar);
    m_blueprintButton->setTooltip("Blueprint Class: open or create the Blueprint of one of this entity's objects");
    m_blueprintButton->onClick = [this] { ShowBlueprintMenu(); };
    // Simulate beside it: one toggle (Simulate / Stop) in place of play, pause and stop.
    m_playButton = prism::make<prism::Button>("Simulate", prism::Icon::Play, prism::ButtonStyle::Toolbar);
    m_playButton->setTooltip("Simulate the entity (its scripts and Blueprints run here, the level is untouched); Stop puts it back");
    m_playButton->onClick = [this] {
        if (m_editor.GetEntitySimulationState() == Editor::EntitySimulationState::Stopped) {
            m_lastTick = std::chrono::steady_clock::now();
            static_cast<void>(m_editor.PlayEntitySimulation());
        } else {
            static_cast<void>(m_editor.StopEntitySimulation());
        }
    };
    auto subRowExtra = prism::hbox(4.0F);
    if (!m_widgetMode) {
        subRowExtra->add(m_blueprintButton, Centre());
        subRowExtra->add(HSeparator());
    }
    subRowExtra->add(m_playButton, Centre());
    subRowExtra->add(HSeparator());
    // Viewport | <folder> > BP_Player > BP_Camera ...: where the centre is, and the Blueprints opened.
    m_crumbRow = prism::hbox(0.0F);
    m_crumbSignature.clear();
    m_openBlueprints.clear();
    subRowExtra->add(m_crumbRow, Centre());
    if (m_widgetMode) {
        // A widget has no 3D gizmos: the sub row is Simulate and Designer | Graph.
        auto subRow = prism::hbox(4.0F);
        subRow->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
        subRow->add(subRowExtra, Centre());
        auto bar = prism::make<prism::Border>();
        bar->setBackground(prism::ThemeColor(&prism::Theme::header));
        bar->setChild(subRow);
        window->add(bar);
    } else {
        window->add(m_viewToolbar.Build(true, subRowExtra));
    }
    // Compile reads the Blueprint being edited, saved or not.
    m_editor.m_openBlueprintProvider = [this](const std::string& path) { return m_blueprintPanel.UnsavedFor(path); };
    m_editor.m_openBlueprintDirty = [this] { return m_blueprintPanel.IsOpen() && m_blueprintPanel.Dirty(); };
    m_editor.m_onContentDeleted = [this](const std::filesystem::path& absolute) { ForgetDeletedContent(absolute); };
    m_editor.m_saveOpenBlueprint = [this] {
        if (m_blueprintPanel.IsOpen() && m_blueprintPanel.Dirty()) {
            static_cast<void>(m_blueprintPanel.Save());
        }
    };
    if (m_widgetMode) {
        m_blueprintPanel.widgetsProvider = [this] {
            std::vector<std::pair<std::string, WidgetKind>> widgets;
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
                const std::optional<WidgetKind> kind = ClassifyWidget(em, object);
                const bool screen = kind == WidgetKind::Canvas && object != m_editor.GetEntityEditorRoot();
                if (kind.has_value() && (*kind != WidgetKind::Canvas || screen) && em.HasComponent<Scene::NameComponent>(object)) {
                    widgets.emplace_back(em.GetComponent<Scene::NameComponent>(object).name, *kind);
                }
            }
            return widgets;
        };
    } else {
        m_blueprintPanel.widgetsProvider = nullptr;
    }
    m_blueprintPanel.hasComponent = [this](const std::string_view needed) {
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        const Scene::Entity root = m_editor.GetEntityEditorRoot();
        if (root == Scene::kInvalidEntity || !em.IsAlive(root)) {
            return true;
        }
        if (needed == "CharacterController") { return em.HasComponent<Scene::CharacterControllerComponent>(root); }
        if (needed == "Rigid3D") { return em.HasComponent<Scene::Rigid3DComponent>(root); }
        if (needed == "AudioSource") { return em.HasComponent<Scene::AudioSourceComponent>(root); }
        if (needed == "Light") { return em.HasComponent<Scene::LightComponent>(root); }
        if (needed == "Camera") { return em.HasComponent<Scene::CameraComponent>(root); }
        if (needed == "GameplayAttributes") { return em.HasComponent<Scene::GameplayAttributesComponent>(root); }
        if (needed == "GameplayTag") { return em.HasComponent<Scene::GameplayTagComponent>(root); }
        if (needed == "ParticleEmitter") { return em.HasComponent<Scene::ParticleEmitterComponent>(root); }
        if (needed == "SurfaceInstance") { return em.HasComponent<Scene::SurfaceInstanceComponent>(root); }
        if (needed == "NavSeeker3D") { return em.HasComponent<Scene::NavSeeker3DComponent>(root); }
        if (needed == "AnimationSequencer") {
            // Its own, or one directly under it (a character's).
            if (em.HasComponent<Scene::AnimationSequencerComponent>(root)) {
                return true;
            }
            for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
                if (em.HasComponent<Scene::AnimationSequencerComponent>(object)) {
                    return true;
                }
            }
            return false;
        }
        return true;
    };
    m_blueprintPanel.animParametersProvider = [this] {
        std::vector<std::string> names;
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
            if (!em.HasComponent<Scene::AnimationGraphComponent>(object)) {
                continue;
            }
            for (const Scene::AnimationParameter& parameter : em.GetComponent<Scene::AnimationGraphComponent>(object).parameters) {
                if (std::find(names.begin(), names.end(), parameter.name) == names.end()) {
                    names.push_back(parameter.name);
                }
            }
        }
        return names;
    };

    // Left: Components over Details (a split, like the level's Outliner and Details stacked);
    // centre: the view over the dock pages; the dock bar runs along the bottom, as in the level.
    auto left = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    m_componentsPanel = BuildComponents();
    auto detailsTabs = prism::make<prism::TabView>();
    detailsTabs->addTab("Inspector", prism::Icon::Properties, Panel(m_details.Build()));
    m_detailsPanel = detailsTabs;
    m_detailsTabs = detailsTabs;
    m_graphDetailsShown = false;
    left->add(m_componentsPanel, prism::Splitter::Pane{260.0F, 120.0F, 1e9F, false});
    left->add(m_detailsPanel, prism::Splitter::Pane{400.0F, 160.0F, 1e9F, true});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(left, prism::Splitter::Pane{340.0F, 240.0F, 900.0F, false});
    m_leftColumn = left;
    m_centreStack = prism::make<prism::StackSwitcher>();
    m_centreStack->addPage(BuildCentre());
    m_centreStack->addPage(BuildBlueprintView());
    m_centreStack->setActiveIndex(0);
    m_blueprintMode = false;
    columns->add(m_centreStack, prism::Splitter::Pane{860.0F, 320.0F, 1e9F, true});
    window->add(columns, prism::Slot{1.0F});
    m_dockBar = BuildDockBar();
    window->add(m_dockBar);
    if (m_widgetMode && m_dockArea) {
        // The Designer takes the whole centre; the dock opens from its bar.
        m_dockVisible = false;
        m_dockArea->setVisible(false);
        for (const auto& tab : m_dockTabs) {
            tab->setChecked(false);
        }
    }

    auto frame = prism::make<ContentKeys>();
    frame->onToggle = [this] { ToggleContentDrawer(); };
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    // Docked last time: the Asset Table comes back as a tab.
    m_contentFrame.reset();
    m_drawerPopup.reset();
    m_contentDocked = false;
    if (m_editor.m_entityContentDocked) {
        SetContentDocked(true);
    }
    return frame;
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildTitleBar() {
    const auto command = [this](const char* id) {
        EditorCommand* const found = m_editor.FindEditorCommand(id);
        prism::MenuItem item = prism::MenuItem::Item(
            found != nullptr ? found->label : std::string(id),
            [this, commandId = std::string(id)] { static_cast<void>(m_editor.RunEditorCommand(commandId)); },
            found != nullptr ? DisplayEditorShortcut(found->shortcuts[0]) : std::string());
        item.enabled = found != nullptr && (!found->isEnabled || found->isEnabled());
        return item;
    };

    m_tab = prism::make<DocumentTab>();
    m_tab->setFallbackIcon(prism::Icon::EntityAsset);
    m_tab->setName(m_editor.GetEntityEditorAssetPath().stem().string());
    m_tab->onClick = [this] {
        const Scene::Entity root = m_editor.GetEntityEditorRoot();
        if (root != Scene::kInvalidEntity) {
            m_editor.SelectEntity(root);
        }
    };

    EditorHeaderDesc desc;
    desc.documentTab = m_tab;
    desc.toolbarRow = BuildToolbarRow();
    desc.onClose = [this] { RequestClose(); };
    desc.menus.push_back({"File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Save", [this] { SaveAll(); }, "Ctrl+S", prism::Icon::Save));
        prism::MenuItem revert = prism::MenuItem::Item("Revert", [this] { static_cast<void>(m_editor.RevertEntityEditor()); });
        revert.enabled = m_editor.HasEntityEditorUnsavedChanges();
        items.push_back(std::move(revert));
        items.push_back(prism::MenuItem::Item("Show in Asset Table", [this] {
            if (onBrowse) {
                onBrowse(m_openPath);
            }
        }, "", prism::Icon::FolderOpen));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { RequestClose(); }, "", prism::Icon::Close));
        return items;
    }});
    desc.menus.push_back({"Edit", [command] {
        return std::vector<prism::MenuItem>{command("edit.undo"), command("edit.redo"), prism::MenuItem::Sep(),
                                            command("edit.cut"), command("edit.copy"), command("edit.paste"),
                                            command("edit.duplicate"), command("edit.delete")};
    }});
    desc.menus.push_back({"Window", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Components", m_componentsPanel && m_componentsPanel->visible(), [this] {
            if (m_componentsPanel) {
                m_componentsPanel->setVisible(!m_componentsPanel->visible());
            }
        }));
        items.push_back(prism::MenuItem::Check("Inspector", m_detailsPanel && m_detailsPanel->visible(), [this] {
            if (m_detailsPanel) {
                m_detailsPanel->setVisible(!m_detailsPanel->visible());
            }
        }));
        return items;
    }});
    desc.menus.push_back({"Tools", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Find in Entity", [this] {
            if (m_findInput) {
                prism::Application::get().setFocus(m_findInput.get());
            }
        }, "", prism::Icon::Search));
        items.push_back(prism::MenuItem::Item("Diff Against Saved", [this] { RunDiff(); }, "", prism::Icon::DiffView));
        items.push_back(prism::MenuItem::Item("Class Settings", [this] {
            ShowClassSettings(prism::Rect{360.0F, 64.0F, 0.0F, 0.0F});
        }, "", prism::Icon::Settings));
        return items;
    }});
    desc.menus.push_back({"Build", [command] { return std::vector<prism::MenuItem>{command("entity.compile")}; }});
    desc.menus.push_back({"Help", [] {
        return std::vector<prism::MenuItem>{prism::MenuItem::Item("UVE Editor Reference", {}).disabled()};
    }});
    return BuildEditorHeader(std::move(desc));
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildToolbarRow() {
    // Sub row 1: compile and its result, save, revert | browse | status.
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(5.0F, 1.0F, 5.0F, 1.0F));

    auto compile = prism::make<prism::Button>("Compile", prism::Icon::Compile, prism::ButtonStyle::Toolbar);
    compile->setTooltip("Check the whole entity: one root, and every object's script against its object (F7)");
    compile->onClick = [this] { Compile(); };
    row->add(compile, Centre());
    m_compileState = prism::make<prism::Label>("Not compiled", prism::FontRole::Small);
    m_compileState->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_compileState, prism::Slot{0.0F, prism::Margin(2.0F, 0.0F, 6.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparator());

    auto save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Toolbar);
    save->setTooltip("Save the entity (Ctrl+S)");
    save->onClick = [this] { SaveAll(); };
    row->add(save, Centre());
    m_revertButton = prism::make<prism::Button>("Revert", prism::Icon::Undo, prism::ButtonStyle::Toolbar);
    m_revertButton->setTooltip("Throw away the unsaved changes and read the file again");
    m_revertButton->onClick = [this] { static_cast<void>(m_editor.RevertEntityEditor()); };
    row->add(m_revertButton, Centre());
    row->add(HSeparator());

    auto browse = ToolButton(prism::Icon::FolderOpen, "Show the entity in the Asset Table", "Browse");
    browse->onClick = [this] {
        if (onBrowse) {
            onBrowse(m_openPath);
        }
    };
    row->add(browse, Centre());

    // Find: every place the text occurs in the entity, listed under the view (Enter).
    m_findInput = prism::make<prism::TextInput>("", "Find in entity");
    m_findInput->setTooltip("Object names and types, components, property names and values (Enter lists them)");
    m_findInput->onCommit = [this](const std::string& text) { RunFind(text); };
    auto findBox = prism::make<prism::SizeBox>();
    findBox->setWidthOverride(220.0F);
    findBox->setContent(m_findInput);
    row->add(findBox, Centre());
    auto diff = prism::make<prism::Button>("Diff", prism::Icon::DiffView, prism::ButtonStyle::Toolbar);
    diff->setTooltip("What changed since the entity was last saved");
    diff->onClick = [this] { RunDiff(); };
    row->add(diff, Centre());
    row->add(HSeparator());

    auto classSettings = prism::make<prism::Button>("Class Settings", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    classSettings->setTooltip("The entity's own settings");
    std::weak_ptr<prism::Button> weakSettings = classSettings;
    classSettings->onClick = [this, weakSettings] {
        if (auto anchor = weakSettings.lock()) {
            ShowClassSettings(anchor->rect());
        }
    };
    row->add(classSettings, Centre());
    if (prism::Ref<prism::Widget> controls = MakeTouchControlsButton()) {   // touch screens: Undo, Ctrl, ...
        row->add(controls, Centre());
    }
    row->add(HSeparator());

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

prism::Ref<prism::Widget> EditorEntityEditor::BuildComponents() {
    // The entity's objects, as the level's Outliner shows the level's: '+', search, the tree.
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));
    auto bar = prism::hbox(4.0F);
    m_addButton = ToolButton(prism::Icon::Plus, m_widgetMode ? "Palette: add a widget" : "Add an object to the entity");
    m_addButton->onClick = [this] {
        if (m_widgetMode && m_stageMode) {
            ShowStageAddMenu();
        } else if (m_widgetMode) {
            ShowWidgetPalette();
        } else {
            ShowAddMenu();
        }
    };
    m_componentsSearch = prism::make<prism::TextInput>("", "Search");
    m_componentsSearch->onChanged = [this](const std::string& text) {
        if (m_components) {
            m_components->setFilter(text);
        }
    };
    bar->add(m_addButton, Centre());
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
            const Scene::Entity entity = EntityForTreeId(node->id);
            if (entity == Scene::kInvalidEntity) {
                continue;
            }
            if (first) {
                m_editor.SelectEntity(entity);
                first = false;
            } else if (!m_editor.IsEntitySelected(entity)) {
                m_editor.ToggleEntitySelection(entity);
            }
        }
    };
    m_components->onVisibilityToggle = [this](prism::TreeNode* node) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (entity != Scene::kInvalidEntity) {
            static_cast<void>(m_editor.SetEntityVisible(entity, node->visible));
        }
    };
    m_components->onRename = [this](prism::TreeNode* node, const std::string& name) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (entity != Scene::kInvalidEntity) {
            m_editor.SelectEntity(entity);
            static_cast<void>(m_editor.SetSelectedEntityName(name));
        }
    };
    m_components->onDelete = [this](prism::TreeNode* node) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (m_editor.IsEntitySelected(entity)) {
            m_editor.RequestDeleteSelectedEntities();
        } else if (entity != Scene::kInvalidEntity && entity != m_editor.GetEntityEditorRoot()) {
            m_editor.SelectEntity(entity);
            m_editor.RequestDeleteSelectedEntities();
        }
    };
    // Drag a row onto another: inside it (a child), or before / after it (a sibling).
    m_components->canDropNode = [this](prism::TreeNode* dragged, prism::TreeNode* target, const prism::TreeView::DropWhere where) {
        const Scene::Entity entity = EntityForTreeId(dragged->id);
        const Scene::Entity onto = EntityForTreeId(target->id);
        const Scene::Entity root = m_editor.GetEntityEditorRoot();
        if (entity == Scene::kInvalidEntity || onto == Scene::kInvalidEntity || entity == root) {
            return false;
        }
        // Nothing goes beside the root: everything lives under it.
        return !(onto == root && where != prism::TreeView::DropWhere::Inside);
    };
    m_components->onDropNode = [this](prism::TreeNode* dragged, prism::TreeNode* target, const prism::TreeView::DropWhere where) {
        const Scene::Entity entity = EntityForTreeId(dragged->id);
        const Scene::Entity onto = EntityForTreeId(target->id);
        const int place = where == prism::TreeView::DropWhere::Before ? 0 : where == prism::TreeView::DropWhere::Inside ? 1 : 2;
        if (m_editor.PlaceDocumentEntity(entity, onto, place)) {
            m_editor.SelectEntity(entity);
        }
    };
    m_components->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        const Scene::Entity entity = node != nullptr ? EntityForTreeId(node->id) : Scene::kInvalidEntity;
        EditorObjectMenu::Hooks hooks;
        hooks.tree = m_components.get();
        // A widget's children come from the widget palette, not the 3D Add menu.
        hooks.addChild = [this] {
            if (m_widgetMode) {
                ShowWidgetPalette();
            } else {
                ShowAddMenu();
            }
        };
        EditorObjectMenu::Show(m_editor, entity, node, at, hooks);
    };
    page->add(m_components, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab(m_widgetMode ? "Hierarchy" : "Components", prism::Icon::Components, Panel(page));
    return tabs;
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildCentre() {
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};
    m_gizmos = prism::make<EditorViewportGizmos>(m_editor);
    m_cameraScreen = prism::make<CameraScreen>();
    m_cameraScreen->setVisible(false);
    auto view = prism::make<prism::Overlay>();
    if (m_widgetMode) {
        // A widget is designed flat, at its screen resolution: the Designer, not a 3D view.
        m_viewport->setVisible(false);
        m_designer = prism::make<WidgetDesignerView>();
        m_designer->onPicked = [this](const Scene::Entity entity) {
            if (entity != Scene::kInvalidEntity) {
                m_editor.SelectEntity(entity);
            }
        };
        m_designer->onMoved = [this](const Scene::Entity entity, const float dx, const float dy, const bool finished) {
            MoveWidget(entity, dx, dy, finished);
        };
        m_designer->onResized = [this](const Scene::Entity entity, const prism::Rect rect, const bool finished) {
            ResizeWidget(entity, rect, finished);
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
        const std::vector<Editor::EntityCompileProblem>& problems = m_editor.GetEntityEditorProblems();
        if (row < 0 || static_cast<std::size_t>(row) >= problems.size()) {
            return {};
        }
        const Editor::EntityCompileProblem& p = problems[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return p.objectName.empty() ? std::string("Entity") : p.objectName;
            case 1: return p.at.line > 0U ? std::to_string(p.at.line) : std::string();
            default: return p.message;
        }
    };
    m_problems->onSelect = [this](const std::vector<int>& rows) {
        const std::vector<Editor::EntityCompileProblem>& problems = m_editor.GetEntityEditorProblems();
        if (rows.empty() || rows.front() < 0 || static_cast<std::size_t>(rows.front()) >= problems.size()) {
            return;
        }
        const Editor::EntityCompileProblem& problem = problems[static_cast<std::size_t>(rows.front())];
        if (problem.entity != Scene::kInvalidEntity) {
            m_editor.SelectEntity(problem.entity);
        }
        // A Blueprint's problem: its graph, with the node picked.
        if (problem.scriptPath.ends_with(".uvbp")) {
            OpenBlueprint(problem.scriptPath, problem.entity);
            m_blueprintPanel.SelectNode(problem.blueprintNode);
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
        const Editor::EntityFindRow& r = m_findRows[static_cast<std::size_t>(row)];
        return col == 0 ? r.object : col == 1 ? r.where : r.text;
    };
    m_findTable->onSelect = [this](const std::vector<int>& rows) {
        if (!rows.empty() && rows.front() >= 0 && static_cast<std::size_t>(rows.front()) < m_findRows.size()) {
            const Scene::Entity entity = m_findRows[static_cast<std::size_t>(rows.front())].entity;
            if (m_editor.m_services->GetEntityManager().IsAlive(entity)) {
                m_editor.SelectEntity(entity);
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
        const EntityDiffRow& r = m_diffRows[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return r.kind == EntityDiffRow::Kind::Added   ? "Added"
                           : r.kind == EntityDiffRow::Kind::Removed ? "Removed"
                                                                          : "Changed";
            case 1: return r.object;
            case 2: return r.what.empty() ? std::string("(object)") : r.what;
            case 3: return r.before;
            default: return r.after;
        }
    };

    // The dock pages (the level's layout): one page per tab of the bar along the window's bottom.
    m_dockPanels.AttachLog();
    m_dockPages = prism::make<prism::StackSwitcher>();
    m_dockPages->addPage(BuildSequencerPage());
    m_dockPages->addPage(BuildGraphPage());
    m_dockPages->addPage(Panel(m_dockPanels.BuildOutputLog()));
    m_dockPages->addPage(Panel(m_dockPanels.BuildConsole()));
    m_dockPages->addPage(Panel(m_problems));
    m_dockPages->addPage(Panel(m_findTable));
    m_dockPages->addPage(Panel(m_diffTable));
    m_contentDockHolder = prism::make<prism::Border>();
    m_dockPages->addPage(m_contentDockHolder);
    m_dockPages->setActiveIndex(kConsolePage);
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

prism::Ref<prism::Widget> EditorEntityEditor::BuildDockBar() {
    // The level's dock bar: each tab shows its page, the active one hides it.
    auto bar = prism::hbox(2.0F);
    bar->setPadding(prism::Margin(4.0F, 1.0F, 8.0F, 1.0F));
    struct DockTab { const char* label; prism::Icon icon; };
    static constexpr std::array<DockTab, kDockPageCount> kTabs{{
        {"Timeline", prism::Icon::AnimSequencer},
        {"Animation Graph", prism::Icon::AnimGraph},
        {"Output Log", prism::Icon::OutputLog},
        {"Console", prism::Icon::Terminal},
        {"Compile Results", prism::Icon::CompileResults},
        {"Find Results", prism::Icon::FindResults},
        {"Diff", prism::Icon::DiffView},
        {"Asset Table", prism::Icon::AssetBrowser},
    }};
    // The Content Drawer first, as the level's bottom bar has it.
    m_drawerButton = prism::make<prism::Button>("Content Drawer", prism::Icon::AssetBrowser, prism::ButtonStyle::Toolbar);
    m_drawerButton->setTooltip("The project's content, sliding up over the bottom (Ctrl+Space)");
    m_drawerButton->onClick = [this] { ToggleContentDrawer(); };
    bar->add(m_drawerButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_dockTabs.clear();
    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        auto tab = prism::make<prism::Button>(kTabs[i].label, kTabs[i].icon, prism::ButtonStyle::Toolbar);
        tab->setCheckable(true);
        tab->setChecked(static_cast<int>(i) == kConsolePage);
        tab->onClick = [this, index = static_cast<int>(i)] { ShowDockPanel(index, false); };
        // The animation tabs come with their objects (SyncAnimationTabs).
        tab->setVisible(i != kSequencerPage && i != kGraphPage && (i != kContentPage || m_contentDocked));
        m_dockTabs.push_back(tab);
        bar->add(tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    bar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    // Full: the dock takes the viewport's place; Viewport brings the default layout back.
    m_fullButton = prism::make<prism::Button>("Full", prism::Icon::LayoutFull, prism::ButtonStyle::Toolbar);
    m_fullButton->setTooltip("Let the dock fill the viewport's space");
    m_fullButton->onClick = [this] { SetDockFull(true); };
    m_viewportButton = prism::make<prism::Button>("Viewport", prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
    m_viewportButton->setTooltip("Back to the default layout: the viewport over the dock");
    m_viewportButton->onClick = [this] { SetDockFull(false); };
    m_viewportButton->setVisible(false);
    bar->add(m_fullButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    bar->add(m_viewportButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    border->setChild(bar);
    return border;
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildContentFrame() {
    // Header: what it is, and where it lives (drawer or dock); under it the Asset Table.
    auto header = prism::hbox(6.0F);
    header->setPadding(prism::Margin(8.0F, 3.0F, 6.0F, 3.0F));
    auto title = prism::make<prism::Label>("Asset Table", prism::FontRole::Semibold);
    header->add(title, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    header->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_contentDockToggle = prism::make<prism::Button>(m_contentDocked ? "Undock to Drawer" : "Dock in Layout", prism::Icon::LayoutViewport,
                                                     prism::ButtonStyle::Toolbar);
    m_contentDockToggle->setTooltip("Keep it as a tab of the dock, or back to a drawer that slides up");
    m_contentDockToggle->onClick = [this] { SetContentDocked(!m_contentDocked); };
    header->add(m_contentDockToggle, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    auto headerBar = prism::make<prism::Border>();
    headerBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    headerBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    headerBar->setChild(header);
    auto column = prism::vbox();
    column->add(headerBar);
    column->add(m_assetTable.Build(), prism::Slot{1.0F});
    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::panel));
    frame->setBorder(prism::ThemeColor(&prism::Theme::borderStrong), prism::Margin(1.0F));
    frame->setChild(column);
    return frame;
}

void EditorEntityEditor::CloseContentDrawer() {
    if (m_drawerPopup && prism::Application::exists()) {
        const std::shared_ptr<prism::Widget> popup = m_drawerPopup;
        m_drawerPopup.reset();
        prism::Application::get().closePopup(popup.get());
    }
    if (m_drawerButton) {
        m_drawerButton->setChecked(false);
    }
}

void EditorEntityEditor::ToggleContentDrawer() {
    if (!prism::Application::exists()) {
        return;
    }
    if (!m_contentFrame) {
        m_contentFrame = BuildContentFrame();
    }
    if (m_contentDocked) {
        ShowDockPanel(kContentPage, false);   // docked: the drawer button shows its tab
        return;
    }
    if (m_drawerPopup) {
        CloseContentDrawer();
        return;
    }
    // Over the bottom of the window, about 40% of its height, the dock bar's width.
    const prism::Rect barRect = m_dockBar ? m_dockBar->rect() : prism::Rect{};
    const float width = std::max(480.0F, barRect.w - 8.0F);
    const float height = std::max(260.0F, barRect.y * 0.42F);
    auto box = prism::make<prism::SizeBox>();
    box->setWidthOverride(width);
    box->setHeightOverride(height);
    if (m_contentDockHolder) {
        m_contentDockHolder->setChild(prism::make<prism::Spacer>());
    }
    box->setContent(m_contentFrame);
    prism::PopupOptions options;
    options.placement = prism::PopupPlacement::AtPoint;
    options.anchor = prism::Rect{barRect.x + 4.0F, barRect.y - height - 2.0F, 0.0F, 0.0F};
    options.dismissOnOutside = true;
    options.shadow = true;
    options.onClose = [this] {
        m_drawerPopup.reset();
        if (m_drawerButton) {
            m_drawerButton->setChecked(false);
        }
    };
    m_drawerPopup = box;
    prism::Application::get().showPopup(box, options);
    if (m_drawerButton) {
        m_drawerButton->setCheckable(true);
        m_drawerButton->setChecked(true);
    }
}

void EditorEntityEditor::SetContentDocked(const bool docked) {
    if (!m_contentFrame) {
        m_contentFrame = BuildContentFrame();
    }
    CloseContentDrawer();
    m_contentDocked = docked;
    m_editor.m_entityContentDocked = docked;
    if (m_contentDockToggle) {
        m_contentDockToggle->setText(docked ? "Undock to Drawer" : "Dock in Layout");
    }
    if (m_dockTabs.size() == kDockPageCount) {
        m_dockTabs[kContentPage]->setVisible(docked);
    }
    if (docked) {
        if (m_contentDockHolder) {
            m_contentDockHolder->setChild(m_contentFrame);
        }
        ShowDockPanel(kContentPage, true);
    } else {
        if (m_contentDockHolder) {
            m_contentDockHolder->setChild(prism::make<prism::Spacer>());
        }
        if (m_dockPages && m_dockPages->activeIndex() == kContentPage) {
            ShowDockPanel(kConsolePage, true);
        }
        ToggleContentDrawer();
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildSequencerPage() {
    return Panel(m_sequencer.Build());
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildGraphPage() {
    return Panel(m_animGraphPanel.Build());
}

void EditorEntityEditor::SetCentreFocus(const bool on) {
    m_centreFocus = on;
    if (m_leftColumn) {
        m_leftColumn->setVisible(!on);
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorEntityEditor::SetDockFull(const bool full) {
    if (!m_centreSplit || !m_viewArea) {
        return;
    }
    if (full && !m_dockVisible && m_dockPages) {
        ShowDockPanel(m_dockPages->activeIndex(), true);   // nothing to fill with while hidden
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

void EditorEntityEditor::SyncGraphDetails(const bool graphShown) {
    // The graph's settings sit in the Details column while its page is up, picked when it opens.
    if (!m_detailsTabs || graphShown == m_graphDetailsShown) {
        return;
    }
    const prism::Ref<prism::Widget> side = m_animGraphPanel.Side();
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

void EditorEntityEditor::ShowDockPanel(const int index, const bool forceVisible) {
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
        SetDockFull(false);
    }
    for (std::size_t i = 0; i < m_dockTabs.size(); ++i) {
        m_dockTabs[i]->setChecked(m_dockVisible && static_cast<int>(i) == m_dockPages->activeIndex());
    }
}

void EditorEntityEditor::SyncAnimationTabs() {
    if (!m_dockPages || m_dockTabs.size() != kDockPageCount) {
        return;
    }
    // The tabs follow the objects: an AnimationSequencer / AnimationGraph in the entity brings its tab.
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    bool hasSequencer = false;
    bool hasGraph = false;
    for (const Scene::Entity entity : m_editor.CollectEntityEditorObjects()) {
        hasSequencer = hasSequencer || em.HasComponent<Scene::AnimationSequencerComponent>(entity);
        hasGraph = hasGraph || em.HasComponent<Scene::AnimationGraphComponent>(entity);
    }
    const auto sync = [this](const int index, const bool want) {
        prism::Button& tab = *m_dockTabs[static_cast<std::size_t>(index)];
        if (tab.visible() == want) {
            return;
        }
        tab.setVisible(want);
        if (want) {
            ShowDockPanel(index, true);   // a new object opens its tab
        } else if (m_dockPages->activeIndex() == index) {
            ShowDockPanel(kConsolePage, true);   // its object went: back to the Console
        }
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
    };
    sync(kSequencerPage, hasSequencer);
    sync(kGraphPage, hasGraph);
}

// ---------------------------------------------------------------------------------------- blueprint

bool EditorEntityEditor::BlueprintFileExists(const std::string& path) const {
    std::filesystem::path real = m_editor.m_services->GetFileSystem().ResolveRealPath(path);
    if (real.empty()) {
        real = path;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(real, error);
}

void EditorEntityEditor::ForgetDeletedContent(const std::filesystem::path& absolute) {
    std::error_code error;
    const std::filesystem::path gone = std::filesystem::weakly_canonical(absolute, error);
    const auto names = [this, &gone](const std::string& path) {
        if (path.empty()) {
            return false;
        }
        std::filesystem::path real = m_editor.m_services->GetFileSystem().ResolveRealPath(path);
        if (real.empty()) {
            real = path;
        }
        std::error_code ignored;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(real, ignored);
        // The file itself, or anything inside a deleted folder.
        const auto [g, c] = std::mismatch(gone.begin(), gone.end(), canonical.begin(), canonical.end());
        return g == gone.end();
    };
    // Its graph closes without saving (saving would write the deleted file back).
    if (m_blueprintPanel.IsOpen() && names(m_blueprintPanel.Path())) {
        m_blueprintPanel.Close();
    }
    std::erase_if(m_openBlueprints, [&names](const OpenBlueprintEntry& entry) { return names(entry.path); });
    if (m_blueprintMode && !m_blueprintPanel.IsOpen()) {
        if (m_openBlueprints.empty()) {
            SetBlueprintMode(false);
        } else {
            const OpenBlueprintEntry next = m_openBlueprints.back();
            OpenBlueprint(next.path, next.target);
        }
    }
    // The open entity's objects stop naming it.
    if (!m_editor.m_entityEditSession.has_value()) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    bool changed = false;
    for (const Scene::Entity entity : m_editor.CollectEntityEditorObjects()) {
        if (em.HasComponent<Scene::ScriptComponent>(entity)) {
            std::string& path = em.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
            if (names(path)) {
                path.clear();
                changed = true;
            }
        }
    }
    if (changed) {
        m_editor.m_sceneDirty = true;
        m_editor.m_contentStatusMessage = "Deleted " + absolute.filename().string() + "; the objects that ran it run nothing now.";
    }
}

void EditorEntityEditor::CreateBlueprintOn(const std::filesystem::path& file, const std::vector<std::string>& objectPath) {
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const auto nameOf = [&em](const Scene::Entity entity) {
        return em.HasComponent<Scene::NameComponent>(entity) ? em.GetComponent<Scene::NameComponent>(entity).name : std::string{};
    };
    Scene::Entity target = Scene::kInvalidEntity;
    for (const Scene::Entity entity : m_editor.CollectEntityEditorObjects()) {
        // Its names from the root down must be the path asked for.
        std::vector<std::string> path{nameOf(entity)};
        Scene::Entity cursor = entity;
        Scene::Entity up = Scene::kInvalidEntity;
        for (int depth = 0; depth < 256 && m_editor.TryGetDocumentParent(cursor, up) && up != Scene::kInvalidEntity && up != cursor;
             ++depth) {
            path.insert(path.begin(), nameOf(up));
            cursor = up;
        }
        if (path == objectPath) {
            target = entity;
            break;
        }
    }
    if (target == Scene::kInvalidEntity) {
        m_editor.m_contentStatusMessage = "Could not find the object for the new Blueprint in this entity.";
        return;
    }
    const std::string path = m_blueprintPanel.CreateFor(target, file);
    if (path.empty()) {
        m_editor.m_contentStatusMessage = "Could not create the Blueprint.";
        return;
    }
    OpenBlueprint(path, target);
    if (m_editor.SaveEntityEditor()) {
        m_editor.m_contentStatusMessage = "Made " + file.filename().string() + " and attached it to " +
                                          m_editor.GetEntityDisplayLabel(target) + ".";
    }
    m_editor.RefreshProjectFileIndex();
}

void EditorEntityEditor::ShowBlueprintMenu() {
    // Only this entity's own objects: its root, then the objects under it (a Player with nothing
    // under it lists just the Player).
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("BLUEPRINT FOR"));
    for (const Scene::Entity entity : m_editor.CollectEntityEditorObjects()) {
        std::string label = m_editor.GetEntityDisplayLabel(entity);
        std::string existing;
        if (em.HasComponent<Scene::ScriptComponent>(entity)) {
            const std::string& path = em.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath;
            // A file deleted since names nothing: the object gets a new one.
            if (path.ends_with(".uvbp") && BlueprintFileExists(path)) {
                existing = path;
            }
        }
        const std::string kind(m_editor.GetObjectTypeName(entity));
        prism::MenuItem item = prism::MenuItem::Item(
            existing.empty() ? label : label + "   (" + std::filesystem::path(existing).stem().string() + ")",
            [this, entity, existing] {
                if (existing.empty()) {
                    ShowCreateBlueprint(entity);
                } else {
                    OpenBlueprint(existing, entity);
                }
            },
            existing.empty() ? kind : std::string("Open"), ObjectKindGlyph(Scene::ResolveSceneObjectKind(em, entity)));
        if (const EditorIcons::Image icon = m_icons.Object(Scene::ResolveSceneObjectKind(em, entity), 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    }
    prism::showMenu(std::move(items), m_blueprintButton ? m_blueprintButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorEntityEditor::ShowCreateBlueprint(const Scene::Entity target) {
    // Name (BP_<Object>) and the folder it goes to: the entity's own folder first, then every folder of Content.
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    std::vector<std::filesystem::path> folders{snapshot.contentRoot};
    if (m_editor.m_entityEditSession.has_value() && !m_editor.m_entityEditSession->assetPath.empty()) {
        const std::filesystem::path own = m_editor.m_entityEditSession->assetPath.parent_path();
        if (own != snapshot.contentRoot) {
            folders.insert(folders.begin(), own);
        }
    }
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
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
    for (const char c : m_editor.GetEntityDisplayLabel(target)) {
        base.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
    }
    auto name = prism::make<prism::TextInput>(base, "Name");
    auto folder = prism::make<prism::ComboBox>(labels, 0);
    auto form = prism::make<prism::PropertyGrid>();
    const int cat = form->addCategory("New Blueprint Class");
    form->addRow(cat, "Name", name, "The file's name: the class other objects use");
    form->addRow(cat, "Folder", folder, "Where the file goes in Content");
    form->addRow(cat, "Parent", prism::make<prism::Label>(std::string(m_editor.GetObjectTypeName(target))), "The object kind it drives");
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
        const std::filesystem::path file = Editor::MakeUniqueContentPath(dir, n->text(), ".uvbp");
        const std::string path = m_blueprintPanel.CreateFor(target, file);
        if (path.empty()) {
            m_editor.m_contentStatusMessage = "Could not create the Blueprint.";
            return;
        }
        OpenBlueprint(path, target);
        // On disk at once: the entity's Script slot now names the Blueprint, so the level's copies
        // and the game run it (it used to live only in the open window until a Save).
        if (m_editor.SaveEntityEditor()) {
            m_editor.m_contentStatusMessage = "Made " + file.filename().string() + " and attached it to " +
                                              m_editor.GetEntityDisplayLabel(target) + ".";
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

void EditorEntityEditor::OpenBlueprint(const std::string& path, const Scene::Entity target) {
    if (m_blueprintPanel.IsOpen() && m_blueprintPanel.Path() != path && m_blueprintPanel.Dirty()) {
        static_cast<void>(m_blueprintPanel.Save());   // the one being left keeps its edits
    }
    // The same file for another object (the entity was reloaded, so its handle changed) reopens it,
    // unless it has unsaved edits.
    const bool reopen = m_blueprintPanel.Path() != path ||
                        (m_blueprintPanel.Target() != target && !m_blueprintPanel.Dirty());
    if (reopen && !m_blueprintPanel.Open(path, target)) {
        return;
    }
    if (std::none_of(m_openBlueprints.begin(), m_openBlueprints.end(),
                     [&path](const OpenBlueprintEntry& entry) { return entry.path == path; })) {
        m_openBlueprints.push_back(OpenBlueprintEntry{path, target});
    }
    SetBlueprintMode(true);
}

prism::Ref<prism::Widget> EditorEntityEditor::BuildBlueprintView() {
    // The graph over the whole centre; the sub row's crumbs switch between it and the viewport.
    return m_blueprintPanel.Build();
}

void EditorEntityEditor::CloseBlueprint(const std::string& path) {
    if (m_blueprintPanel.Path() == path) {
        if (m_blueprintPanel.Dirty()) {
            static_cast<void>(m_blueprintPanel.Save());   // closing keeps its edits
        }
        m_blueprintPanel.Close();
    }
    std::erase_if(m_openBlueprints, [&path](const OpenBlueprintEntry& entry) { return entry.path == path; });
    // The one left of it (or the viewport) takes its place.
    if (!m_blueprintPanel.IsOpen() && m_blueprintMode && !m_openBlueprints.empty()) {
        const OpenBlueprintEntry next = m_openBlueprints.back();
        OpenBlueprint(next.path, next.target);
        return;
    }
    SetBlueprintMode(m_blueprintMode);
}

void EditorEntityEditor::SyncBlueprintCrumbs() {
    if (!m_crumbRow) {
        return;
    }
    // A crumb whose object came back under a new handle (Simulate, Revert) follows it; the rest,
    // whose object left the entity, are forgotten.
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const std::vector<Scene::Entity> objects = m_editor.CollectEntityEditorObjects();
    for (OpenBlueprintEntry& entry : m_openBlueprints) {
        if (!em.IsAlive(entry.target)) {
            entry.target = m_blueprintPanel.FindUser(entry.path);
        }
    }
    std::erase_if(m_openBlueprints, [&](const OpenBlueprintEntry& entry) {
        return !em.IsAlive(entry.target) || std::find(objects.begin(), objects.end(), entry.target) == objects.end();
    });
    const std::string active = m_blueprintMode ? m_blueprintPanel.Path() : std::string{};
    std::string signature = active + (m_blueprintPanel.Dirty() ? "*" : "") + (m_stageMode ? "S" : "") + "|";
    if (m_widgetMode) {
        const Scene::IEntityManager& screenEm = m_editor.m_services->GetEntityManager();
        for (const Scene::Entity screen : WidgetScreens()) {
            signature += (screen == m_activeScreen ? "*" : "") +
                         (screenEm.HasComponent<Scene::NameComponent>(screen) ? screenEm.GetComponent<Scene::NameComponent>(screen).name
                                                                                 : std::string()) + ";";
        }
        signature += std::to_string(static_cast<int>(m_designer ? m_designer->Width() : 0.0F));
    }
    for (const OpenBlueprintEntry& entry : m_openBlueprints) {
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
        const std::vector<Scene::Entity> screens = WidgetScreens();
        const Scene::IEntityManager& screenEm = m_editor.m_services->GetEntityManager();
        const bool designerPage = !m_blueprintMode && !m_stageMode;
        for (const Scene::Entity screen : screens) {
            const std::string name = screenEm.HasComponent<Scene::NameComponent>(screen)
                                         ? screenEm.GetComponent<Scene::NameComponent>(screen).name
                                         : std::string("Screen");
            auto tab = prism::make<prism::Button>(name, prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            tab->setCheckable(true);
            const bool active = screen == m_activeScreen;
            tab->setChecked(designerPage && active);
            tab->setTooltip("Designer: the " + name + " screen. Click it again to rename or delete it");
            tab->onClick = [this, screen, active, designerPage] {
                if (active && designerPage) {
                    ShowScreenSettings(screen);
                    return;
                }
                m_activeScreen = screen;
                m_designerSignature = 0;
                SetWidgetPage(0);
            };
            m_crumbRow->add(tab, Centre());
        }
        if (screens.empty()) {
            auto designer = prism::make<prism::Button>("Designer", prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            designer->setCheckable(true);
            designer->setChecked(designerPage);
            designer->setTooltip("Designer: lay out the widget's text, buttons and images");
            designer->onClick = [this] { SetWidgetPage(0); };
            m_crumbRow->add(designer, Centre());
        }
        auto addScreen = prism::make<prism::Button>("", prism::Icon::Plus, prism::ButtonStyle::Toolbar);
        addScreen->setTooltip("Add a screen: another Designer (Settings, Pause, Credits ...) run by the same Graph");
        addScreen->onClick = [this] { AddWidgetScreen(); };
        m_crumbRow->add(addScreen, Centre());
        auto bar0 = prism::make<prism::Label>("|", prism::FontRole::Small);
        bar0->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_crumbRow->add(bar0, Centre());
        auto graph = prism::make<prism::Button>(std::string("Graph") + (m_blueprintPanel.Dirty() ? " *" : ""), prism::Icon::BlueprintClass,
                                                prism::ButtonStyle::Toolbar);
        graph->setCheckable(true);
        graph->setChecked(m_blueprintMode);
        graph->setTooltip("Graph: what the widget does (On Button Clicked, Set Text ...)");
        graph->onClick = [this] { OpenWidgetGraph(); };
        auto stage = prism::make<prism::Button>("Stage", prism::Icon::Cube, prism::ButtonStyle::Toolbar);
        stage->setCheckable(true);
        stage->setChecked(!m_blueprintMode && m_stageMode);
        stage->setTooltip("Stage: the 3D world a 3D View shows its asset in - floor, lights, props, StageCamera, ModelSpot");
        stage->onClick = [this] { SetWidgetPage(2); };
        m_crumbRow->add(graph, Centre());
        m_crumbRow->add(stage, Centre());
        // On a phone the Designer and the Graph are squeezed beside Components and Details: Focus
        // gives them the whole width.
        auto focus = prism::make<prism::Button>("Focus", prism::Icon::LayoutFull, prism::ButtonStyle::Toolbar);
        focus->setCheckable(true);
        focus->setChecked(m_centreFocus);
        focus->setTooltip("Focus: hide Components and Details so the Designer or Graph fills the window");
        focus->onToggle = [this](const bool on) { SetCentreFocus(on); };
        m_crumbRow->add(focus, Centre());
        if (!m_blueprintMode && !m_stageMode && m_designer) {
            // The screen the widget is designed for.
            auto bar = prism::make<prism::Label>("|", prism::FontRole::Small);
            bar->setColor(prism::ThemeColor(&prism::Theme::textDim));
            m_crumbRow->add(bar, Centre());
            m_resolutionButton = prism::make<prism::Button>(
                std::to_string(static_cast<int>(m_designer->Width())) + " x " + std::to_string(static_cast<int>(m_designer->Height())),
                prism::Icon::LayoutViewport, prism::ButtonStyle::Toolbar);
            m_resolutionButton->setTooltip("The screen size the Designer shows the widget at");
            m_resolutionButton->onClick = [this] { ShowResolutionMenu(); };
            m_crumbRow->add(m_resolutionButton, Centre());
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
    viewport->onClick = [this] { SetBlueprintMode(false); };
    m_crumbRow->add(viewport, Centre());
    if (!m_openBlueprints.empty()) {
        const auto chevron = [this] {
            auto label = prism::make<prism::Label>(">", prism::FontRole::Small);
            label->setColor(prism::ThemeColor(&prism::Theme::textDim));
            m_crumbRow->add(label, Centre());
        };
        auto bar = prism::make<prism::Label>("|", prism::FontRole::Small);
        bar->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_crumbRow->add(bar, Centre());
        // The folder of the Blueprint shown (or the first one): Content, or the folder it was made in.
        const std::string& shown = active.empty() ? m_openBlueprints.front().path : active;
        const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        const std::filesystem::path contentProject = m_blueprintPanel.ProjectPath(snapshot.contentRoot);
        const std::filesystem::path folderRelative = std::filesystem::path(shown).parent_path().lexically_relative(contentProject);
        const bool atRoot = folderRelative.empty() || folderRelative == "." || *folderRelative.begin() == "..";
        auto folder = prism::make<prism::Button>(atRoot ? std::string("Content") : folderRelative.filename().string(), prism::Icon::Folder,
                                                 prism::ButtonStyle::Toolbar);
        folder->setTooltip(atRoot ? std::string("Content: show it in the Asset Table")
                                  : "Content/" + folderRelative.generic_string() + ": show it in the Asset Table");
        folder->onClick = [this, folderRelative, atRoot] {
            m_editor.m_assetTableDirectory = atRoot ? std::filesystem::path{} : folderRelative;
            m_editor.m_contentStatusMessage = "Asset Table: " + (atRoot ? std::string("Content") : "Content/" + folderRelative.generic_string());
        };
        m_crumbRow->add(folder, Centre());
        for (const OpenBlueprintEntry& entry : m_openBlueprints) {
            chevron();
            const bool isActive = entry.path == active;
            const std::string stem = std::filesystem::path(entry.path).stem().string();
            auto crumb = prism::make<prism::Button>(stem + (isActive && m_blueprintPanel.Dirty() ? " *" : ""), prism::Icon::BlueprintClass,
                                                    prism::ButtonStyle::Toolbar);
            crumb->setCheckable(true);
            crumb->setChecked(isActive);
            crumb->setTooltip(entry.path + "  (" + m_editor.GetEntityDisplayLabel(entry.target) + ")");
            crumb->onClick = [this, entry] { OpenBlueprint(entry.path, entry.target); };
            m_crumbRow->add(crumb, Centre());
            if (isActive) {
                auto close = ToolButton(prism::Icon::Close, "Close " + stem + " (its edits are saved)");
                close->onClick = [this, path = entry.path] { CloseBlueprint(path); };
                m_crumbRow->add(close, Centre());
            }
        }
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorEntityEditor::SetBlueprintMode(const bool on) {
    const bool want = on && m_blueprintPanel.IsOpen();
    if (m_centreStack) {
        m_centreStack->setActiveIndex(want ? 1 : 0);
    }
    if (m_dockBar) {
        m_dockBar->setVisible(!want);
    }
    m_blueprintMode = want;
    SyncBlueprintTab();
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorEntityEditor::SaveAll() {
    if (m_blueprintPanel.Dirty()) {
        static_cast<void>(m_blueprintPanel.Save());
    }
    static_cast<void>(m_editor.SaveEntityEditor());
}

void EditorEntityEditor::SyncBlueprintTab() {
    // The strip's tab names the Blueprint (with * while unsaved); its Details tab is there in Blueprint mode.
    const bool open = m_blueprintPanel.IsOpen();
    if (!open && m_blueprintMode) {
        SetBlueprintMode(false);   // its object went away
        return;
    }
    SyncBlueprintCrumbs();
    const bool shown = open && m_blueprintMode;
    if (!m_detailsTabs || shown == m_blueprintDetailsShown) {
        return;
    }
    if (shown) {
        static_cast<void>(m_detailsTabs->insertTab(m_detailsTabs->count(),
                                                   prism::TabView::Page{"Blueprint", prism::Icon::BlueprintClass, m_blueprintPanel.Side(), false}));
        m_detailsTabs->setCurrent(m_detailsTabs->count() - 1);
    } else if (const int index = m_detailsTabs->indexOf("Blueprint"); index >= 0) {
        static_cast<void>(m_detailsTabs->takeTab(index));
        m_detailsTabs->setCurrent(0);
    }
    m_blueprintDetailsShown = shown;
}

// --------------------------------------------------------------------------------- widget mode

void EditorEntityEditor::OpenWidgetGraph() {
    // The graph is the widget's own, kept in its file; it runs on the root Canvas.
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        return;
    }
    const std::string path = m_blueprintPanel.ProjectPath(m_openPath);
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    // An older widget without its Script slot runs its graph once it has one (an edit like any other:
    // the widget is unsaved until it is written, and Undo takes it back).
    if (!em.HasComponent<Scene::ScriptComponent>(root) ||
        em.GetComponent<Scene::ScriptComponent>(root).scriptAssetPath != path) {
        Scene::ScriptComponent script;
        if (em.HasComponent<Scene::ScriptComponent>(root)) {
            script = em.GetComponent<Scene::ScriptComponent>(root);
        }
        script.scriptAssetPath = path;
        m_editor.SelectEntity(root);
        static_cast<void>(m_editor.SetSelectedSceneComponent(EditorSceneComponentKind::Script, script));
    }
    OpenBlueprint(path, root);
}

void EditorEntityEditor::RefreshDesigner() {
    if (!m_designer) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (m_dragWidget == Scene::kInvalidEntity) {
        // Boxes place their children as the game will, so the Designer shows what the player sees.
        static const UI::UIFontAtlas atlas;
        UI::LayoutUIContainers(em, atlas);
    }
    std::vector<WidgetDesignerView::Item> items;
    std::uint64_t signature = 1469598103934665603ULL;
    const auto toColor = [](const Math::Vector3& c, const float alpha = 1.0F) {
        const auto byte = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
        return prism::Color(byte(c.x), byte(c.y), byte(c.z), byte(alpha));
    };
    const auto fraction = [](const float value, const float lo, const float hi) { return hi > lo ? (value - lo) / (hi - lo) : 0.0F; };
    // Only the active screen's widgets (a widget with screens shows one at a time).
    const Scene::Entity screenRoot = DesignerParent();
    const bool screened = screenRoot != m_editor.GetEntityEditorRoot();
    const auto onScreen = [&](Scene::Entity entity) {
        if (!screened) {
            return true;
        }
        for (int depth = 0; depth < 256 && em.IsAlive(entity); ++depth) {
            if (entity == screenRoot) {
                return true;
            }
            if (!em.HasComponent<Scene::HierarchyComponent>(entity)) {
                return false;
            }
            entity = em.GetComponent<Scene::HierarchyComponent>(entity).parent;
        }
        return false;
    };
    Hash(signature, (static_cast<std::uint64_t>(screenRoot.generation) << 32U) | screenRoot.index);
    // The screen size: the widget's reference resolution, else this device's.
    if (const Math::Vector2 design = DesignResolution();
        design.x != m_designer->Width() || design.y != m_designer->Height()) {
        m_designer->SetResolution(design.x, design.y);
        m_crumbSignature.clear();
        m_designerSignature = 0;
    }
    for (const Scene::Entity entity : m_editor.CollectEntityEditorObjects()) {
        if (!em.IsAlive(entity) || !onScreen(entity)) {
            continue;
        }
        WidgetDesignerView::Item item;
        item.entity = entity;
        if (em.HasComponent<Scene::HierarchyComponent>(entity)) {
            item.parent = em.GetComponent<Scene::HierarchyComponent>(entity).parent;
        }
        item.name = em.HasComponent<Scene::NameComponent>(entity) ? em.GetComponent<Scene::NameComponent>(entity).name : std::string{};
        item.visible = !em.HasComponent<Scene::VisibilityComponent>(entity) || em.GetComponent<Scene::VisibilityComponent>(entity).visible;
        const auto fromRect = [](const Math::Rect& r) { return prism::Rect{r.position.x, r.position.y, r.size.x, r.size.y}; };
        const std::optional<WidgetKind> kind = ClassifyWidget(em, entity);
        if (!kind.has_value() || *kind == WidgetKind::Canvas) {
            continue;
        }
        item.kind = *kind;
        switch (*kind) {
            case WidgetKind::Button: {
                const auto& b = em.GetComponent<Scene::UIButtonComponent>(entity);
                item.rect = fromRect(b.rect);
                item.color = toColor(b.normalColor);
                break;
            }
            case WidgetKind::Text: {
                const auto& t = em.GetComponent<Scene::UITextComponent>(entity);
                item.text = t.text;
                item.fontSize = t.fontSize;
                item.color = toColor(t.color, t.alpha);
                // The box, as the runtime takes it: the parent widget's (Fill Parent), else its own.
                Math::Rect box{t.positionPixels, t.boxSize};
                if (t.fillParent && item.parent != Scene::kInvalidEntity && em.IsAlive(item.parent)) {
                    if (const std::optional<Math::Rect> parentRect = WidgetRectOf(em, item.parent)) {
                        box = *parentRect;
                    }
                }
                const std::vector<Scene::UIText::Line> lines = Scene::UIText::LayoutLines(
                    t, t.text, box, [&t](const std::string_view line) { return WidgetDesignerView::MeasureText(line, t.fontSize); });
                float left = 1e9F, right = -1e9F;
                for (const Scene::UIText::Line& line : lines) {
                    item.lines.emplace_back(prism::Vec2{line.baseline.x, line.baseline.y}, line.text);
                    left = std::min(left, line.baseline.x);
                    right = std::max(right, line.baseline.x + line.width);
                }
                item.shadow = toColor(t.shadowColor, t.alpha * t.shadowAlpha);
                item.shadowOffset = prism::Vec2{t.shadowOffset.x, t.shadowOffset.y};
                item.outline = toColor(t.outlineColor, t.outlineWidth > 0.0F ? t.alpha : 0.0F);
                item.outlineWidth = t.outlineWidth;
                if (box.size.x > 0.0F && box.size.y > 0.0F) {
                    item.rect = prism::Rect{box.position.x, box.position.y, box.size.x, box.size.y};
                } else {
                    const float lineCount = static_cast<float>(std::max<std::size_t>(1U, lines.size()));
                    item.rect = prism::Rect{std::min(left, t.positionPixels.x), t.positionPixels.y,
                                            std::max(8.0F, right - std::min(left, t.positionPixels.x)),
                                            t.fontSize * (1.0F + (lineCount - 1.0F) * t.lineHeight) + t.fontSize * 0.25F};
                }
                break;
            }
            case WidgetKind::Image:
            case WidgetKind::Border:
            case WidgetKind::Spacer: {
                const auto& i = em.GetComponent<Scene::UIImageComponent>(entity);
                item.rect = fromRect(i.rect);
                item.color = toColor(i.tintColor, *kind == WidgetKind::Spacer ? 1.0F : i.alpha);
                break;
            }
            case WidgetKind::ProgressBar: {
                const auto& b = em.GetComponent<Scene::UIProgressBarComponent>(entity);
                item.rect = fromRect(b.rect);
                item.value = fraction(b.value, b.minValue, b.maxValue);
                break;
            }
            case WidgetKind::Slider: {
                const auto& sl = em.GetComponent<Scene::UISliderComponent>(entity);
                item.rect = fromRect(sl.rect);
                item.value = fraction(sl.value, sl.minValue, sl.maxValue);
                break;
            }
            case WidgetKind::CheckBox: {
                const auto& cb = em.GetComponent<Scene::UICheckboxComponent>(entity);
                item.rect = fromRect(cb.rect);
                item.checked = cb.checked;
                break;
            }
            case WidgetKind::TextBox: {
                const auto& ti = em.GetComponent<Scene::UITextInputComponent>(entity);
                item.rect = fromRect(ti.rect);
                item.text = ti.text.empty() ? ti.placeholder : ti.text;
                item.fontSize = ti.fontSize;
                break;
            }
            case WidgetKind::ComboBox: {
                const auto& box = em.GetComponent<Scene::UIDropdownComponent>(entity);
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
            case WidgetKind::VerticalBox:
            case WidgetKind::HorizontalBox:
            case WidgetKind::WrapBox:
                item.rect = fromRect(em.GetComponent<Scene::UILayoutContainerComponent>(entity).rect);
                break;
            case WidgetKind::ScrollBox:
                item.rect = fromRect(em.GetComponent<Scene::UIScrollContainerComponent>(entity).rect);
                break;
            case WidgetKind::View3D: {
                const auto& view = em.GetComponent<Scene::UIView3DComponent>(entity);
                item.rect = fromRect(view.rect);
                item.text = view.assetPath.empty() ? std::string{} : std::filesystem::path(view.assetPath).filename().string();
                break;
            }
            case WidgetKind::Canvas:
                break;
        }
        Hash(signature, (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index);
        Hash(signature, item.text + "|" + item.name);
        for (const auto& [baseline, line] : item.lines) {
            Hash(signature, static_cast<std::uint64_t>(std::lround(baseline.x * 10.0F)) * 1000003ULL +
                                   static_cast<std::uint64_t>(std::lround(baseline.y * 10.0F)));
        }
        Hash(signature, (static_cast<std::uint64_t>(item.shadow.a) << 8U) | static_cast<std::uint64_t>(item.outline.a));
        for (const float v : {item.rect.x, item.rect.y, item.rect.w, item.rect.h, item.value, item.fontSize}) {
            Hash(signature, static_cast<std::uint64_t>(std::lround(v * 100.0F)));
        }
        Hash(signature, (static_cast<std::uint64_t>(item.color.r) << 24U) | (static_cast<std::uint64_t>(item.color.g) << 16U) |
                               (static_cast<std::uint64_t>(item.color.b) << 8U) | item.color.a);
        Hash(signature, static_cast<std::uint64_t>(item.visible) * 2U + static_cast<std::uint64_t>(item.checked));
        items.push_back(std::move(item));
    }
    if (signature != m_designerSignature && m_dragWidget == Scene::kInvalidEntity) {
        m_designerSignature = signature;
        m_designer->SetItems(std::move(items));
    }
    m_designer->SetSelected(m_editor.GetSelectedEntity());
    // One 3D View shows its asset live: the selected one, else the first with an asset.
    Scene::Entity live = Scene::kInvalidEntity;
    for (const WidgetDesignerView::Item& item : m_designer->Items()) {
        if (item.kind != WidgetKind::View3D || item.text.empty() || !item.visible) {
            continue;
        }
        if (item.entity == m_editor.GetSelectedEntity()) {
            live = item.entity;
            break;
        }
        if (live == Scene::kInvalidEntity) {
            live = item.entity;
        }
    }
    m_designer->SetLivePreview(m_blueprintMode ? Scene::kInvalidEntity : live);
}

namespace {

/// A widget's design position: a Text's pen, a control's rect corner. Nothing for anything else.
[[nodiscard]] Math::Vector2* WidgetPosition(Scene::IEntityManager& em, const Scene::Entity entity) {
    if (em.HasComponent<Scene::UIButtonComponent>(entity)) {
        return &em.GetComponent<Scene::UIButtonComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UITextComponent>(entity)) {
        return &em.GetComponent<Scene::UITextComponent>(entity).positionPixels;
    }
    if (em.HasComponent<Scene::UIImageComponent>(entity)) {
        return &em.GetComponent<Scene::UIImageComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UIProgressBarComponent>(entity)) {
        return &em.GetComponent<Scene::UIProgressBarComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UISliderComponent>(entity)) {
        return &em.GetComponent<Scene::UISliderComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UICheckboxComponent>(entity)) {
        return &em.GetComponent<Scene::UICheckboxComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UITextInputComponent>(entity)) {
        return &em.GetComponent<Scene::UITextInputComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UIDropdownComponent>(entity)) {
        return &em.GetComponent<Scene::UIDropdownComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UILayoutContainerComponent>(entity)) {
        return &em.GetComponent<Scene::UILayoutContainerComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UIScrollContainerComponent>(entity)) {
        return &em.GetComponent<Scene::UIScrollContainerComponent>(entity).rect.position;
    }
    if (em.HasComponent<Scene::UIView3DComponent>(entity)) {
        return &em.GetComponent<Scene::UIView3DComponent>(entity).rect.position;
    }
    return nullptr;
}

/// The component holding a widget's position, for its undo step.
[[nodiscard]] std::type_index WidgetComponentType(Scene::IEntityManager& em, const Scene::Entity entity) {
    if (em.HasComponent<Scene::UIView3DComponent>(entity)) {
        return std::type_index(typeid(Scene::UIView3DComponent));
    }
    if (em.HasComponent<Scene::UIButtonComponent>(entity)) {
        return std::type_index(typeid(Scene::UIButtonComponent));
    }
    if (em.HasComponent<Scene::UITextComponent>(entity)) {
        return std::type_index(typeid(Scene::UITextComponent));
    }
    if (em.HasComponent<Scene::UIImageComponent>(entity)) {
        return std::type_index(typeid(Scene::UIImageComponent));
    }
    if (em.HasComponent<Scene::UIProgressBarComponent>(entity)) {
        return std::type_index(typeid(Scene::UIProgressBarComponent));
    }
    if (em.HasComponent<Scene::UISliderComponent>(entity)) {
        return std::type_index(typeid(Scene::UISliderComponent));
    }
    if (em.HasComponent<Scene::UICheckboxComponent>(entity)) {
        return std::type_index(typeid(Scene::UICheckboxComponent));
    }
    if (em.HasComponent<Scene::UITextInputComponent>(entity)) {
        return std::type_index(typeid(Scene::UITextInputComponent));
    }
    if (em.HasComponent<Scene::UIDropdownComponent>(entity)) {
        return std::type_index(typeid(Scene::UIDropdownComponent));
    }
    if (em.HasComponent<Scene::UILayoutContainerComponent>(entity)) {
        return std::type_index(typeid(Scene::UILayoutContainerComponent));
    }
    return std::type_index(typeid(Scene::UIScrollContainerComponent));
}

template <typename Component>
bool CommitWidgetComponent(Scene::IEntityManager& em, const Scene::Entity entity, const std::type_index type,
                              const Math::Vector2 start, const std::function<void(const Core::TypeMetadataEntry&, const void*)>& set) {
    if (type != std::type_index(typeid(Component))) {
        return false;
    }
    auto& component = em.GetComponent<Component>(entity);
    Component next = component;
    // Back where it started, then set where it was let go: one step from there to here.
    if constexpr (std::is_same_v<Component, Scene::UITextComponent>) {
        component.positionPixels = start;
    } else {
        component.rect.position = start;
    }
    if (const Core::TypeMetadataEntry* const entry = Scene::FindSceneComponentMetadata(type)) {
        set(*entry, &next);
    } else {
        component = next;
    }
    return true;
}

} // namespace

void EditorEntityEditor::MoveWidget(const Scene::Entity entity, const float dx, const float dy, const bool finished) {
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (!em.IsAlive(entity)) {
        m_dragWidget = Scene::kInvalidEntity;
        m_dragStarts.clear();
        return;
    }
    if (finished) {
        if (m_dragWidget != entity) {
            return;
        }
        const auto set = [this](const Core::TypeMetadataEntry& entry, const void* value) {
            static_cast<void>(m_editor.SetSelectedComponentValue(entry, value));
        };
        for (const auto& [moved, start] : m_dragStarts) {
            if (!em.IsAlive(moved) || WidgetPosition(em, moved) == nullptr) {
                continue;
            }
            m_editor.SelectEntity(moved);
            const std::type_index type = WidgetComponentType(em, moved);
            const Math::Vector2 from{start[0], start[1]};
            static_cast<void>(CommitWidgetComponent<Scene::UIButtonComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UITextComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UIImageComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UIProgressBarComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UISliderComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UICheckboxComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UITextInputComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UIDropdownComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UILayoutContainerComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UIScrollContainerComponent>(em, moved, type, from, set) ||
                              CommitWidgetComponent<Scene::UIView3DComponent>(em, moved, type, from, set));
        }
        m_editor.SelectEntity(entity);
        m_dragWidget = Scene::kInvalidEntity;
        m_dragStarts.clear();
        m_designerSignature = 0;
        return;
    }
    if (m_dragWidget != entity) {
        // It, and every widget inside it (a Button's label), move together.
        m_dragWidget = entity;
        m_dragStarts.clear();
        for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
            bool inside = object == entity;
            for (Scene::Entity cursor = object; !inside && em.HasComponent<Scene::HierarchyComponent>(cursor);) {
                cursor = em.GetComponent<Scene::HierarchyComponent>(cursor).parent;
                if (cursor == Scene::kInvalidEntity || !em.IsAlive(cursor)) {
                    break;
                }
                inside = cursor == entity;
            }
            if (const Math::Vector2* position = inside ? WidgetPosition(em, object) : nullptr) {
                m_dragStarts.emplace_back(object, std::array<float, 2>{position->x, position->y});
            }
        }
    }
    for (const auto& [moved, start] : m_dragStarts) {
        if (Math::Vector2* position = em.IsAlive(moved) ? WidgetPosition(em, moved) : nullptr) {
            position->x += dx;
            position->y += dy;
            // A Scroll Box's scrollbar rides along its edge.
            if (em.HasComponent<Scene::UIScrollbarComponent>(moved)) {
                auto& bar = em.GetComponent<Scene::UIScrollbarComponent>(moved).rect.position;
                bar.x += dx;
                bar.y += dy;
            }
        }
    }
}

namespace {

/// A rect widget's rect (not a Text's: it has a pen, not a rect).
[[nodiscard]] const Math::Rect* WidgetRect(Scene::IEntityManager& em, const Scene::Entity e) {
    if (em.HasComponent<Scene::UIView3DComponent>(e)) {
        return &em.GetComponent<Scene::UIView3DComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UIButtonComponent>(e)) {
        return &em.GetComponent<Scene::UIButtonComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UIImageComponent>(e)) {
        return &em.GetComponent<Scene::UIImageComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UIProgressBarComponent>(e)) {
        return &em.GetComponent<Scene::UIProgressBarComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UISliderComponent>(e)) {
        return &em.GetComponent<Scene::UISliderComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UICheckboxComponent>(e)) {
        return &em.GetComponent<Scene::UICheckboxComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UITextInputComponent>(e)) {
        return &em.GetComponent<Scene::UITextInputComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UIDropdownComponent>(e)) {
        return &em.GetComponent<Scene::UIDropdownComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UILayoutContainerComponent>(e)) {
        return &em.GetComponent<Scene::UILayoutContainerComponent>(e).rect;
    }
    if (em.HasComponent<Scene::UIScrollContainerComponent>(e)) {
        return &em.GetComponent<Scene::UIScrollContainerComponent>(e).rect;
    }
    return nullptr;
}

template <typename Component>
void SetWidgetRectOn(Component& component, const Math::Rect& rect) {
    component.rect = rect;
}

void SetWidgetRectOn(Scene::UITextComponent& text, const Math::Rect& rect) {
    // A Text is sized by its letters: the handle's height is its font size.
    text.positionPixels = rect.position;
    text.fontSize = std::clamp(rect.size.y / 1.25F, 6.0F, 400.0F);
}

using SetWidgetComponent = std::function<void(const Core::TypeMetadataEntry&, const void*)>;

/// Sets `rect` live, or (commit) puts `start` back and records `rect` as one undo step.
template <typename Component>
bool ResizeWidgetComponent(Scene::IEntityManager& em, const Scene::Entity entity, const Math::Rect& rect,
                              const Math::Rect& start, const bool commit, const SetWidgetComponent& set) {
    if (!em.HasComponent<Component>(entity)) {
        return false;
    }
    Component& component = em.GetComponent<Component>(entity);
    if (!commit) {
        SetWidgetRectOn(component, rect);
        return true;
    }
    Component next = component;
    SetWidgetRectOn(next, rect);
    SetWidgetRectOn(component, start);
    if (const Core::TypeMetadataEntry* const entry = Scene::FindSceneComponentMetadata(std::type_index(typeid(Component)))) {
        set(*entry, &next);
    } else {
        component = next;
    }
    return true;
}

bool ResizeAnyWidget(Scene::IEntityManager& em, const Scene::Entity e, const Math::Rect& r, const Math::Rect& start,
                        const bool commit, const SetWidgetComponent& set) {
    return ResizeWidgetComponent<Scene::UIView3DComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UIButtonComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UIImageComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UIProgressBarComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UISliderComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UICheckboxComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UITextInputComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UIDropdownComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UILayoutContainerComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UIScrollContainerComponent>(em, e, r, start, commit, set) ||
           ResizeWidgetComponent<Scene::UITextComponent>(em, e, r, start, commit, set);
}

[[nodiscard]] Math::Rect ToEngineRect(const prism::Rect& r) {
    return Math::Rect{Math::Vector2{r.x, r.y}, Math::Vector2{r.w, r.h}};
}

} // namespace

void EditorEntityEditor::ResizeWidget(const Scene::Entity entity, const prism::Rect rect, const bool finished) {
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (!em.IsAlive(entity)) {
        m_resizeWidget = Scene::kInvalidEntity;
        return;
    }
    if (m_resizeWidget != entity) {
        // Where it started: the component as it is before this first change.
        m_resizeWidget = entity;
        m_resizeStartRect = rect;
        if (em.HasComponent<Scene::UITextComponent>(entity) && !em.HasComponent<Scene::UIButtonComponent>(entity)) {
            const auto& text = em.GetComponent<Scene::UITextComponent>(entity);
            m_resizeStartRect = prism::Rect{text.positionPixels.x, text.positionPixels.y, rect.w, text.fontSize * 1.25F};
        } else if (const Math::Rect* current = WidgetRect(em, entity)) {
            m_resizeStartRect = prism::Rect{current->position.x, current->position.y, current->size.x, current->size.y};
        }
    }
    const SetWidgetComponent set = [this](const Core::TypeMetadataEntry& entry, const void* value) {
        static_cast<void>(m_editor.SetSelectedComponentValue(entry, value));
    };
    if (!finished) {
        static_cast<void>(ResizeAnyWidget(em, entity, ToEngineRect(rect), ToEngineRect(m_resizeStartRect), false, set));
        return;
    }
    m_editor.SelectEntity(entity);
    static_cast<void>(ResizeAnyWidget(em, entity, ToEngineRect(rect), ToEngineRect(m_resizeStartRect), true, set));
    m_resizeWidget = Scene::kInvalidEntity;
    m_designerSignature = 0;
}

void EditorEntityEditor::ShowResolutionMenu() {
    struct Preset {
        const char* label;
        float width;
        float height;
    };
    static constexpr Preset kPresets[] = {
        {"Desktop  1920 x 1080", 1920.0F, 1080.0F}, {"Desktop  2560 x 1440", 2560.0F, 1440.0F},
        {"Desktop  1280 x 720", 1280.0F, 720.0F},   {"Ultrawide  3440 x 1440", 3440.0F, 1440.0F},
        {"Phone  2340 x 1080", 2340.0F, 1080.0F},   {"Phone (portrait)  1080 x 2340", 1080.0F, 2340.0F},
        {"Tablet  2048 x 1536", 2048.0F, 1536.0F},
    };
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("SCREEN SIZE"));
    {
        Math::Vector2 device{1920.0F, 1080.0F};
        for (const Window::MonitorInfo& monitor : m_editor.m_services->GetWindowManager().EnumerateMonitors()) {
            if (monitor.width > 0U && monitor.height > 0U) {
                device = Math::Vector2{static_cast<float>(monitor.width), static_cast<float>(monitor.height)};
                if (monitor.isPrimary) {
                    break;
                }
            }
        }
        prism::MenuItem automatic = prism::MenuItem::Item(
            "Auto - this device  " + std::to_string(static_cast<int>(device.x)) + " x " + std::to_string(static_cast<int>(device.y)),
            [this] { SetDesignResolution(Math::Vector2{0.0F, 0.0F}); });
        items.push_back(std::move(automatic));
    }
    for (const Preset& preset : kPresets) {
        prism::MenuItem item = prism::MenuItem::Item(preset.label, [this, preset] {
            SetDesignResolution(Math::Vector2{preset.width, preset.height});
        });
        item.checked = m_designer && m_designer->Width() == preset.width && m_designer->Height() == preset.height;
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
[[nodiscard]] Math::Vector2 FindFreeSpot(const std::vector<WidgetDesignerView::Item>& items, const Math::Vector2 size,
                                               const float canvasW, const float canvasH) {
    constexpr float kMargin = 64.0F;
    constexpr float kGap = 24.0F;
    constexpr float kStep = 16.0F;
    const auto overlaps = [&items, size](const float x, const float y) {
        for (const WidgetDesignerView::Item& item : items) {
            if (!item.visible || item.kind == WidgetKind::Spacer) {
                continue;
            }
            const prism::Rect& r = item.rect;
            if (x < r.x + r.w + kGap && x + size.x + kGap > r.x && y < r.y + r.h + kGap && y + size.y + kGap > r.y) {
                return true;
            }
        }
        return false;
    };
    for (float y = kMargin; y + size.y <= canvasH - kMargin * 0.5F; y += kStep) {
        for (float x = kMargin; x + size.x <= canvasW - kMargin * 0.5F; x += kStep) {
            if (!overlaps(x, y)) {
                return Math::Vector2{x, y};
            }
        }
    }
    float bottom = kMargin;
    for (const WidgetDesignerView::Item& item : items) {
        bottom = std::max(bottom, item.rect.y + item.rect.h + kGap);
    }
    return Math::Vector2{kMargin, bottom};
}

} // namespace

void EditorEntityEditor::ShowWidgetPalette() {
    // The Palette: what a widget is built from, each with its own icon. A new widget goes into the
    // selected panel (a Vertical Box, a Scroll Box ...), else onto the Canvas at the first free
    // place for its size - never on top of another.
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    Scene::Entity parent = DesignerParent();
    bool intoPanel = false;
    if (const Scene::Entity selected = m_editor.GetSelectedEntity(); em.IsAlive(selected)) {
        if (const std::optional<WidgetKind> kind = ClassifyWidget(em, selected);
            kind == WidgetKind::VerticalBox || kind == WidgetKind::HorizontalBox || kind == WidgetKind::WrapBox ||
            kind == WidgetKind::ScrollBox) {
            parent = selected;
            intoPanel = true;
        }
    }
    // A shape is given where the widget goes.
    using Shape = std::function<void(Scene::IEntityManager&, Scene::Entity, Math::Vector2)>;
    const auto place = [this, parent, intoPanel](const std::string& name, const SceneObjectKind kind, const Math::Vector2 size,
                                                 const Shape& shape) {
        Math::Vector2 at{64.0F, 64.0F};
        if (intoPanel) {
            // The panel lays it out; it starts at the panel's corner.
            if (m_designer) {
                for (const WidgetDesignerView::Item& item : m_designer->Items()) {
                    if (item.entity == parent) {
                        at = Math::Vector2{item.rect.x + 8.0F, item.rect.y + 8.0F};
                    }
                }
            }
        } else if (m_designer) {
            at = FindFreeSpot(m_designer->Items(), size, m_designer->Width(), m_designer->Height());
        }
        const Scene::Entity made = m_editor.CreateDocumentSceneObject(kind, parent);
        if (made == Scene::kInvalidEntity) {
            return;
        }
        Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
        if (entities.HasComponent<Scene::NameComponent>(made)) {
            entities.GetComponent<Scene::NameComponent>(made).name = name;
        }
        if (shape) {
            shape(entities, made, at);
        }
        // A Button comes with its label, centred on it, as a Button does in a layout tool.
        if (kind == SceneObjectKind::UIButton) {
            const Scene::Entity label = m_editor.CreateDocumentSceneObject(SceneObjectKind::UIText, made);
            if (label != Scene::kInvalidEntity && entities.HasComponent<Scene::UITextComponent>(label)) {
                if (entities.HasComponent<Scene::NameComponent>(label)) {
                    entities.GetComponent<Scene::NameComponent>(label).name = name + "Label";
                }
                auto& text = entities.GetComponent<Scene::UITextComponent>(label);
                text.text = "Button";
                text.fontSize = 22.0F;
                // Laid out in the button's own rect, centred: it stays centred when the button is resized.
                text.positionPixels = at;
                text.fillParent = true;
                text.horizontalAlign = Scene::UITextHorizontalAlign::Center;
                text.verticalAlign = Scene::UITextVerticalAlign::Middle;
            }
        }
        m_editor.SelectEntity(made);
        m_designerSignature = 0;
    };
    // A widget that is not a Text, Button or Image starts as an Image and takes its own component.
    const auto instead = [](auto component) -> Shape {
        return [component](Scene::IEntityManager& entities, const Scene::Entity entity, const Math::Vector2 at) mutable {
            if (entities.HasComponent<Scene::UIImageComponent>(entity)) {
                entities.RemoveComponent<Scene::UIImageComponent>(entity);
            }
            component.rect.position = at;
            entities.AddComponent<decltype(component)>(entity, component);
        };
    };
    const auto image = [](Scene::UIImageComponent component) -> Shape {
        return [component](Scene::IEntityManager& entities, const Scene::Entity entity, const Math::Vector2 at) mutable {
            if (entities.HasComponent<Scene::UIImageComponent>(entity)) {
                component.rect.position = at;
                entities.GetComponent<Scene::UIImageComponent>(entity) = component;
            }
        };
    };
    std::vector<prism::MenuItem> items;
    const auto entry = [&items, place](const WidgetKind kind, const std::string& name, const SceneObjectKind objectKind,
                                       const Math::Vector2 size, Shape shape) {
        prism::MenuItem item =
            prism::MenuItem::Item(WidgetKindLabel(kind), [place, name, objectKind, size, shape] { place(name, objectKind, size, shape); });
        if (const EditorIcons::Image icon = WidgetIcon(kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    };
    const auto sized = [](auto component, const Math::Vector2 size) {
        component.rect.size = size;
        return component;
    };

    items.push_back(prism::MenuItem::Header("COMMON"));
    entry(WidgetKind::Text, "Text", SceneObjectKind::UIText, {170.0F, 30.0F},
          [](Scene::IEntityManager& entities, const Scene::Entity e, const Math::Vector2 at) {
              if (entities.HasComponent<Scene::UITextComponent>(e)) {
                  auto& text = entities.GetComponent<Scene::UITextComponent>(e);
                  text.text = "Text Block";
                  text.positionPixels = at;
                  text.fontSize = 24.0F;
              }
          });
    entry(WidgetKind::Button, "Button", SceneObjectKind::UIButton, {200.0F, 52.0F},
          [](Scene::IEntityManager& entities, const Scene::Entity e, const Math::Vector2 at) {
              if (entities.HasComponent<Scene::UIButtonComponent>(e)) {
                  auto& button = entities.GetComponent<Scene::UIButtonComponent>(e);
                  button.rect = Math::Rect{at, Math::Vector2{200.0F, 52.0F}};
                  button.normalColor = Math::Vector3{0.16F, 0.42F, 0.86F};
                  button.hoverColor = Math::Vector3{0.24F, 0.52F, 0.95F};
                  button.pressedColor = Math::Vector3{0.10F, 0.30F, 0.66F};
              }
          });
    entry(WidgetKind::Image, "Image", SceneObjectKind::UIImage, {128.0F, 128.0F}, image(sized(Scene::UIImageComponent{}, {128.0F, 128.0F})));
    Scene::UIImageComponent border = sized(Scene::UIImageComponent{}, {320.0F, 200.0F});
    border.tintColor = Math::Vector3{0.10F, 0.11F, 0.14F};
    border.alpha = 0.85F;
    border.nineSliceEnabled = true;
    entry(WidgetKind::Border, "Border", SceneObjectKind::UIImage, {320.0F, 200.0F}, image(border));
    Scene::UIImageComponent spacer = sized(Scene::UIImageComponent{}, {40.0F, 40.0F});
    spacer.alpha = 0.0F;
    entry(WidgetKind::Spacer, "Spacer", SceneObjectKind::UIImage, {40.0F, 40.0F}, image(spacer));

    items.push_back(prism::MenuItem::Header("INPUT"));
    entry(WidgetKind::CheckBox, "CheckBox", SceneObjectKind::UIImage, {28.0F, 28.0F},
          instead(sized(Scene::UICheckboxComponent{}, {28.0F, 28.0F})));
    Scene::UISliderComponent slider = sized(Scene::UISliderComponent{}, {260.0F, 24.0F});
    slider.value = 0.5F;
    entry(WidgetKind::Slider, "Slider", SceneObjectKind::UIImage, {260.0F, 24.0F}, instead(slider));
    Scene::UITextInputComponent input = sized(Scene::UITextInputComponent{}, {300.0F, 36.0F});
    input.placeholder = "Type here...";
    entry(WidgetKind::TextBox, "TextBox", SceneObjectKind::UIImage, {300.0F, 36.0F}, instead(input));
    Scene::UIDropdownComponent combo = sized(Scene::UIDropdownComponent{}, {220.0F, 32.0F});
    combo.options = "Option 1\nOption 2\nOption 3";
    combo.selectedIndex = 0;
    entry(WidgetKind::ComboBox, "ComboBox", SceneObjectKind::UIImage, {220.0F, 32.0F}, instead(combo));

    items.push_back(prism::MenuItem::Header("FEEDBACK"));
    Scene::UIProgressBarComponent bar = sized(Scene::UIProgressBarComponent{}, {320.0F, 24.0F});
    bar.value = 0.6F;
    entry(WidgetKind::ProgressBar, "ProgressBar", SceneObjectKind::UIImage, {320.0F, 24.0F}, instead(bar));

    items.push_back(prism::MenuItem::Header("3D"));
    entry(WidgetKind::View3D, "View3D", SceneObjectKind::UIImage, {360.0F, 480.0F},
          instead(sized(Scene::UIView3DComponent{}, {360.0F, 480.0F})));

    items.push_back(prism::MenuItem::Header("PANEL"));
    Scene::UILayoutContainerComponent vertical = sized(Scene::UILayoutContainerComponent{}, {260.0F, 300.0F});
    vertical.direction = Scene::UILayoutDirection::Vertical;
    vertical.padding = 8.0F;
    vertical.spacing = 8.0F;
    entry(WidgetKind::VerticalBox, "VerticalBox", SceneObjectKind::UIImage, {260.0F, 300.0F}, instead(vertical));
    Scene::UILayoutContainerComponent horizontal = sized(vertical, {480.0F, 80.0F});
    horizontal.direction = Scene::UILayoutDirection::Horizontal;
    entry(WidgetKind::HorizontalBox, "HorizontalBox", SceneObjectKind::UIImage, {480.0F, 80.0F}, instead(horizontal));
    Scene::UILayoutContainerComponent wrap = sized(horizontal, {420.0F, 260.0F});
    wrap.wrapAfter = 3U;
    entry(WidgetKind::WrapBox, "WrapBox", SceneObjectKind::UIImage, {420.0F, 260.0F}, instead(wrap));
    const Scene::UIScrollContainerComponent scroll = sized(Scene::UIScrollContainerComponent{}, {280.0F, 320.0F});
    entry(WidgetKind::ScrollBox, "ScrollBox", SceneObjectKind::UIImage, {280.0F, 320.0F},
          [scroll](Scene::IEntityManager& entities, const Scene::Entity e, const Math::Vector2 at) {
              if (entities.HasComponent<Scene::UIImageComponent>(e)) {
                  entities.RemoveComponent<Scene::UIImageComponent>(e);
              }
              Scene::UIScrollContainerComponent placed = scroll;
              placed.rect.position = at;
              entities.AddComponent<Scene::UIScrollContainerComponent>(e, placed);
              // Its scrollbar on the same widget, down its right edge.
              Scene::UIScrollbarComponent track;
              track.rect = Math::Rect{Math::Vector2{at.x + placed.rect.size.x - 12.0F, at.y}, Math::Vector2{12.0F, placed.rect.size.y}};
              entities.AddComponent<Scene::UIScrollbarComponent>(e, track);
          });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search widgets");
}

std::vector<Scene::Entity> EditorEntityEditor::WidgetScreens() const {
    std::vector<Scene::Entity> screens;
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        return screens;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    for (const Scene::Entity child : m_editor.m_services->GetSceneGraph().GetChildren(em, root)) {
        if (em.HasComponent<Scene::CanvasComponent>(child)) {
            screens.push_back(child);
        }
    }
    return screens;
}

Scene::Entity EditorEntityEditor::DesignerParent() {
    const std::vector<Scene::Entity> screens = WidgetScreens();
    if (screens.empty()) {
        m_activeScreen = Scene::kInvalidEntity;
        return m_editor.GetEntityEditorRoot();
    }
    if (std::find(screens.begin(), screens.end(), m_activeScreen) == screens.end()) {
        m_activeScreen = screens.front();
    }
    return m_activeScreen;
}

void EditorEntityEditor::AddWidgetScreen() {
    // The first time, the widget's own widgets become its Main screen; then a new, hidden screen
    // (the Graph's Show Screen brings it up) opens in the Designer.
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const auto makeScreen = [&](const std::string& name, const bool visible) {
        const Scene::Entity screen = m_editor.CreateDocumentSceneObject(SceneObjectKind::Canvas, root);
        if (screen == Scene::kInvalidEntity) {
            return screen;
        }
        if (em.HasComponent<Scene::NameComponent>(screen)) {
            em.GetComponent<Scene::NameComponent>(screen).name = name;
        }
        if (em.HasComponent<Scene::CanvasComponent>(screen)) {
            em.GetComponent<Scene::CanvasComponent>(screen).visible = visible;
        }
        return screen;
    };
    std::vector<Scene::Entity> screens = WidgetScreens();
    if (screens.empty()) {
        const std::vector<Scene::Entity> children = m_editor.m_services->GetSceneGraph().GetChildren(em, root);
        const Scene::Entity main = makeScreen("Main", true);
        if (main == Scene::kInvalidEntity) {
            return;
        }
        for (const Scene::Entity child : children) {
            const bool stage = em.HasComponent<Scene::NameComponent>(child) &&
                               em.GetComponent<Scene::NameComponent>(child).name == Scene::kWidgetStageName;
            if (child != main && !stage) {
                static_cast<void>(m_editor.ReparentDocumentEntity(child, main));
            }
        }
        screens.push_back(main);
    }
    std::string name = "Screen" + std::to_string(screens.size() + 1U);
    const Scene::Entity made = makeScreen(name, false);
    if (made != Scene::kInvalidEntity) {
        m_activeScreen = made;
        m_editor.SelectEntity(made);
    }
    m_designerSignature = 0;
    SetWidgetPage(0);
}

void EditorEntityEditor::ShowScreenSettings(const Scene::Entity screen) {
    // Rename or delete a screen (its name is what the Graph's Show Screen calls it by).
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const std::string current = em.HasComponent<Scene::NameComponent>(screen) ? em.GetComponent<Scene::NameComponent>(screen).name
                                                                                    : std::string("Screen");
    auto content = prism::vbox(6.0F);
    content->add(prism::make<prism::Label>("Screen name (Show Screen uses it):"));
    auto input = prism::make<prism::TextInput>(current, "Screen name");
    content->add(input);
    m_host.App().runInWindow(m_window, [this, screen, input, content] {
        prism::showDialog(
            "Screen", content,
            {prism::DialogButton{"Rename", true,
                                 [this, screen, input] {
                                     const Editor::EntityDocumentScope scope(m_editor);
                                     const std::string name = input->text();
                                     if (!name.empty()) {
                                         m_editor.SelectEntity(screen);
                                         static_cast<void>(m_editor.SetSelectedEntityName(name));
                                     }
                                     m_crumbSignature.clear();
                                 }},
             prism::DialogButton{"Delete Screen", false,
                                 [this, screen] {
                                     const Editor::EntityDocumentScope scope(m_editor);
                                     m_editor.SelectEntity(screen);
                                     static_cast<void>(m_editor.DeleteSelectedEntity());
                                     m_activeScreen = Scene::kInvalidEntity;
                                     m_designerSignature = 0;
                                     m_crumbSignature.clear();
                                 }},
             prism::DialogButton{"Cancel", false, [] {}}},
            380.0F);
    });
}

Math::Vector2 EditorEntityEditor::DesignResolution() const {
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (root != Scene::kInvalidEntity && em.HasComponent<Scene::CanvasComponent>(root)) {
        const Math::Vector2 reference = em.GetComponent<Scene::CanvasComponent>(root).referenceResolution;
        if (reference.x > 0.0F && reference.y > 0.0F) {
            return reference;
        }
    }
    // Auto: this device's own screen.
    Math::Vector2 device{1920.0F, 1080.0F};
    for (const Window::MonitorInfo& monitor : m_editor.m_services->GetWindowManager().EnumerateMonitors()) {
        if (monitor.width > 0U && monitor.height > 0U) {
            device = Math::Vector2{static_cast<float>(monitor.width), static_cast<float>(monitor.height)};
            if (monitor.isPrimary) {
                break;
            }
        }
    }
    return device;
}

void EditorEntityEditor::SetDesignResolution(const Math::Vector2 size) {
    // Kept on the widget (its root Canvas), one undo step; the game fits the canvas to any screen.
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (root == Scene::kInvalidEntity || !em.HasComponent<Scene::CanvasComponent>(root)) {
        return;
    }
    const Scene::Entity previous = m_editor.GetSelectedEntity();
    Scene::CanvasComponent next = em.GetComponent<Scene::CanvasComponent>(root);
    next.referenceResolution = size;
    next.scaleWithScreen = true;
    m_editor.SelectEntity(root);
    if (const Core::TypeMetadataEntry* const entry =
            Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::CanvasComponent)))) {
        static_cast<void>(m_editor.SetSelectedComponentValue(*entry, &next));
    }
    if (previous != Scene::kInvalidEntity && em.IsAlive(previous)) {
        m_editor.SelectEntity(previous);
    }
    m_crumbSignature.clear();
    m_designerSignature = 0;
}

void EditorEntityEditor::SetWidgetPage(const int page) {
    // 0 Designer, 1 Graph, 2 Stage.
    if (page == 1) {
        OpenWidgetGraph();
        return;
    }
    m_stageMode = page == 2;
    if (m_stageMode) {
        EnsureWidgetStage();
    }
    SetBlueprintMode(false);
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
    SyncBlueprintCrumbs();
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

Scene::Entity EditorEntityEditor::FindWidgetStage() const {
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (root == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    for (const Scene::Entity child : m_editor.m_services->GetSceneGraph().GetChildren(em, root)) {
        if (em.HasComponent<Scene::NameComponent>(child) &&
            em.GetComponent<Scene::NameComponent>(child).name == Scene::kWidgetStageName) {
            return child;
        }
    }
    return Scene::kInvalidEntity;
}

void EditorEntityEditor::EnsureWidgetStage() {
    // A widget's first Stage: a floor, a key and a rim light, the camera a 3D View looks
    // through and the spot its asset stands on - a lit set, ready to dress.
    if (FindWidgetStage() != Scene::kInvalidEntity) {
        return;
    }
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    if (root == Scene::kInvalidEntity) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    Scene::ISceneGraph& graph = m_editor.m_services->GetSceneGraph();
    const auto make = [&](const SceneObjectKind kind, const std::string_view name, const Scene::Entity parent,
                          const Math::Vector3 position, const Math::Vector3 eulerDegrees, const Math::Vector3 scale) {
        const Scene::Entity entity = m_editor.CreateDocumentSceneObject(kind, parent);
        if (entity == Scene::kInvalidEntity) {
            return entity;
        }
        if (em.HasComponent<Scene::NameComponent>(entity)) {
            em.GetComponent<Scene::NameComponent>(entity).name = std::string(name);
        }
        if (em.HasComponent<Scene::TransformComponent>(entity)) {
            Scene::TransformComponent transform = em.GetComponent<Scene::TransformComponent>(entity);
            const float k = Math::kPi / 180.0F;
            transform.localPosition = position;
            transform.localEulerRadians = Math::Vector3{eulerDegrees.x * k, eulerDegrees.y * k, eulerDegrees.z * k};
            static_cast<void>(Math::TryMakeEuler(transform.localEulerRadians, transform.localRotation));
            transform.localScale = scale;
            graph.SetLocalTransform(em, entity, transform);
        }
        return entity;
    };
    const Math::Vector3 one{1.0F, 1.0F, 1.0F};
    const Scene::Entity stage = make(SceneObjectKind::Object3D, Scene::kWidgetStageName, root, {}, {}, one);
    if (stage == Scene::kInvalidEntity) {
        return;
    }
    const Scene::Entity floor = make(SceneObjectKind::PlaneMesh3D, "Floor", stage, {}, {}, Math::Vector3{8.0F, 1.0F, 8.0F});
    if (floor != Scene::kInvalidEntity && em.HasComponent<Scene::PrimitiveMeshComponent>(floor)) {
        em.GetComponent<Scene::PrimitiveMeshComponent>(floor).baseColor = Math::Vector3{0.34F, 0.36F, 0.42F};
    }
    const auto light = [&](const std::string_view name, const Math::Vector3 position, const Math::Color color, const float intensity) {
        const Scene::Entity entity = make(SceneObjectKind::Light3D, name, stage, position, {}, one);
        if (entity != Scene::kInvalidEntity && em.HasComponent<Scene::LightComponent>(entity)) {
            Scene::LightComponent& l = em.GetComponent<Scene::LightComponent>(entity);
            l.type = Scene::LightType::Point;
            l.color = color;
            l.intensity = intensity;
            l.range = 14.0F;
        }
    };
    light("KeyLight", Math::Vector3{2.2F, 3.2F, 2.6F}, Math::Color{1.0F, 0.95F, 0.88F}, 14.0F);
    light("RimLight", Math::Vector3{-2.6F, 2.4F, -2.2F}, Math::Color{0.55F, 0.7F, 1.0F}, 9.0F);
    light("FillLight", Math::Vector3{-2.4F, 1.6F, 3.0F}, Math::Color{0.9F, 0.92F, 1.0F}, 5.0F);
    const Scene::Entity camera =
        make(SceneObjectKind::Camera3D, Scene::kWidgetStageCameraName, stage, Math::Vector3{0.0F, 1.05F, 4.8F},
             Math::Vector3{-2.0F, 0.0F, 0.0F}, one);
    if (camera != Scene::kInvalidEntity && em.HasComponent<Scene::CameraComponent>(camera)) {
        Scene::CameraComponent& lens = em.GetComponent<Scene::CameraComponent>(camera);
        lens.fieldOfViewDegrees = 32.0F;
        lens.current = false;
    }
    make(SceneObjectKind::Marker3D, Scene::kWidgetModelSpotName, stage, {}, {}, one);
    m_editor.SelectEntity(stage);
}

void EditorEntityEditor::ShowStageAddMenu() {
    // What a Stage is dressed with: everything an entity is built from, under the Stage.
    const Scene::Entity stage = FindWidgetStage();
    std::vector<prism::MenuItem> items = BuildAddObjectMenu(
        AddObjectContext::Entity, m_icons, [this, stage](const SceneObjectKind kind) {
            Scene::Entity parent = stage;
            const Scene::Entity selected = m_editor.GetSelectedEntity();
            // Into the selected Stage object when it is one, else onto the Stage.
            for (Scene::Entity cursor = selected; cursor != Scene::kInvalidEntity;) {
                if (cursor == stage) {
                    parent = selected;
                    break;
                }
                Scene::Entity up = Scene::kInvalidEntity;
                if (!m_editor.TryGetDocumentParent(cursor, up) || up == cursor) {
                    break;
                }
                cursor = up;
            }
            static_cast<void>(m_editor.CreateDocumentSceneObject(kind, parent));
        });
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search stage objects");
}

void EditorEntityEditor::ShowAddMenu() {
    // What an entity is built from (no level environment, sky or streaming), grouped and searchable.
    // Only what fits this entity's root: a character's parts, or a prop's.
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    const EntityRoot rootKind =
        root == Scene::kInvalidEntity
            ? EntityRoot::Any
            : EntityRootOf(Scene::ResolveSceneObjectKind(m_editor.m_services->GetEntityManager(), root));
    std::vector<prism::MenuItem> items = BuildAddObjectMenu(
        AddObjectContext::Entity, m_icons,
        [this](const SceneObjectKind kind) { static_cast<void>(m_editor.CreateDocumentSceneObject(kind)); }, {}, rootKind);
    prism::showSearchMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search components");
}

void EditorEntityEditor::Compile() {
    if (m_blueprintPanel.IsOpen()) {
        m_blueprintPanel.Compile();   // compiles the entity too, and marks the Blueprint's nodes
    } else {
        static_cast<void>(m_editor.CompileEntityEditor());
    }
    m_problemsShown = ~std::size_t{0};
}

void EditorEntityEditor::ShowResults(const int tab) {
    if (tab >= 0 && tab < 3) {
        ShowDockPanel(kCompilePage + tab, true);
    }
}

void EditorEntityEditor::RunFind(const std::string& query) {
    m_findRows = m_editor.FindInEntityEditor(query);
    if (m_findTable) {
        m_findTable->setRowCount(static_cast<int>(m_findRows.size()));
    }
    m_editor.m_contentStatusMessage = query.empty()        ? std::string()
                                      : m_findRows.empty() ? "\"" + query + "\" is not in the entity."
                                                           : std::to_string(m_findRows.size()) + " found for \"" + query + "\".";
    if (!query.empty()) {
        ShowResults(1);
    }
}

void EditorEntityEditor::RunDiff() {
    m_diffRows = m_editor.DiffEntityEditor();
    if (m_diffTable) {
        m_diffTable->setRowCount(static_cast<int>(m_diffRows.size()));
    }
    m_editor.m_contentStatusMessage = m_diffRows.empty() ? "No changes since " + m_openPath.filename().string() + " was saved."
                                                         : std::to_string(m_diffRows.size()) +
                                                               (m_diffRows.size() == 1U ? " change" : " changes") +
                                                               " since the last save.";
    ShowResults(2);
}

void EditorEntityEditor::ShowClassSettings(const prism::Rect anchor) {
    // The entity's own settings: what it is, where it lives, its root, and the project roles it can take.
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    if (root != Scene::kInvalidEntity) {
        m_editor.SelectEntity(root);   // the root's components follow in Details
    }
    auto grid = prism::make<prism::PropertyGrid>();
    const int entity = grid->addCategory("Entity", true);
    const auto text = [](const std::string& value) {
        auto label = prism::make<prism::Label>(value);
        label->setColor(prism::ThemeColor(&prism::Theme::textBright));
        return label;
    };
    grid->addRow(entity, "File", text(m_openPath.filename().string()));
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(m_openPath, snapshot.contentRoot, error);
    grid->addRow(entity, "Folder", text(error ? m_openPath.parent_path().string()
                                       : relative.parent_path().empty() ? std::string("Content")
                                                                        : "Content/" + relative.parent_path().generic_string()));
    grid->addRow(entity, "Root", text(root != Scene::kInvalidEntity ? m_editor.GetEntityDisplayLabel(root) : "(none)"));
    grid->addRow(entity, "Root Type",
                 text(root != Scene::kInvalidEntity ? std::string(m_editor.GetObjectTypeName(root)) : "-"));
    grid->addRow(entity, "Objects", text(std::to_string(m_editor.CollectEntityEditorObjects().size())));

    const int project = grid->addCategory("Project", true);
    if (!error && m_openPath.extension() == ".uventity") {
        const bool isDefault = m_editor.GetDefaultPlayerEntity() == relative.generic_string();
        auto player = prism::make<prism::CheckBox>("", isDefault);
        player->setTooltip("Play spawns this entity as the player");
        player->onChange = [this, relative](const bool on) {
            if (!m_editor.SetDefaultPlayerEntity(on ? relative : std::filesystem::path{})) {
                m_editor.m_contentStatusMessage = "Could not change the default player.";
            }
        };
        grid->addRow(project, "Default Player", player);
    }
    grid->addRow(project, "Compiled", text(!m_editor.HasEntityEditorCompiled()             ? "Not yet"
                                           : m_editor.GetEntityEditorProblems().empty() ? "Yes, no problems"
                                                                                            : std::to_string(m_editor.GetEntityEditorProblems().size()) + " problems"));

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

Scene::Entity EditorEntityEditor::EntityForTreeId(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntity;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

void EditorEntityEditor::RefreshComponents() {
    if (!m_components) {
        return;
    }
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    Scene::ISceneGraph& graph = m_editor.m_services->GetSceneGraph();
    const Scene::Entity root = m_editor.GetEntityEditorRoot();
    const std::vector<Scene::Entity> roots =
        root != Scene::kInvalidEntity ? std::vector<Scene::Entity>{root} : m_editor.GetDocumentRoots();

    std::uint64_t h = 1469598103934665603ULL;
    const std::function<void(Scene::Entity, int)> visit = [&](const Scene::Entity e, const int depth) {
        Hash(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
        Hash(h, static_cast<std::uint64_t>(depth));
        Hash(h, m_editor.GetEntityDisplayLabel(e));
        Hash(h, m_editor.IsEntitySelected(e) ? 1U : 0U);
        for (const Scene::Entity child : graph.GetChildren(em, e)) {
            visit(child, depth + 1);
        }
    };
    for (const Scene::Entity r : roots) {
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
    const std::function<void(prism::TreeNode*, Scene::Entity)> add = [&](prism::TreeNode* parent,
                                                                           const Scene::Entity e) {
        m_treeEntities.push_back(e);
        const int id = static_cast<int>(m_treeEntities.size());
        prism::TreeNode* node = m_components->addNode(parent, m_editor.GetEntityDisplayLabel(e), prism::Icon::Dot, id);
        if (const EditorIcons::Image icon = m_icons.Object(Scene::ResolveSceneObjectKind(em, e), 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(m_editor.GetObjectTypeName(e));
        if (m_widgetMode) {
            // A widget reads as what it is (Combo Box, Vertical Box ...), with the widget's own icon.
            if (const std::optional<WidgetKind> kind = ClassifyWidget(em, e)) {
                const EditorIcons::Image icon = WidgetIcon(*kind, 16);
                node->image = icon.pixels;
                node->imageSize = icon.size;
                node->badge = e == m_editor.GetEntityEditorRoot() ? std::string("Widget") : std::string(WidgetKindLabel(*kind));
            }
        }
        if (m_editor.IsEntitySelected(e)) {
            selected.push_back(id);
        }
        for (const Scene::Entity child : graph.GetChildren(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::Entity r : roots) {
        add(nullptr, r);
    }
    m_components->setSelection(selected);
    if (m_componentsSearch) {
        m_components->setFilter(m_componentsSearch->text());
    }
    m_components->refresh();
    m_syncingSelection = false;
}

void EditorEntityEditor::RefreshProblems() {
    if (!m_problems || !m_problemsPanel) {
        return;
    }
    // What the entity's make-up is missing shows at once, without a Compile.
    if (m_editor.RefreshEntityStructureProblems()) {
        m_problemsShown = static_cast<std::size_t>(-1);
        m_problems->setRowCount(0);
    }
    const std::vector<Editor::EntityCompileProblem>& problems = m_editor.GetEntityEditorProblems();
    const bool compiled = m_editor.HasEntityEditorCompiled();
    if (problems.size() == m_problemsShown && compiled == m_problemsCompiled) {
        return;
    }
    const bool newlyCompiled = compiled && (problems.size() != m_problemsShown || !m_problemsCompiled);
    m_problemsShown = problems.size();
    m_problemsCompiled = compiled;
    m_problems->setRowCount(static_cast<int>(problems.size()));
    // A Compile with problems opens them; a clean one leaves the area as it was.
    if (newlyCompiled && !problems.empty()) {
        ShowResults(0);
    }
}

void EditorEntityEditor::RefreshToolbar() {
    const bool unsaved = m_editor.HasEntityEditorUnsavedChanges();
    if (m_tab) {
        m_tab->setName(m_openPath.stem().string());
        m_tab->setDirty(unsaved);
        const Scene::Entity root = m_editor.GetEntityEditorRoot();
        if (root != Scene::kInvalidEntity) {
            const EditorIcons::Image icon = m_icons.Object(
                Scene::ResolveSceneObjectKind(m_editor.m_services->GetEntityManager(), root), 16);
            m_tab->setIcon(icon.pixels, icon.size);
        }
    }
    if (m_revertButton) {
        m_revertButton->setEnabled(unsaved);
    }
    using Simulation = Editor::EntitySimulationState;
    const Simulation simulation = m_editor.GetEntitySimulationState();
    if (m_playButton) {
        const bool running = simulation != Simulation::Stopped;
        m_playButton->setChecked(running);
        if (m_playButton->text() != (running ? "Stop" : "Simulate")) {
            m_playButton->setText(running ? "Stop" : "Simulate");
            m_playButton->setIcon(running ? prism::Icon::Stop : prism::Icon::Play);
        }
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(simulation == Simulation::Paused);
        m_pauseButton->setEnabled(simulation != Simulation::Stopped);
    }
    if (m_stopButton) {
        m_stopButton->setEnabled(simulation != Simulation::Stopped);
    }
    if (m_compileState) {
        const std::vector<Editor::EntityCompileProblem>& problems = m_editor.GetEntityEditorProblems();
        std::string text = "Not compiled";
        prism::Color color = prism::theme().textDim;
        if (m_editor.HasEntityEditorCompiled()) {
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

void EditorEntityEditor::Refresh() {
    if (m_window == 0 || !m_editor.IsEntityDocumentActive()) {
        return;
    }
    // A running simulation advances by the time since the last frame (paused, it keeps the clock).
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTick.time_since_epoch().count() == 0
                          ? 0.0
                          : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    if (m_editor.GetEntitySimulationState() == Editor::EntitySimulationState::Running) {
        m_editor.TickEntitySimulation(dt);
    }
    m_host.App().runInWindow(m_window, [this, dt] {
        RefreshComponents();
        m_details.Refresh();
        m_viewToolbar.Refresh();
        RefreshToolbar();
        RefreshProblems();
        SyncAnimationTabs();
        const bool sequencerShown = m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kSequencerPage &&
                                    m_dockTabs.size() == kDockPageCount && m_dockTabs[kSequencerPage]->visible();
        m_sequencer.Refresh(dt, sequencerShown);
        const bool graphShown = m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kGraphPage &&
                                m_dockTabs.size() == kDockPageCount && m_dockTabs[kGraphPage]->visible();
        m_animGraphPanel.Refresh(dt, graphShown);
        SyncGraphDetails(graphShown);
        m_blueprintPanel.Refresh();
        if (!m_editor.m_pendingBlueprintOpen.empty()) {
            // Opened from the Asset Table: the object that runs it.
            const std::string pending = std::exchange(m_editor.m_pendingBlueprintOpen, std::string{});
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
                if (em.HasComponent<Scene::ScriptComponent>(object) &&
                    em.GetComponent<Scene::ScriptComponent>(object).scriptAssetPath == pending) {
                    OpenBlueprint(pending, object);
                    break;
                }
            }
        }
        if (m_editor.m_pendingBlueprintCreate.has_value()) {
            // Made from the Asset Table: the graph on the object it names, opened here.
            const Editor::PendingBlueprintCreate pending = *m_editor.m_pendingBlueprintCreate;
            m_editor.m_pendingBlueprintCreate.reset();
            CreateBlueprintOn(pending.file, pending.objectPath);
        }
        SyncBlueprintTab();
        if (m_widgetMode) {
            RefreshDesigner();
        }
        m_dockPanels.Refresh(m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kConsolePage, false);
        if (m_contentFrame && (m_drawerPopup || (m_contentDocked && m_dockVisible && m_dockPages && m_dockPages->activeIndex() == kContentPage))) {
            m_assetTable.Refresh();
        }
    });
}

// ---------------------------------------------------------------------------------------- render

void EditorEntityEditor::Render() {
    if (m_window == 0 || !m_editor.IsEntityDocumentActive()) {
        return;
    }
    std::vector<EditorHost::ViewImage> views;
    if (m_viewport && m_viewport->visible() && m_editor.m_viewports.entity) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            // Inside the entity's scope this is the entity view's own state.
            Editor::ViewportOverlayState& state = m_editor.m_viewportOverlayState;
            state.gameWorkspaceActive = false;
            state.studioView = false;
            state.entityAuthoringView = true;
            state.bones.clear();
            state.colliderLines.clear();
            m_editor.BuildSkeletonOverlay(state.bones);
            m_editor.BuildColliderOverlay(state.colliderLines);
            state.pointerOverOverlay = false;
            state.hostDrawsGizmos = true;
            if (m_gizmos) {
                const std::array<float, 2> orbit = m_gizmos->TakeOrbitRequest();
                state.orbitRequestX = orbit[0];
                state.orbitRequestY = orbit[1];
                m_gizmos->ApplyTouchControllerToState(state);
            }
            state.input = m_host.CaptureViewInput(m_window, rect);
            const float dpiScale = std::max(1.0F, state.input.dpiScale);
            const bool compactSurface = prism::Application::get().platform().singleSurface();
            const float renderScale = state.resolvedRenderScale > 0.1F
                                          ? std::clamp(state.resolvedRenderScale, 0.5F, 1.0F)
                                          : (compactSurface ? 0.80F : 1.0F);
            const float effectiveScale = dpiScale * renderScale;
            const float renderW = std::max(1.0F, std::floor(rect.w * effectiveScale));
            const float renderH = std::max(1.0F, std::floor(rect.h * effectiveScale));
            if (renderScale < 0.999F) {
                state.input.imageOrigin = Math::Vector2{state.input.imageOrigin.x * renderScale,
                                                           state.input.imageOrigin.y * renderScale};
                state.input.mousePos = Math::Vector2{state.input.mousePos.x * renderScale,
                                                        state.input.mousePos.y * renderScale};
                state.input.mouseDelta = Math::Vector2{state.input.mouseDelta.x * renderScale,
                                                          state.input.mouseDelta.y * renderScale};
                state.input.dpiScale = effectiveScale;
                for (std::size_t i = 0U; i < EditorViewportInput::kButtonCount; ++i) {
                    state.input.pressPos[i] = Math::Vector2{state.input.pressPos[i].x * renderScale,
                                                               state.input.pressPos[i].y * renderScale};
                    state.input.dragDistance[i] *= renderScale;
                }
            }
            Math::Vector2 used{0.0F, 0.0F};
            const std::uint64_t texture =
                m_editor.m_viewports.entity(Math::Vector2{renderW, renderH}, used, state);
            m_host.ApplyViewCursor(m_window, state.input);
            state.orbitRequestX = 0.0F;
            state.orbitRequestY = 0.0F;
            if (m_gizmos) {
                m_gizmos->Sync();
            }
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorHost::ViewImage view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), std::floor(rect.w), std::floor(rect.h)};
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
        const Scene::Entity live = m_designer->LivePreview();
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        const prism::Rect rect = m_designer->ViewRectOf(live);
        if (live != Scene::kInvalidEntity && rect.w > 0.0F && em.IsAlive(live) &&
            em.HasComponent<Scene::UIView3DComponent>(live)) {
            const Scene::UIView3DComponent& component = em.GetComponent<Scene::UIView3DComponent>(live);
            Editor::WidgetPreviewRequest request;
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
                    const Scene::Entity stage = FindWidgetStage();
                    if (stage != Scene::kInvalidEntity) {
                        if (const std::optional<Scene::SceneSnapshot> snapshot = m_editor.m_services->GetSceneSerializer().Capture(
                                m_editor.m_services->GetEntityManager(), {stage}, Scene::SceneAssetType::Prefab)) {
                            m_stageSnapshot = snapshot->bytes;
                        }
                    }
                }
                request.stage = m_stageSnapshot;
            }
            const float dpiScale = std::max(1.0F, m_host.App().platform().dpiScale());
            Math::Vector2 used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderWidgetPreview(
                request, Math::Vector2{std::max(1.0F, std::floor(rect.w * dpiScale)), std::max(1.0F, std::floor(rect.h * dpiScale))},
                used);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorHost::ViewImage view{};
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
    EditorShell::AppendCameraScreen(m_editor, m_details, m_cameraScreen.get(), views);
    m_host.RenderWindow(m_window, views);
}

bool EditorEntityEditor::SaveScreenshot(const std::filesystem::path& path) {
    return m_window != 0 && m_host.SaveWindowScreenshot(m_window, path);
}

} // namespace UVE::Editor
