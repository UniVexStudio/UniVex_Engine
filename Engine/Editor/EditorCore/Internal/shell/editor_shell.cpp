// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_shell.h"
#include "prism/AssetBrowser.h"
#include "png_metadata.h"
#include "editor_object_menu.h"
#include "i_asset_database.h"
#include "prefab_instance_component.h"

#include "editor_chrome.h"

#include "editor.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Menu.h"
#include "prism/SearchMenu.h"
#include "editor_add_objects.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Docking.h"
#include "prism/Layouts.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
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
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace UVE::Editor {
namespace {

using SceneObjectKindUVE = Scene::Objects::SceneObjectKindUVE;


void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

/// The picture every thumbnail sits on (a dark checker), from the editor's assets.
void LoadThumbnailBackdropUVE(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return;
    }
    std::vector<std::byte> bytes;
    for (char ch; in.get(ch);) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    Asset::PngRgba8ImageUVE image;
    if (!Asset::DecodePngRgba8ImageUVE(bytes, image) || image.width == 0U || image.height == 0U) {
        return;
    }
    const auto* const rgba = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t i = 0U; i < argb.size(); ++i) {
        argb[i] = 0xFF000000U | (static_cast<std::uint32_t>(rgba[i * 4U]) << 16U) |
                  (static_cast<std::uint32_t>(rgba[i * 4U + 1U]) << 8U) | static_cast<std::uint32_t>(rgba[i * 4U + 2U]);
    }
    prism::setThumbnailBackdrop(std::move(argb), static_cast<int>(image.width), static_cast<int>(image.height));
}

} // namespace

EditorShellUVE::EditorShellUVE(EditorUVE& editor)
    : m_editor(editor), m_entityEditor(editor, m_host), m_fileDialog(editor, m_host), m_entityClassWindow(editor, m_host), m_montageEditor(editor, m_host), m_retargetEditor(editor, m_host), m_animationLibraryEditor(editor, m_host), m_contentBrowser(editor), m_details2(editor), m_dockPanels(editor),
      m_viewToolbar(editor) {
    m_entityEditor.onBrowse = [this](const std::filesystem::path& path) {
        ShowDockPanelUVE(0, true);
        m_contentBrowser.RevealFileUVE(path);
        m_host.FocusWindowUVE(EditorHostUVE::kMainWindowUVE);
    };
}

bool EditorShellUVE::SaveEntityEditorScreenshotUVE(const std::filesystem::path& path) {
    const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
    return m_entityEditor.SaveScreenshotUVE(path);
}

EditorShellUVE::~EditorShellUVE() {
    ShutdownUVE();
}

bool EditorShellUVE::InitializeUVE(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (!m_host.InitializeUVE(window, assetDir)) {
        return false;
    }
    LoadThumbnailBackdropUVE(assetDir / "images" / "thumbnail_backdrop.png");
    m_dockPanels.AttachLogUVE();
    m_editor.SetFilePickerHostUVE([this](FilePickerRequestUVE request) { m_fileDialog.OpenUVE(std::move(request)); });
    m_host.SetRootUVE(BuildLayoutUVE());
    // Every keyboard shortcut is an editor command's (the same registry the menus use).
    m_host.AppUVE().setShortcutHandler([this](const prism::Key key, const std::uint8_t mods) {
        return DispatchShortcutUVE(key, mods);
    });
    return true;
}

void EditorShellUVE::ShutdownUVE() {
    m_editor.SetFilePickerHostUVE({});
    if (m_host.IsReadyUVE() && m_fileDialog.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_fileDialog.WindowIdUVE());
    }
    if (m_host.IsReadyUVE() && m_entityEditor.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_entityEditor.WindowIdUVE());
    }
    if (m_host.IsReadyUVE() && m_entityClassWindow.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_entityClassWindow.WindowIdUVE());
    }
    if (m_host.IsReadyUVE() && m_montageEditor.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_montageEditor.WindowIdUVE());
    }
    if (m_host.IsReadyUVE() && m_retargetEditor.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_retargetEditor.WindowIdUVE());
    }
    if (m_host.IsReadyUVE() && m_animationLibraryEditor.WindowIdUVE() != 0) {
        m_host.CloseWindowUVE(m_animationLibraryEditor.WindowIdUVE());
    }
    m_host.ShutdownUVE();
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorShellUVE::BuildLayoutUVE() {
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
    // The loading screen over everything while the editor starts (and a corner panel after).
    auto root = prism::make<prism::Overlay>();
    root->add(frame);
    m_loadingScreen = prism::make<EditorLoadingScreenUVE>();
    m_loadingScreen->onDismiss = [this] { m_editor.DismissStartupLoadingUVE(); };
    root->add(m_loadingScreen);
    return root;
}

std::vector<prism::MenuItem> EditorShellUVE::CommandItemsUVE(const std::initializer_list<const char*> ids) {
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

bool EditorShellUVE::DispatchShortcutUVE(const prism::Key key, const std::uint8_t mods) {
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
    explicit OutlinerDropUVE(EditorShellUVE& shell) : m_shell(shell) {}
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
        return e.operation && e.operation->tag() == EditorContentBrowserUVE::kDragTagUVE;
    }
    EditorShellUVE& m_shell;
    bool m_hot = false;
};

} // namespace

prism::Ref<prism::Widget> EditorShellUVE::BuildTitleBarUVE() {
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
        return CommandItemsUVE({"edit.undo", "edit.redo", nullptr, "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete"});
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

prism::Ref<prism::Widget> EditorShellUVE::BuildToolbarRowUVE() {
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


prism::Ref<prism::Widget> EditorShellUVE::BuildOutlinerUVE() {
    // #6 tab, #7 '+', #8 filter, #10 search, #11 tree (#12 its scrollbar).
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));

    auto bar = prism::hbox(4.0F);
    // A folder in one click, and the level's objects (Basic, Lights, Shapes, ...) under one button.
    m_addButton = ToolButtonUVE(prism::Icon::Folder, "New Folder");
    m_addButton->onClick = [this] {
        static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(SceneObjectKindUVE::Folder));
    };
    m_filterButton = ToolButtonUVE(prism::Icon::Cube, "Place an object in the level");
    m_filterButton->onClick = [this] { ShowAddMenuUVE(); };
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
    m_outliner->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        const Scene::EntityUVE entity = node != nullptr ? EntityForTreeIdUVE(node->id) : Scene::kInvalidEntityUVE;
        EditorObjectMenuUVE::HooksUVE hooks;
        hooks.tree = m_outliner.get();
        hooks.addChild = [this] { ShowAddMenuUVE(); };
        EditorObjectMenuUVE::ShowUVE(m_editor, entity, node, at, hooks);
    };
    // Double-click a placed entity: it opens in the Entity Editor (its inside is edited there).
    m_outliner->onActivate = [this](prism::TreeNode* node) {
        const Scene::EntityUVE entity = node != nullptr ? EntityForTreeIdUVE(node->id) : Scene::kInvalidEntityUVE;
        if (!m_editor.IsSealedHierarchyRowUVE(entity)) {
            return;
        }
        const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        const Asset::AssetGuidUVE guid = em.GetComponentUVE<Scene::PrefabInstanceComponentUVE>(entity).sourcePrefabGuid;
        const std::filesystem::path source = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(guid);
        if (!source.empty()) {
            static_cast<void>(m_editor.OpenEntityEditorUVE(source));
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

prism::Ref<prism::Widget> EditorShellUVE::BuildViewportUVE() {
    // #25: the viewport itself (its name is tab #2 under the title bar).
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    // Input reaches the viewport through the host's raw state (CaptureViewInputUVE); onInput only
    // makes the view focusable so clicking it takes keyboard focus away from text fields.
    m_viewport->onInput = [](const prism::EngineInput&) {};
    // Every gizmo (navigation, transform, object gizmos and billboards) is PRISM's, drawn over the
    // engine's image of the scene; the renderer draws none of its own (hostDrawsGizmos).
    m_gizmos = prism::make<EditorViewportGizmosUVE>(m_editor);
    m_cameraScreen = prism::make<CameraScreenUVE>();
    m_cameraScreen->setVisible(false);
    auto stack = prism::make<prism::Overlay>();
    stack->add(m_viewport);
    stack->add(m_gizmos);
    stack->add(m_cameraScreen);
    return stack;
}

prism::Ref<prism::Widget> EditorShellUVE::BuildDetailsUVE() {
    // #17 tab, #18 search, #20 the selected object's components (#21 scroll), drawn from the
    // engine's component metadata (EditorDetailsUVE).
    m_detailsTabs = prism::make<prism::TabView>();
    m_detailsTabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details2.BuildUVE()));
    return m_detailsTabs;
}

prism::Ref<prism::Widget> EditorShellUVE::BuildDockUVE() {
    // The dock panel (above the #16 dock bar): one page per dock tab.
    m_dockPages = prism::make<prism::StackSwitcher>();
    m_dockPages->addPage(PanelUVE(m_contentBrowser.BuildUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildOutputLogUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildConsoleUVE()));
    m_dockPages->addPage(PanelUVE(m_dockPanels.BuildProfilerUVE()));
    return m_dockPages;
}

prism::Ref<prism::Widget> EditorShellUVE::BuildDockBarUVE() {
    // #16: the dock bar along the bottom; each tab shows its panel, the active one hides it.
    auto bar = prism::hbox(2.0F);
    bar->setPadding(prism::Margin(4.0F, 1.0F, 8.0F, 1.0F));
    struct DockTabUVE { const char* label; prism::Icon icon; };
    static constexpr std::array<DockTabUVE, 4> kTabs{{
        {"Content Browser", prism::Icon::AssetBrowser},
        {"Output Log", prism::Icon::OutputLog},
        {"Console", prism::Icon::Terminal},
        {"Profiler", prism::Icon::Profiler},
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

bool EditorShellUVE::DropContentOnOutlinerUVE(const std::string& relativePath, const prism::Vec2 at) {
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

void EditorShellUVE::ApplyLayoutPresetUVE(const int preset) {
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

void EditorShellUVE::UpdateDockColumnsUVE() {
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

void EditorShellUVE::ResetPanelLayoutUVE() {
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

void EditorShellUVE::ShowDockPanelUVE(const int index, const bool forceVisible) {
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


void EditorShellUVE::ShowAddMenuUVE() {
    // GET CONTENT, PLACE OBJECTS (a flyout per group) and RECENTLY PLACED; typing searches them all.
    // Only what a level holds: a character's parts are built in an Entity, and an entity of the
    // project (a player, a character) is offered under Basic once it is in Content.
    const auto place = [this](const SceneObjectKindUVE kind) {
        static_cast<void>(m_editor.CreateDocumentSceneObjectUVE(kind));
        const int key = static_cast<int>(kind);
        m_recentKinds.erase(std::remove(m_recentKinds.begin(), m_recentKinds.end(), key), m_recentKinds.end());
        m_recentKinds.insert(m_recentKinds.begin(), key);
        if (m_recentKinds.size() > 5U) {
            m_recentKinds.resize(5U);
        }
    };
    const auto itemFor = [this, &place](const SceneObjectKindUVE kind, std::string label = {}) {
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(kind);
        if (label.empty()) {
            label = descriptor != nullptr ? std::string(descriptor->displayName) : std::string("Object");
        }
        prism::MenuItem item = prism::MenuItem::Item(label, [place, kind] { place(kind); }, "", ObjectKindGlyphUVE(kind));
        if (const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        return item;
    };
    const auto offered = [](const SceneObjectKindUVE kind) {
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(kind);
        return descriptor != nullptr && descriptor->libraryCreatable && IsObjectOfferedUVE(kind, AddObjectContextUVE::Level);
    };
    std::set<int> grouped;
    const auto group = [&](const char* const title, const prism::Icon icon, std::initializer_list<SceneObjectKindUVE> kinds,
                           std::vector<prism::MenuItem> extra = {}) {
        std::vector<prism::MenuItem> sub = std::move(extra);
        for (const SceneObjectKindUVE kind : kinds) {
            if (offered(kind)) {
                sub.push_back(itemFor(kind));
                grouped.insert(static_cast<int>(kind));
            }
        }
        return prism::MenuItem::Sub(title, std::move(sub), icon);
    };

    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("GET CONTENT"));
    items.push_back(prism::MenuItem::Item("Import Content...", [this] {
        static_cast<void>(m_editor.PromptImportFilesUVE(m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot));
    }, "", prism::Icon::Open));
    items.push_back(prism::MenuItem::Header("PLACE OBJECTS"));

    // The project's own entities (a player, a character): placed as they are in Content.
    std::vector<prism::MenuItem> entities;
    {
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
            if (entry.kind != Asset::ProjectFileEntryKindUVE::File || entry.relativePath.extension() != ".uventity") {
                continue;
            }
            const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
            entities.push_back(prism::MenuItem::Item(entry.relativePath.stem().string(), [this, absolute] {
                static_cast<void>(m_editor.PlaceEntityAssetUVE(absolute));
            }, "", prism::Icon::PreviewCharacter));
        }
    }
    items.push_back(group("Basic", prism::Icon::Object3D,
                          {SceneObjectKindUVE::Object3D, SceneObjectKindUVE::Marker3D, SceneObjectKindUVE::SpawnPoint3D,
                           SceneObjectKindUVE::Camera3D, SceneObjectKindUVE::Light3D, SceneObjectKindUVE::Area3D},
                          std::move(entities)));
    items.push_back(group("Lights", prism::Icon::Light,
                          {SceneObjectKindUVE::DirectionalLight3D, SceneObjectKindUVE::Light3D, SceneObjectKindUVE::WorldEnvironment3D,
                           SceneObjectKindUVE::Sky, SceneObjectKindUVE::ReflectionProbe3D}));
    items.push_back(group("Shapes", prism::Icon::Cube,
                          {SceneObjectKindUVE::BoxMesh3D, SceneObjectKindUVE::SphereMesh3D, SceneObjectKindUVE::PlaneMesh3D,
                           SceneObjectKindUVE::MeshInstance3D, SceneObjectKindUVE::LODGroup3D}));
    items.push_back(group("Cinematic", prism::Icon::Camera,
                          {SceneObjectKindUVE::Camera3D, SceneObjectKindUVE::SpringArm3D, SceneObjectKindUVE::AnimationSequencer}));
    items.push_back(group("Visual Effects", prism::Icon::Environment,
                          {SceneObjectKindUVE::ParticleEmitter3D, SceneObjectKindUVE::Decal3D, SceneObjectKindUVE::FogVolume3D}));
    items.push_back(group("Volumes", prism::Icon::Collider,
                          {SceneObjectKindUVE::Area3D, SceneObjectKindUVE::Collider3D, SceneObjectKindUVE::NavMeshVolume3D,
                           SceneObjectKindUVE::LevelStreamer3D, SceneObjectKindUVE::WorldPartition3D, SceneObjectKindUVE::Occluder3D,
                           SceneObjectKindUVE::VisibilityRegion3D}));
    // Kept for what comes next; nothing in it yet.
    items.push_back(prism::MenuItem::Sub("More", {prism::MenuItem::Item("Nothing here yet", {}).disabled()}, prism::Icon::Plus));
    if (!m_recentKinds.empty()) {
        items.push_back(prism::MenuItem::Header("RECENTLY PLACED"));
        for (const int key : m_recentKinds) {
            items.push_back(itemFor(static_cast<SceneObjectKindUVE>(key)));
        }
    }
    prism::showSearchMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search objects", true);
}

// ---------------------------------------------------------------------------------------- refresh

std::vector<Scene::EntityUVE> EditorShellUVE::OutlinerRootsUVE() {
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

int EditorShellUVE::TreeIdForUVE(const Scene::EntityUVE entity) {
    m_treeEntities.push_back(entity);
    return static_cast<int>(m_treeEntities.size());
}

Scene::EntityUVE EditorShellUVE::EntityForTreeIdUVE(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntityUVE;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

std::uint64_t EditorShellUVE::OutlinerSignatureUVE() {
    std::uint64_t h = 1469598103934665603ULL;
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::function<void(Scene::EntityUVE, int)> visit = [&](const Scene::EntityUVE e, const int depth) {
        HashUVE(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
        HashUVE(h, static_cast<std::uint64_t>(depth));
        HashUVE(h, m_editor.GetEntityDisplayLabelUVE(e));
        HashUVE(h, m_editor.IsEntitySelectedUVE(e) ? 1U : 0U);
        if (m_editor.IsSealedHierarchyRowUVE(e)) {
            return; // a placed entity is one row: its inside is the Entity Editor's
        }
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            visit(child, depth + 1);
        }
    };
    for (const Scene::EntityUVE root : OutlinerRootsUVE()) {
        visit(root, 0);
    }
    for (const Scene::EntityUVE e : m_editor.GetSelectedEntitiesUVE()) {
        HashUVE(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
    }
    return h;
}

void EditorShellUVE::RefreshOutlinerUVE() {
    if (!m_outliner) {
        return;
    }
    // Walking the whole level for its signature is done a dozen times a second, not every frame;
    // a change of selection is taken at once (the tree shows it).
    {
        std::uint64_t selection = 1469598103934665603ULL;
        for (const Scene::EntityUVE e : m_editor.m_selectedEntities) {
            selection = (selection ^ ((static_cast<std::uint64_t>(e.index) << 32U) | e.generation)) * 1099511628211ULL;
        }
        const auto now = std::chrono::steady_clock::now();
        if (selection == m_outlinerSelection && now - m_outlinerCheckedAt < std::chrono::milliseconds(80)) {
            return;
        }
        m_outlinerSelection = selection;
        m_outlinerCheckedAt = now;
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
    std::unordered_map<std::uint64_t, int> rowOf;
    const std::function<void(prism::TreeNode*, Scene::EntityUVE)> add = [&](prism::TreeNode* parent,
                                                                           const Scene::EntityUVE e) {
        const int id = TreeIdForUVE(e);
        const std::string_view typeName = m_editor.GetObjectTypeNameUVE(e);
        prism::TreeNode* node = m_outliner->addNode(parent, m_editor.GetEntityDisplayLabelUVE(e), prism::Icon::Dot, id);
        // The engine's own icon for the object type (a folder in golden dark brown).
        if (const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(Scene::ResolveSceneObjectKindUVE(em, e), 16);
            icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(typeName);
        rowOf[(static_cast<std::uint64_t>(e.index) << 32U) | e.generation] = id;
        // A placed entity is one row, like a mesh is one object: what is inside it belongs to the
        // Entity Editor (double-click or Open Entity), not to the level's tree.
        if (m_editor.IsSealedHierarchyRowUVE(e)) {
            return;
        }
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::EntityUVE root : OutlinerRootsUVE()) {
        add(nullptr, root);
    }
    // Something selected inside a placed entity shows as its entity's row.
    for (const Scene::EntityUVE e : m_editor.GetSelectedEntitiesUVE()) {
        const Scene::EntityUVE owner = m_editor.ResolveHierarchyRowOwnerUVE(e);
        const auto row = rowOf.find((static_cast<std::uint64_t>(owner.index) << 32U) | owner.generation);
        if (row != rowOf.end() && std::find(selected.begin(), selected.end(), row->second) == selected.end()) {
            selected.push_back(row->second);
        }
    }
    m_outliner->setSelection(selected);
    if (m_outlinerSearch) {
        m_outliner->setFilter(m_outlinerSearch->text());
    }
    m_outliner->refresh();
    m_syncingSelection = false;
}

void EditorShellUVE::RefreshDetailsUVE() {
    m_details2.RefreshUVE();
}

void EditorShellUVE::RefreshToolbarUVE() {
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
            const EditorIconsUVE::ImageUVE icon = m_icons.ObjectUVE(
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

void EditorShellUVE::AppendCameraScreenUVE(EditorUVE& editor, const EditorDetailsUVE& details,
                                                CameraScreenUVE* screen,
                                                std::vector<EditorHostUVE::ViewImageUVE>& views) {
    // A selected Camera3D's view in the viewport's corner, while the view itself stays the editor's
    // (not while the viewport already looks through that camera).
    if (screen == nullptr) {
        return;
    }
    const Scene::EntityUVE selected = editor.GetSelectedEntityUVE();
    const bool show = details.CameraScreenOpenUVE() && editor.m_viewportPanelRenderer && editor.m_selectedEntities.size() == 1U &&
                      Scene::IsDocumentCameraEntityUVE(editor.m_services->GetEntityManagerUVE(), selected) &&
                      editor.GetPreviewCameraUVE() != selected;
    if (screen->visible() != show) {
        screen->setVisible(show);
    }
    if (!show) {
        return;
    }
    screen->setTitle("Camera3D: " + editor.GetEntityDisplayLabelUVE(selected));
    const prism::Rect rect = screen->viewRect();
    if (rect.w < 8.0F || rect.h < 8.0F) {
        return;
    }
    static EditorUVE::ViewportOverlayStateUVE state{};
    state.hostDrawsGizmos = true;
    state.gameWorkspaceActive = false;
    state.studioView = false;
    state.entityAuthoringView = editor.IsEntityDocumentActiveUVE();
    state.input = EditorViewportInputUVE{};
    Math::Vector2UVE used{0.0F, 0.0F};
    const std::uint64_t texture = editor.m_viewportPanelRenderer(EditorUVE::ViewportContextUVE::InspectorCameraPreview,
                                                                 Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)},
                                                                 used, state);
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

void EditorShellUVE::FrameUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    // The editor's own work per frame (UI, views, panels), logged every five seconds: what a slow
    // machine pays for the editor itself, apart from waiting on the display.
    const auto frameStart = std::chrono::steady_clock::now();
    struct FrameTimerUVE final {
        EditorShellUVE& shell;
        std::chrono::steady_clock::time_point start;
        ~FrameTimerUVE() {
            const auto end = std::chrono::steady_clock::now();
            shell.m_frameWorkSeconds += std::chrono::duration<double>(end - start).count();
            ++shell.m_frameCount;
            if (shell.m_frameReportAt == std::chrono::steady_clock::time_point{}) {
                shell.m_frameReportAt = end;
            }
            if (end - shell.m_frameReportAt >= std::chrono::seconds(5) && shell.m_frameCount > 0U) {
                const double seconds = std::chrono::duration<double>(end - shell.m_frameReportAt).count();
                UVE_INFO("Editor: {:.1f} frames/s, {:.2f} ms of editor work per frame", static_cast<double>(shell.m_frameCount) / seconds,
                         shell.m_frameWorkSeconds * 1000.0 / static_cast<double>(shell.m_frameCount));
                shell.m_frameReportAt = end;
                shell.m_frameWorkSeconds = 0.0;
                shell.m_frameCount = 0U;
            }
        }
    } frameTimer{*this, frameStart};
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
    m_montageEditor.SyncUVE();
    m_retargetEditor.SyncUVE();
    m_animationLibraryEditor.SyncUVE();
    {
        const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
        m_entityEditor.RefreshUVE();
    }
    if (m_loadingScreen) {
        const EditorUVE::EditorLoadingStateUVE loading = m_editor.GetLoadingStateUVE();
        if (loading.startup && m_projectName.empty() && m_editor.m_projectFileSnapshotInitialized) {
            // The project's folder: the one that holds its content folder.
            std::error_code error;
            std::filesystem::path content = std::filesystem::absolute(m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot, error);
            if (content.filename().empty()) {
                content = content.parent_path();
            }
            m_projectName = content.parent_path().filename().string();
        }
        m_loadingScreen->SyncUVE(loading, m_projectName);
        m_startupCovered = loading.startup;
    }
    // The level view starts gently: nothing while the loading screen covers it, then a frame now
    // and then, more often each second, until it draws every frame (about three seconds), so the
    // device is not hit with the full scene the moment the editor appears.
    bool renderView = !m_startupCovered;
    // While the Entity Editor's own window has focus its view is the one being looked at: the level
    // view keeps its last frame instead of drawing the whole scene again behind it.
    const bool entityEditorFocused = m_entityEditor.WindowIdUVE() != 0 && m_host.IsWindowFocusedUVE(m_entityEditor.WindowIdUVE());
    if (renderView) {
        const auto now = std::chrono::steady_clock::now();
        if (m_viewWarmBegan == std::chrono::steady_clock::time_point{}) {
            m_viewWarmBegan = now;
        }
        const double warm = std::chrono::duration<double>(now - m_viewWarmBegan).count();
        const int every = warm < 1.0 ? 4 : warm < 2.0 ? 3 : warm < 3.0 ? 2 : 1;
        renderView = m_lastView.texture == 0U || (++m_viewWarmFrame % every) == 0;
        // "Loading render" over the view: until the warm-up is through and the renderer has
        // nothing left to load or send to the GPU (it sends a few meshes a frame), with the names
        // of the meshes it is drawing now.
        Render::IRenderer3DUVE& renderer = m_editor.m_services->GetRenderer3DUVE();
        if (!m_uploadBudgetSet) {
            renderer.SetMeshUploadBudgetUVE(2U);
            m_uploadBudgetSet = true;
        }
        const Render::Renderer3DFrameDiagnosticsUVE frame = renderer.GetLastFrameDiagnosticsUVE();
        const std::size_t loading = frame.pendingAssetLoads + frame.primitiveMaterialPending + frame.meshUploadsDeferred;
        if (warm < 0.05) {
            m_renderLoadPeak = 0U;
            m_renderLoadDone = false;
            m_renderLoadNames.clear();
        }
        // Names of the meshes just drawn for the first time (an imported part reads as its part name).
        for (const Asset::AssetGuidUVE guid : frame.uploadedMeshes) {
            if (guid == Asset::AssetGuidUVE{}) {
                continue;
            }
            std::string name = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(guid).stem().string();
            if (const std::size_t part = name.find(".part-"); part != std::string::npos) {
                name = name.substr(part + 6U);
            }
            if (!name.empty() && (m_renderLoadNames.empty() || m_renderLoadNames.front() != name)) {
                m_renderLoadNames.insert(m_renderLoadNames.begin(), name);
                if (m_renderLoadNames.size() > 2U) {
                    m_renderLoadNames.resize(2U);
                }
            }
        }
        m_renderLoadPeak = std::max(m_renderLoadPeak, loading);
        if (!m_renderLoadDone && warm >= 3.0 && loading == 0U) {
            m_renderLoadDone = true;
        }
        if (m_loadingScreen && m_viewport) {
            const float warmPart = static_cast<float>(std::min(warm / 3.0, 1.0));
            const float assetPart = m_renderLoadPeak > 0U
                                        ? static_cast<float>(m_renderLoadPeak - std::min(loading, m_renderLoadPeak)) / static_cast<float>(m_renderLoadPeak)
                                        : 1.0F;
            const std::string text = loading > 0U ? "Loading render (" + std::to_string(loading) + " left)" : "Loading render";
            std::string detail;
            for (const std::string& name : m_renderLoadNames) {
                detail += (detail.empty() ? "Rendering " : ", ") + name;
            }
            m_loadingScreen->SyncRenderUVE(!m_renderLoadDone && m_viewport->visible(), m_viewport->rect(), text, detail,
                                           std::min(warmPart, assetPart));
        }
    }
    if (m_loadingScreen && !m_startupCovered) {
        m_loadingScreen->SyncUVE(m_editor.GetLoadingStateUVE(), m_projectName);
    }
    m_host.BeginFrameUVE();

    std::vector<EditorHostUVE::ViewImageUVE> views;
    if (entityEditorFocused && m_lastView.texture != 0U) {
        renderView = false;
    }
    if (!renderView && !m_startupCovered && m_viewport && m_viewport->visible() && m_lastView.texture != 0U) {
        views.push_back(m_lastView); // the last frame again, between warm-up frames
    } else if (renderView && m_viewport && m_viewport->visible() && m_editor.m_viewportPanelRenderer) {
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
                EditorHostUVE::ViewImageUVE view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x, used.y};
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
                m_lastView = view;
            }
        }
    }
    AppendCameraScreenUVE(m_editor, m_details2, m_cameraScreen.get(), views);
    m_host.RenderUVE(views);
    {
        const EditorUVE::EntityDocumentScopeUVE scope(m_editor);
        m_entityEditor.RenderUVE();
    }
    m_montageEditor.RenderUVE();
    m_retargetEditor.RenderUVE();
    m_animationLibraryEditor.RenderUVE();
    m_fileDialog.FrameUVE();
    m_entityClassWindow.FrameUVE();
}

} // namespace UVE::Editor
