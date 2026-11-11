// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_shell.h"

#include "editor_prism_chrome.h"

#include "editor.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Menu.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Docking.h"
#include "prism/Layouts.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
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

void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

} // namespace

EditorPrismShellUVE::EditorPrismShellUVE(EditorUVE& editor)
    : m_editor(editor), m_entityEditor(editor, m_host), m_contentBrowser(editor), m_details2(editor), m_dockPanels(editor),
      m_viewToolbar(editor) {
    m_entityEditor.onBrowse = [this](const std::filesystem::path& path) {
        ShowDockPanelUVE(0, true);
        m_contentBrowser.RevealFileUVE(path);
        m_host.FocusWindowUVE(EditorPrismHostUVE::kMainWindowUVE);
    };
}

bool EditorPrismShellUVE::SaveEntityEditorScreenshotUVE(const std::filesystem::path& path) {
    const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
    return m_entityEditor.SaveScreenshotUVE(path);
}

EditorPrismShellUVE::~EditorPrismShellUVE() {
    ShutdownUVE();
}

bool EditorPrismShellUVE::InitializeUVE(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (!m_host.InitializeUVE(window, assetDir)) {
        return false;
    }
    m_dockPanels.AttachLogUVE();
    m_host.SetRootUVE(BuildLayoutUVE());
    // Every keyboard shortcut is an editor command's (the same registry the menus use).
    m_host.AppUVE().setShortcutHandler([this](const prism::Key key, const std::uint8_t mods) {
        return DispatchShortcutUVE(key, mods);
    });
    return true;
}

void EditorPrismShellUVE::ShutdownUVE() {
    if (m_host.IsReadyUVE() && m_entityEditor.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_entityEditor.WindowIdUVE());
    }
    m_host.ShutdownUVE();
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildLayoutUVE() {
    // Editor design sheet, top to bottom:
    //   [#1 logo] [#15 title bar: menus | drag strip | - [] x]
    //   [       ] [#2 viewport / level name tab               ]
    //   sub row 1: #4 #5 #3 | #14 | transport
    //   sub row 2: gizmo / snap / view settings (full width)
    //   [#6 Outliner | #25 viewport over the dock panel | #17 Details]
    //   #16 dock bar: Content Browser, Output Log, Console, Profiler ...
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());
    window->add(BuildToolbarRowUVE());
    window->add(m_viewToolbar.BuildUVE());

    auto centre = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centre->add(BuildViewportUVE(), prism::Splitter::Pane{400.0F, 160.0F, 1e9F, true});
    m_dockArea = BuildDockUVE();
    centre->add(m_dockArea, prism::Splitter::Pane{290.0F, 110.0F, 900.0F, false});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    m_outlinerColumn = BuildOutlinerUVE();
    m_detailsColumn = BuildDetailsUVE();
    columns->add(m_outlinerColumn, prism::Splitter::Pane{250.0F, 170.0F, 700.0F, false});
    columns->add(centre, prism::Splitter::Pane{800.0F, 320.0F, 1e9F, true});
    columns->add(m_detailsColumn, prism::Splitter::Pane{330.0F, 220.0F, 800.0F, false});
    window->add(columns, prism::Slot{1.0F});

    // Docking: drag a panel's tab onto another panel's edge to split it there (the Details under
    // or beside the Outliner, say), onto its tab strip to share the area, or anywhere else to float
    // it in a window of its own. Every divider resizes.
    m_dockSpace = std::make_unique<prism::DockSpace>("level-editor");
    m_dockSpace->attach(*m_outlinerTabs);
    m_dockSpace->attach(*m_detailsTabs);
    m_dockSpace->onChanged = [this] { UpdateDockColumnsUVE(); };

    window->add(BuildDockBarUVE());

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

std::vector<prism::MenuItem> EditorPrismShellUVE::CommandItemsUVE(const std::initializer_list<const char*> ids) {
    std::vector<prism::MenuItem> items;
    for (const char* const id : ids) {
        if (id == nullptr) {
            items.push_back(prism::MenuItem::Sep());
            continue;
        }
        EditorCommandUVE* const command = m_editor.FindEditorCommandUVE(id);
        if (command == nullptr) {
            continue;
        }
        prism::MenuItem item = prism::MenuItem::Item(
            command->label, [this, commandId = std::string(id)] { static_cast<void>(m_editor.RunEditorCommandUVE(commandId)); },
            DisplayEditorShortcutUVE(command->shortcuts[0]));
        item.enabled = !command->isEnabled || command->isEnabled();
        items.push_back(std::move(item));
    }
    return items;
}

bool EditorPrismShellUVE::DispatchShortcutUVE(const prism::Key key, const std::uint8_t mods) {
    // PRISM key + modifiers -> the editor's stable shortcut name ("Ctrl+S") -> its command.
    std::string name;
    const int k = static_cast<int>(key);
    if (key >= prism::Key::A && key <= prism::Key::Z) {
        name = std::string(1, static_cast<char>('A' + (k - static_cast<int>(prism::Key::A))));
    } else if (key >= prism::Key::N0 && key <= prism::Key::N9) {
        name = std::string(1, static_cast<char>('0' + (k - static_cast<int>(prism::Key::N0))));
    } else if (key >= prism::Key::F1 && key <= prism::Key::F12) {
        name = "F" + std::to_string(1 + (k - static_cast<int>(prism::Key::F1)));
    } else {
        switch (key) {
            case prism::Key::Delete: name = "Delete"; break;
            case prism::Key::Backspace: name = "Backspace"; break;
            case prism::Key::Home: name = "Home"; break;
            case prism::Key::End: name = "End"; break;
            case prism::Key::PageUp: name = "PageUp"; break;
            case prism::Key::PageDown: name = "PageDown"; break;
            case prism::Key::Space: name = "Space"; break;
            case prism::Key::Enter: name = "Enter"; break;
            case prism::Key::Tab: name = "Tab"; break;
            case prism::Key::Left: name = "Left"; break;
            case prism::Key::Right: name = "Right"; break;
            case prism::Key::Up: name = "Up"; break;
            case prism::Key::Down: name = "Down"; break;
            default: return false;
        }
    }
    std::string text;
    text += (mods & prism::ModCtrl) != 0 ? "Ctrl+" : "";
    text += (mods & prism::ModShift) != 0 ? "Shift+" : "";
    text += (mods & prism::ModAlt) != 0 ? "Alt+" : "";
    text += name;
    const std::optional<EditorShortcutUVE> pressed = ParseEditorShortcutUVE(text);
    if (!pressed.has_value()) {
        return false;
    }
    for (const EditorCommandUVE& command : m_editor.GetEditorCommandsUVE()) {
        if (command.shortcuts[0] == *pressed || command.shortcuts[1] == *pressed) {
            return m_editor.RunEditorCommandUVE(command.id);
        }
    }
    return false;
}

namespace {


/// The Outliner's drop target for content dragged from the Content Browser.
class OutlinerDropUVE final : public prism::Border {
public:
    explicit OutlinerDropUVE(EditorPrismShellUVE& shell) : m_shell(shell) {}
    bool onDragEnter(const prism::DragDropEvent& e) override { return Accepts(e); }
    bool onDragOver(const prism::DragDropEvent& e) override {
        const bool accepts = Accepts(e);
        if (accepts != m_hot) {
            m_hot = accepts;
            invalidate();
        }
        return accepts;
    }
    void onDragLeave() override {
        m_hot = false;
        invalidate();
    }
    bool onDrop(const prism::DragDropEvent& e) override {
        m_hot = false;
        invalidate();
        if (!Accepts(e)) {
            return false;
        }
        const std::string* path = e.operation->getPayload<std::string>();
        return path != nullptr && m_shell.DropContentOnOutlinerUVE(*path, e.pos);
    }
    void paint(prism::Canvas& c) override {
        prism::Border::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect(), 3.0F, prism::theme().accent, 2);
        }
    }

private:
    [[nodiscard]] static bool Accepts(const prism::DragDropEvent& e) {
        return e.operation && e.operation->tag() == EditorPrismContentBrowserUVE::kDragTagUVE;
    }
    EditorPrismShellUVE& m_shell;
    bool m_hot = false;
};

} // namespace

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildTitleBarUVE() {
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(12.0F);
    menus->addMenu("File", [this] {
        std::vector<prism::MenuItem> items = CommandItemsUVE({"file.saveScene", "file.saveAll", "file.loadScene", nullptr,
                                                              "file.projectSettings", "file.inputMap", "file.preferences",
                                                              "file.keyboardShortcuts", "file.commandPalette"});
        items.push_back(prism::MenuItem::Item("Save Editor Preferences",
                                              [this] { static_cast<void>(m_editor.SaveSessionSettingsUVE()); }));
        return items;
    });
    menus->addMenu("Edit", [this] {
        return CommandItemsUVE({"edit.undo", "edit.redo", nullptr, "edit.duplicate", "edit.delete"});
    });
    menus->addMenu("Window", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Outliner", m_editor.m_scenePanelVisible, [this] {
            m_editor.m_scenePanelVisible = !m_editor.m_scenePanelVisible;
            UpdateDockColumnsUVE();
        }));
        items.push_back(prism::MenuItem::Check("Details", m_editor.m_inspectorPanelVisible, [this] {
            m_editor.m_inspectorPanelVisible = !m_editor.m_inspectorPanelVisible;
            UpdateDockColumnsUVE();
        }));
        items.push_back(prism::MenuItem::Sub("Layout", {
            prism::MenuItem::Item("Default", [this] { ApplyLayoutPresetUVE(0); }),
            prism::MenuItem::Item("Details under Outliner", [this] { ApplyLayoutPresetUVE(1); }),
            prism::MenuItem::Item("Details beside Outliner", [this] { ApplyLayoutPresetUVE(2); }),
            prism::MenuItem::Item("Wide Viewport", [this] { ApplyLayoutPresetUVE(3); }),
        }, prism::Icon::Frame));
        items.push_back(prism::MenuItem::Item("Reset Panel Layout", [this] { ApplyLayoutPresetUVE(0); }));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Check("Network Capture", m_editor.m_showCaptureWindow,
                                               [this] { m_editor.m_showCaptureWindow = !m_editor.m_showCaptureWindow; }));
        items.push_back(prism::MenuItem::Check("Audio Mixer", m_editor.m_showAudioMixerWindow,
                                               [this] { m_editor.m_showAudioMixerWindow = !m_editor.m_showAudioMixerWindow; }));
        items.push_back(prism::MenuItem::Check("Bottom Dock", m_dockVisible, [this] {
            ShowDockPanelUVE(m_dockPages ? m_dockPages->activeIndex() : 0, false);
        }));
        return items;
    });
    menus->addMenu("Tools", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Project Browser", [this] { ShowDockPanelUVE(0, true); }));
        items.push_back(prism::MenuItem::Item("Import Queue", {}).disabled());
        items.push_back(prism::MenuItem::Sep());
        for (prism::MenuItem& item : CommandItemsUVE({"file.projectSettings", "file.preferences", "file.commandPalette"})) {
            items.push_back(std::move(item));
        }
        return items;
    });
    menus->addMenu("Build", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Build All Levels", {}).disabled(),
            prism::MenuItem::Item("Build Geometry", {}).disabled(),
            prism::MenuItem::Item("Build Lighting Only", {}).disabled(),
            prism::MenuItem::Item("Cook Content for Project", {}).disabled(),
        };
    });
    menus->addMenu("Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("UVE Editor Reference", {}).disabled(),
            prism::MenuItem::Item("About UNIVEX Engine", {}).disabled(),
        };
    });
    row->add(menus, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});

    // - [] x
    AddWindowButtonsUVE(*row, [] { prism::Application::get().quit(); });

    // [#1 logo] [ #15 title row      ]
    //           [ #2 viewport name tab ]
    auto tabRow = prism::hbox();
    auto viewportTab = prism::make<DocumentTabUVE>();
    viewportTab->onClick = [this] {
        const Scene::EntityUVE viewport = m_editor.GetDocumentViewportUVE();
        if (viewport != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(viewport);
        }
    };
    m_viewportNameTab = viewportTab;
    tabRow->add(m_viewportNameTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    // The rest of the tab row is title bar too: it moves the window.
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

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildToolbarRowUVE() {
    // Sub row 1: #4 open folder, #5 save, #3 selection mode | #14 blueprint (beta) | transport.
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));

    // #4 open folder, then #5 save beside #3 selection mode.
    auto openFolder = ToolButtonUVE(prism::Icon::FolderOpen, "Open folder (Content Browser)");
    openFolder->onClick = [this] { ShowDockPanelUVE(0, true); };
    row->add(openFolder, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto save = ToolButtonUVE(prism::Icon::Save, "Save all (Ctrl+Shift+S)");
    save->onClick = [this] { static_cast<void>(m_editor.RunEditorCommandUVE("file.saveAll")); };
    row->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    m_selectionMode = prism::make<prism::ComboBox>(std::vector<std::string>{"Universal", "Move", "Rotate", "Scale"}, 0);
    m_selectionMode->setTooltip("Selection mode");
    m_selectionMode->onChange = [this](const int index) {
        using Mode = EditorUVE::ViewportGizmoModeUVE;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    auto modeBox = prism::make<prism::SizeBox>();
    modeBox->setWidthOverride(150.0F);
    modeBox->setContent(m_selectionMode);
    row->add(modeBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparatorUVE());


    auto blueprint = ToolButtonUVE(prism::Icon::Blueprint, "Blueprint editor (beta)");
    blueprint->onClick = [] {
        if (prism::Application::exists()) {
            prism::Application::get().notify("Blueprint editor (beta) - coming in the blueprint milestone",
                                             prism::ToastLevel::Info);
        }
    };
    row->add(blueprint, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparatorUVE());

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
    m_statusLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    m_statusLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_statusLabel, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

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
    m_outliner->setShowGuides(true);
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
    // Content dragged from the Content Browser lands here, under the row it is dropped on.
    auto drop = prism::make<OutlinerDropUVE>(*this);
    drop->setChild(m_outliner);
    page->add(drop, prism::Slot{1.0F});

    m_outlinerTabs = prism::make<prism::TabView>();
    m_outlinerTabs->addTab("Outliner", prism::Icon::Layers, PanelUVE(page));
    return m_outlinerTabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildViewportUVE() {
    // #25: the viewport itself (its name is tab #2 under the title bar).
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    // Input reaches the viewport through the host's raw state (CaptureViewInputUVE); onInput only
    // makes the view focusable so clicking it takes keyboard focus away from text fields.
    m_viewport->onInput = [](const prism::EngineInput&) {};
    // Every gizmo (navigation, transform, object gizmos and billboards) is PRISM's, drawn over the
    // engine's image of the scene; the renderer draws none of its own (hostDrawsGizmos).
    m_gizmos = prism::make<EditorPrismViewportGizmosUVE>(m_editor);
    auto stack = prism::make<prism::Overlay>();
    stack->add(m_viewport);
    stack->add(m_gizmos);
    return stack;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDetailsUVE() {
    // #17 tab, #18 search, #20 the selected object's components (#21 scroll), drawn from the
    // engine's component metadata (EditorPrismDetailsUVE).
    m_detailsTabs = prism::make<prism::TabView>();
    m_detailsTabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details2.BuildUVE()));
    return m_detailsTabs;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDockUVE() {
    // The dock panel (above the #16 dock bar): one page per dock tab.
    m_dockPages = prism::make<prism::StackSwitcher>();
    m_dockPages->addPage(PanelUVE(m_contentBrowser.BuildUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildOutputLogUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildConsoleUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildProfilerUVE()));
    return m_dockPages;
}

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildDockBarUVE() {
    // #16: the dock bar along the bottom; each tab shows its panel, the active one hides it.
    auto bar = prism::hbox(2.0F);
    bar->setPadding(prism::Margin(4.0F, 1.0F, 8.0F, 1.0F));
    struct DockTabUVE { const char* label; prism::Icon icon; };
    static constexpr std::array<DockTabUVE, 4> kTabs{{
        {"Content Browser", prism::Icon::Folder},
        {"Output Log", prism::Icon::Info},
        {"Console", prism::Icon::Terminal},
        {"Profiler", prism::Icon::Refresh},
    }};
    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        auto tab = prism::make<prism::Button>(kTabs[i].label, kTabs[i].icon, prism::ButtonStyle::Toolbar);
        tab->setCheckable(true);
        tab->setChecked(i == 0);
        tab->onClick = [this, index = static_cast<int>(i)] { ShowDockPanelUVE(index, false); };
        m_dockTabs.push_back(tab);
        bar->add(tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    bar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    border->setChild(bar);
    return border;
}

bool EditorPrismShellUVE::DropContentOnOutlinerUVE(const std::string& relativePath, const prism::Vec2 at) {
    // Entity assets and models dropped on a row go under that row's object (else where a new
    // object goes), through the same calls as the Content Browser's "Place in Scene".
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path relative(relativePath);
    Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
    if (m_outliner) {
        if (const prism::TreeNode* node = m_outliner->nodeAt(at); node != nullptr) {
            parent = EntityForTreeIdUVE(node->id);
        }
    }
    std::string extension = relative.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    Scene::EntityUVE placed = Scene::kInvalidEntityUVE;
    if (extension == ".uventity" || extension == ".uvprefab") {
        placed = m_editor.PlaceEntityAssetUVE(snapshot.contentRoot / relative, parent);
    } else if (EditorUVE::IsModelSourcePathUVE(relative)) {
        placed = m_editor.PlaceModelSourceUVE(relative, parent);
    } else {
        m_editor.m_contentStatusMessage = "Only entity assets and models can be placed in the Outliner.";
        return false;
    }
    if (placed == Scene::kInvalidEntityUVE && m_editor.m_contentStatusMessage.empty()) {
        m_editor.m_contentStatusMessage = "Could not place " + relative.filename().string() + ".";
    }
    return placed != Scene::kInvalidEntityUVE;
}

void EditorPrismShellUVE::ApplyLayoutPresetUVE(const int preset) {
    // Window > Layout: ready-made arrangements of the panels; every panel re-flows at once.
    ResetPanelLayoutUVE();
    if (!m_dockSpace || !m_outlinerTabs || !m_detailsTabs) {
        return;
    }
    const auto moveDetails = [this](const prism::TabView::DockZone zone) {
        const int index = m_detailsTabs->indexOf("Details");
        if (index >= 0) {
            static_cast<void>(m_dockSpace->split(m_outlinerTabs.get(), zone, m_detailsTabs->takeTab(index)));
        }
    };
    switch (preset) {
        case 1: moveDetails(prism::TabView::DockZone::Bottom); break;
        case 2: moveDetails(prism::TabView::DockZone::Right); break;
        case 3:
            m_editor.m_scenePanelVisible = false;
            m_editor.m_inspectorPanelVisible = false;
            if (m_dockVisible) {
                ShowDockPanelUVE(m_dockPages ? m_dockPages->activeIndex() : 0, false);
            }
            break;
        default:
            if (!m_dockVisible) {
                ShowDockPanelUVE(m_dockPages ? m_dockPages->activeIndex() : 0, true);
            }
            break;
    }
    UpdateDockColumnsUVE();
}

void EditorPrismShellUVE::UpdateDockColumnsUVE() {
    if (m_outlinerColumn && m_outlinerTabs) {
        m_outlinerColumn->setVisible(m_editor.m_scenePanelVisible && m_outlinerTabs->count() > 0);
    }
    if (m_detailsColumn && m_detailsTabs) {
        m_detailsColumn->setVisible(m_editor.m_inspectorPanelVisible && m_detailsTabs->count() > 0);
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorPrismShellUVE::ResetPanelLayoutUVE() {
    if (!m_dockSpace || !m_outlinerTabs || !m_detailsTabs) {
        return;
    }
    // Every split-off or floating panel back into the Details area first, then each panel to its own.
    m_dockSpace->dissolve(*m_detailsTabs);
    const auto moveTo = [](prism::TabView& from, prism::TabView& to, const std::string& title) {
        const int index = from.indexOf(title);
        if (index >= 0) {
            to.insertTab(to.count(), from.takeTab(index));
        }
    };
    moveTo(*m_detailsTabs, *m_outlinerTabs, "Outliner");
    moveTo(*m_outlinerTabs, *m_detailsTabs, "Details");
    m_outlinerTabs->setCurrent(0);
    m_detailsTabs->setCurrent(0);
    m_editor.m_scenePanelVisible = true;
    m_editor.m_inspectorPanelVisible = true;
    UpdateDockColumnsUVE();
}

void EditorPrismShellUVE::ShowDockPanelUVE(const int index, const bool forceVisible) {
    if (!m_dockPages) {
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
    for (std::size_t i = 0; i < m_dockTabs.size(); ++i) {
        m_dockTabs[i]->setChecked(m_dockVisible && static_cast<int>(i) == m_dockPages->activeIndex());
    }
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
        prism::MenuItem item = prism::MenuItem::Item(
            entry.label,
            [this, kind = entry.kind] { static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind)); }, "",
            entry.icon);
        // The engine's own object icon.
        if (const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(entry.kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
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
        prism::MenuItem item = prism::MenuItem::Check(kAddEntriesUVE[i].label, on, [this, key] {
            if (m_addKinds.count(key) > 0) {
                m_addKinds.erase(key);
            } else {
                m_addKinds.insert(key);
            }
        });
        if (const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(kAddEntriesUVE[i].kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        items.push_back(std::move(item));
    }
    prism::showMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{},
                    prism::PopupPlacement::Below);
}

// ---------------------------------------------------------------------------------------- refresh

std::vector<Scene::EntityUVE> EditorPrismShellUVE::OutlinerRootsUVE() {
    // The scene root is structural and stays out of the Outliner, as it always has: when the
    // document has its Viewport, the rows start at the scene root's children (the Viewport and
    // anything beside it); a document without one lists its roots.
    if (m_editor.GetDocumentViewportUVE() != Scene::kInvalidEntityUVE) {
        const Scene::EntityUVE sceneRoot = m_editor.GetDocumentSceneRootUVE();
        if (sceneRoot != Scene::kInvalidEntityUVE) {
            return m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(m_editor.m_services->GetEntityManagerUVE(),
                                                                         sceneRoot);
        }
    }
    return m_editor.GetDocumentRootsUVE();
}

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
    for (const Scene::EntityUVE root : OutlinerRootsUVE()) {
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
        prism::TreeNode* node = m_outliner->addNode(parent, m_editor.GetEntityDisplayLabelUVE(e), prism::Icon::Dot, id);
        // The engine's own icon for the object type (a folder in golden dark brown).
        if (const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(Scene::ResolveSceneObjectKindUVE(em, e), 16);
            icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(typeName);
        if (m_editor.IsEntitySelectedUVE(e)) {
            selected.push_back(id);
        }
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::EntityUVE root : OutlinerRootsUVE()) {
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
    m_details2.RefreshUVE();
}

void EditorPrismShellUVE::RefreshToolbarUVE() {
    const EditorPlayModeStateUVE play = m_editor.GetPlayModeStateUVE();
    if (m_playButton) {
        m_playButton->setChecked(play == EditorPlayModeStateUVE::Playing);
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(play == EditorPlayModeStateUVE::Paused);
    }
    m_viewToolbar.RefreshUVE();
    using Mode = EditorUVE::ViewportGizmoModeUVE;
    int mode = 0;
    switch (m_editor.m_viewportOverlayState.gizmoMode) {
        case Mode::Universal: mode = 0; break;
        case Mode::Move: mode = 1; break;
        case Mode::Rotate: mode = 2; break;
        case Mode::Scale: mode = 3; break;
    }
    if (m_selectionMode && m_selectionMode->selected() != mode) {
        m_selectionMode->setSelected(mode);
    }
    if (m_viewportNameTab) {
        // The same Viewport object the Outliner shows: its own name and its own icon.
        auto* tab = static_cast<DocumentTabUVE*>(m_viewportNameTab.get());
        const Scene::EntityUVE viewport = m_editor.GetDocumentViewportUVE();
        if (viewport != Scene::kInvalidEntityUVE) {
            tab->setName(m_editor.GetEntityDisplayLabelUVE(viewport));
            const EditorPrismIconsUVE::ImageUVE icon = m_icons.ObjectUVE(
                Scene::ResolveSceneObjectKindUVE(m_editor.m_services->GetEntityManagerUVE(), viewport), 16);
            tab->setIcon(icon.pixels, icon.size);
        } else {
            const std::string stem = m_editor.m_activeScenePath.stem().string();
            tab->setName(stem.empty() ? "Viewport" : stem);
            tab->setIcon(nullptr, 0);
        }
    }
    if (m_statusLabel) {
        const char* const state = play == EditorPlayModeStateUVE::Edit      ? "Editing"
                                  : play == EditorPlayModeStateUVE::Playing ? "Playing"
                                                                            : "Paused";
        std::string text = m_editor.m_sceneDirty ? "unsaved" : "saved";
        text += " | " + std::to_string(m_editor.m_selectedEntities.size()) + " selected | ";
        text += state;
        text += "    UVE Editor 0.1";
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
    m_contentBrowser.RefreshUVE();
    {
        const int page = m_dockPages ? m_dockPages->activeIndex() : 0;
        m_dockPanels.RefreshUVE(m_dockVisible && page == 2, m_dockVisible && page == 3);
    }
    // The Entity Editor's window follows the entity session; its panels read the entity's document.
    m_entityEditor.SyncUVE();
    {
        const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
        m_entityEditor.RefreshUVE();
    }
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
            state.hostDrawsGizmos = true;
            if (m_gizmos) {
                const std::array<float, 2> orbit = m_gizmos->TakeOrbitRequestUVE();
                state.orbitRequestX = orbit[0];
                state.orbitRequestY = orbit[1];
            }
            state.input = m_host.CaptureViewInputUVE(rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.m_viewportPanelRenderer(
                EditorUVE::ViewportContextUVE::Main, Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used,
                state);
            m_host.ApplyViewCursorUVE(state.input);
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
    m_host.RenderUVE(views);
    {
        const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
        m_entityEditor.RenderUVE();
    }
}

} // namespace UVE::Editor
