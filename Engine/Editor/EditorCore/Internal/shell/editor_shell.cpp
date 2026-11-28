// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_shell.h"
#include "editor_layout_store.h"
#include "editor_shortcut_keys.h"
#include "prism/AssetBrowser.h"
#include "png_metadata.h"
#include "editor_object_menu.h"
#include "i_asset_database.h"
#include "prefab_instance_component.h"

#include "editor_chrome.h"

#include "editor.h"
#include "engine_core.h"
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

using SceneObjectKind = Scene::Objects::SceneObjectKind;


void Hash(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void Hash(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        Hash(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

/// The picture every thumbnail sits on (a dark checker), from the editor's assets.
void LoadThumbnailBackdrop(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return;
    }
    std::vector<std::byte> bytes;
    for (char ch; in.get(ch);) {
        bytes.push_back(static_cast<std::byte>(ch));
    }
    Asset::PngRgba8Image image;
    if (!Asset::DecodePngRgba8Image(bytes, image) || image.width == 0U || image.height == 0U) {
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

EditorShell::EditorShell(Editor& editor)
    : m_editor(editor), m_entityEditor(editor, m_host), m_fileDialog(editor, m_host), m_entityClassWindow(editor, m_host),
      m_preferencesWindow(editor, m_host, editor.m_preferencesWindow.visible,
                          [&editor] {
                              EditorSettingsWindow::Source source;
                              source.title = "Editor Preferences";
                              source.registry = &editor.m_settingsRegistry;
                              source.get = [&editor](const std::string_view id) { return editor.GetEditorSetting(id); };
                              source.set = [&editor](const std::string_view id, const Config::SettingValue& value) {
                                  return editor.SetEditorSetting(id, value);
                              };
                              source.note = [] { return std::string("Changes apply at once and are kept with your editor session."); };
                              return source;
                          },
                          &editor.m_preferencesWindow.focusPending),
      m_projectSettingsWindow(editor, m_host, editor.m_projectSettingsWindow.visible,
                              [&editor] {
                                  Config::SettingsDocument& project = editor.m_services->GetProjectSettings();
                                  EditorSettingsWindow::Source source;
                                  source.title = "Project Settings";
                                  source.registry = &project.GetRegistry();
                                  source.get = [&project](const std::string_view id) { return project.GetValue(id); };
                                  source.set = [&project](const std::string_view id, const Config::SettingValue& value) {
                                      return project.SetValue(id, value);
                                  };
                                  source.note = [&project] {
                                      const std::string file = project.GetPath().filename().string();
                                      return project.IsDirty() ? "Unsaved changes to " + file + "." : "Saved in " + file + ", with the project.";
                                  };
                                  source.dirty = [&project] { return project.IsDirty(); };
                                  source.save = [&editor] { return editor.SaveProjectSettings(); };
                                  return source;
                              },
                              &editor.m_projectSettingsWindow.focusPending),
      m_shortcutsWindow(editor, m_host), m_inputMapWindow(editor, m_host), m_captureWindow(editor, m_host), m_audioMixerWindow(editor, m_host), m_montageEditor(editor, m_host), m_vfxEditor(editor, m_host), m_retargetEditor(editor, m_host), m_animationLibraryEditor(editor, m_host), m_assetTable(editor), m_details2(editor), m_dockPanels(editor),
      m_viewToolbar(editor) {
    m_entityEditor.onBrowse = [this](const std::filesystem::path& path) {
        ShowDockPanel(0, true);
        m_assetTable.RevealFile(path);
        m_host.FocusWindow(EditorHost::kMainWindow);
    };
}

bool EditorShell::SaveVfxEditorScreenshot(const std::filesystem::path& path) {
    return m_vfxEditor.SaveScreenshot(path);
}

bool EditorShell::SaveEntityEditorScreenshot(const std::filesystem::path& path) {
    const Editor::EntityDocumentScope scope(m_editor);
    return m_entityEditor.SaveScreenshot(path);
}

EditorShell::~EditorShell() {
    Shutdown();
}

bool EditorShell::Initialize(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (!m_host.Initialize(window, assetDir)) {
        return false;
    }
    LoadThumbnailBackdrop(assetDir / "images" / "thumbnail_backdrop.png");
    m_dockPanels.AttachLog();
    m_editor.SetFilePickerHost([this](FilePickerRequest request) { m_fileDialog.Open(std::move(request)); });
    // Dropped from the file manager: imported into the folder the Asset Table shows.
    m_host.onFilesDropped = [this](std::vector<std::filesystem::path> files) {
        const std::filesystem::path root = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
        static_cast<void>(m_editor.ImportExternalFiles(files, root / m_editor.m_assetTableDirectory));
    };
    m_host.SetRoot(BuildLayout());
    // Every keyboard shortcut is an editor command's (the same registry the menus use).
    m_host.App().setShortcutHandler([this](const prism::Key key, const std::uint8_t mods) {
        return DispatchShortcut(key, mods);
    });
    // A game holding the mouse lets go on Escape, whatever has the keyboard, so the editor's own
    // controls (Stop above all) can be reached again.
    m_host.SetKeyHook([this](const int key, const int action) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && m_editor.IsGameMouseCaptured()) {
            m_editor.SetGameMouseCaptured(false);
            return true;
        }
        return false;
    });
    return true;
}

void EditorShell::Shutdown() {
    m_editor.SetFilePickerHost({});
    if (m_host.IsReady() && m_fileDialog.WindowId() != 0) {
        m_host.CloseWindow(m_fileDialog.WindowId());
    }
    if (m_host.IsReady() && m_entityEditor.WindowId() != 0) {
        m_host.CloseWindow(m_entityEditor.WindowId());
    }
    for (const int id : {m_preferencesWindow.WindowId(), m_projectSettingsWindow.WindowId(), m_shortcutsWindow.WindowId(), m_inputMapWindow.WindowId(), m_captureWindow.WindowId(), m_audioMixerWindow.WindowId()}) {
        if (m_host.IsReady() && id != 0) {
            m_host.CloseWindow(id);
        }
    }
    if (m_host.IsReady() && m_entityClassWindow.WindowId() != 0) {
        m_host.CloseWindow(m_entityClassWindow.WindowId());
    }
    if (m_host.IsReady() && m_montageEditor.WindowId() != 0) {
        m_host.CloseWindow(m_montageEditor.WindowId());
    }
    if (m_host.IsReady() && m_vfxEditor.WindowId() != 0) {
        m_host.CloseWindow(m_vfxEditor.WindowId());
    }
    if (m_host.IsReady() && m_retargetEditor.WindowId() != 0) {
        m_host.CloseWindow(m_retargetEditor.WindowId());
    }
    if (m_host.IsReady() && m_animationLibraryEditor.WindowId() != 0) {
        m_host.CloseWindow(m_animationLibraryEditor.WindowId());
    }
    m_host.Shutdown();
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorShell::BuildLayout() {
    // Editor design sheet, top to bottom:
    //   [#1 logo] [#15 title bar: menus | drag strip | - [] x]
    //   [       ] [#2 viewport / level name tab               ]
    //   sub row 1: #4 #5 #3 | #14 | transport
    //   sub row 2: gizmo / snap / view settings (full width)
    //   [#6 Outliner | #25 viewport over the dock panel | #17 Details]
    //   #16 dock bar: Asset Table, Output Log, Console, Profiler ...
    auto window = prism::vbox();
    window->add(BuildTitleBar());
    window->add(m_viewToolbar.Build());

    const bool singleSurface = m_host.App().platform().singleSurface();
    const float logicalWidth = m_host.App().windowSize().x;
    const float sideW = (logicalWidth > 0.0F && logicalWidth < 900.0F) ? std::max(140.0F, std::floor(logicalWidth * 0.22F)) : 250.0F;
    const float detailsW = (logicalWidth > 0.0F && logicalWidth < 900.0F) ? std::max(160.0F, std::floor(logicalWidth * 0.26F)) : 330.0F;

    auto& layoutStore = EditorLayoutStore::Instance();
    m_editor.m_scenePanelVisible = layoutStore.GetBool("level.outlinerVisible", m_editor.m_scenePanelVisible);
    m_editor.m_inspectorPanelVisible = layoutStore.GetBool("level.inspectorVisible", m_editor.m_inspectorPanelVisible);
    m_dockVisible = layoutStore.GetBool("level.dockVisible", true);

    auto centre = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centre->add(BuildViewport(), prism::Splitter::Pane{400.0F, 140.0F, 1e9F, true});
    m_dockArea = BuildDock();
    centre->add(m_dockArea, prism::Splitter::Pane{singleSurface ? 170.0F : 290.0F, 90.0F, 900.0F, false});
    layoutStore.BindSplitter("level.centre", *centre);
    if (m_dockPages) {
        const int savedTab = std::clamp(layoutStore.GetInt("level.dockTab", 0), 0, std::max(0, m_dockPages->count() - 1));
        m_dockPages->setActiveIndex(savedTab);
    }
    if (m_dockArea) {
        m_dockArea->setVisible(m_dockVisible);
    }

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    m_outlinerColumn = BuildOutliner();
    m_detailsColumn = BuildDetails();
    columns->add(m_outlinerColumn, prism::Splitter::Pane{sideW, 130.0F, 700.0F, false});
    columns->add(centre, prism::Splitter::Pane{800.0F, 220.0F, 1e9F, true});
    columns->add(m_detailsColumn, prism::Splitter::Pane{detailsW, 150.0F, 800.0F, false});
    layoutStore.BindSplitter("level.columns", *columns);
    window->add(columns, prism::Slot{1.0F});

    // Docking: drag a panel's tab onto another panel's edge to split it there (the Details under
    // or beside the Outliner, say), onto its tab strip to share the area, or anywhere else to float
    // it in a window of its own. Every divider resizes.
    m_dockSpace = std::make_unique<prism::DockSpace>("level-editor");
    m_dockSpace->attach(*m_outlinerTabs);
    m_dockSpace->attach(*m_detailsTabs);
    m_dockSpace->onChanged = [this] { UpdateDockColumns(); };
    if (m_outlinerColumn && m_outlinerTabs) {
        m_outlinerColumn->setVisible(m_editor.m_scenePanelVisible && m_outlinerTabs->count() > 0);
    }
    if (m_detailsColumn && m_detailsTabs) {
        m_detailsColumn->setVisible(m_editor.m_inspectorPanelVisible && m_detailsTabs->count() > 0);
    }

    window->add(BuildDockBar());

    m_safeFrame = prism::make<prism::Border>();
    m_safeFrame->setBackground(prism::ThemeColor(&prism::Theme::window));
    m_safeFrame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(singleSurface ? 0.0F : 1.0F));
    m_safeFrame->setChild(window);
    // The loading screen over everything while the editor starts (and a corner panel after).
    auto root = prism::make<prism::Overlay>();
    root->add(m_safeFrame);
    m_loadingScreen = prism::make<EditorLoadingScreen>();
    {
        const Core::Version version = Core::EngineCore::GetEngineVersion();
        m_loadingScreen->SetVersion("v" + std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                                       std::to_string(version.patch) + "." + std::to_string(version.build));
    }
    m_loadingScreen->onDismiss = [this] { m_editor.DismissStartupLoading(); };
    m_loadingScreen->onCancel = [this] { m_editor.CancelBackgroundImports(); };
    root->add(m_loadingScreen);
    return root;
}

std::vector<prism::MenuItem> EditorShell::CommandItems(const std::initializer_list<const char*> ids) {
    std::vector<prism::MenuItem> items;
    for (const char* const id : ids) {
        if (id == nullptr) {
            items.push_back(prism::MenuItem::Sep());
            continue;
        }
        EditorCommand* const command = m_editor.FindEditorCommand(id);
        if (command == nullptr) {
            continue;
        }
        prism::MenuItem item = prism::MenuItem::Item(
            command->label, [this, commandId = std::string(id)] { static_cast<void>(m_editor.RunEditorCommand(commandId)); },
            DisplayEditorShortcut(command->shortcuts[0]));
        item.enabled = !command->isEnabled || command->isEnabled();
        items.push_back(std::move(item));
    }
    return items;
}

bool EditorShell::DispatchShortcut(const prism::Key key, const std::uint8_t mods) {
    // Flying the viewport: W, A, S, D, Q and E move the camera, so no shortcut may take them.
    if (m_editor.IsViewportFlying()) {
        return true;
    }
    // PRISM key + modifiers -> the editor's stable shortcut ("Ctrl+S") -> its command.
    const std::optional<EditorShortcut> pressed = ShortcutFromPrismKey(key, mods);
    if (!pressed.has_value()) {
        return false;
    }
    for (const EditorCommand& command : m_editor.GetEditorCommands()) {
        if (command.shortcuts[0] == *pressed || command.shortcuts[1] == *pressed) {
            return m_editor.RunEditorCommand(command.id);
        }
    }
    return false;
}

namespace {


/// The Outliner's drop target for content dragged from the Asset Table.
class OutlinerDrop final : public prism::Border {
public:
    explicit OutlinerDrop(EditorShell& shell) : m_shell(shell) {}
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
        return path != nullptr && m_shell.DropContentOnOutliner(*path, e.pos);
    }
    void paint(prism::Canvas& c) override {
        prism::Border::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect(), 3.0F, prism::theme().highlight, 2);
        }
    }

private:
    [[nodiscard]] static bool Accepts(const prism::DragDropEvent& e) {
        return e.operation && e.operation->tag() == EditorAssetTable::kDragTag;
    }
    EditorShell& m_shell;
    bool m_hot = false;
};

/// The level viewport's drop target for content dragged from the Asset Table. It wraps the view
/// and its gizmo layer, so a drop anywhere over the picture lands where the pointer is.
class ViewportDrop final : public prism::Overlay {
public:
    explicit ViewportDrop(EditorShell& shell) : m_shell(shell) {}
    bool onDragEnter(const prism::DragDropEvent& e) override { return onDragOver(e); }
    bool onDragOver(const prism::DragDropEvent& e) override {
        const bool accepts = Accepts(e);
        m_shell.PreviewViewportDrop(accepts ? std::optional<prism::Vec2>{e.pos} : std::nullopt);
        if (accepts != m_hot) {
            m_hot = accepts;
            invalidate();
        }
        return accepts;
    }
    void onDragLeave() override {
        m_shell.PreviewViewportDrop(std::nullopt);
        m_hot = false;
        invalidate();
    }
    bool onDrop(const prism::DragDropEvent& e) override {
        m_hot = false;
        invalidate();
        if (!Accepts(e)) {
            m_shell.PreviewViewportDrop(std::nullopt);
            return false;
        }
        const std::string* path = e.operation->getPayload<std::string>();
        return path != nullptr && m_shell.DropContentOnViewport(*path, e.pos);
    }
    void paint(prism::Canvas& c) override {
        prism::Overlay::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect(), 3.0F, prism::theme().highlight, 2);
        }
    }

private:
    [[nodiscard]] bool Accepts(const prism::DragDropEvent& e) const {
        if (!e.operation || e.operation->tag() != EditorAssetTable::kDragTag) {
            return false;
        }
        const std::string* path = e.operation->getPayload<std::string>();
        return path != nullptr && m_shell.CanPlaceDroppedContent(*path);
    }
    EditorShell& m_shell;
    bool m_hot = false;
};

} // namespace

prism::Ref<prism::Widget> EditorShell::BuildTitleBar() {
    auto viewportTab = prism::make<DocumentTab>();
    viewportTab->onClick = [this] {
        const Scene::Entity viewport = m_editor.GetDocumentViewport();
        if (viewport != Scene::kInvalidEntity) {
            m_editor.SelectEntity(viewport);
        }
    };
    m_viewportNameTab = viewportTab;

    EditorHeaderDesc desc;
    desc.documentTab = viewportTab;
    desc.toolbarRow = BuildToolbarRow();
    desc.onClose = [] { prism::Application::get().quit(); };
    desc.mainWindow = true;
    desc.outTitleRow = &m_titleBarRow;

    desc.menus.push_back({"File", [this] {
        std::vector<prism::MenuItem> items = CommandItems({"file.saveScene", "file.saveAll", "file.loadScene", nullptr});
        items.push_back(prism::MenuItem::Item("Import Files...", [this] {
            const std::filesystem::path root = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
            static_cast<void>(m_editor.PromptImportFiles(root / m_editor.m_assetTableDirectory));
        }, "", prism::Icon::Open));
        items.push_back(prism::MenuItem::Item("Import Folder...", [this] {
            const std::filesystem::path root = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
            static_cast<void>(m_editor.PromptImportFolder(root / m_editor.m_assetTableDirectory));
        }, "", prism::Icon::FolderOpen));
        items.push_back(prism::MenuItem::Sep());
        for (prism::MenuItem& item : CommandItems({"file.projectSettings", "file.inputMap", "file.preferences",
                                                      "file.keyboardShortcuts", "file.commandPalette"})) {
            items.push_back(std::move(item));
        }
        items.push_back(prism::MenuItem::Item("Save Editor Preferences",
                                              [this] { static_cast<void>(m_editor.SaveSessionSettings()); }));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Return to Hub", [] { prism::Application::get().quit(); }, "",
                                              prism::Icon::ChevronLeft));
        return items;
    }});
    desc.menus.push_back({"Edit", [this] {
        return CommandItems({"edit.undo", "edit.redo", nullptr, "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete"});
    }});
    desc.menus.push_back({"Window", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Outliner", m_editor.m_scenePanelVisible, [this] {
            m_editor.m_scenePanelVisible = !m_editor.m_scenePanelVisible;
            UpdateDockColumns();
        }));
        items.push_back(prism::MenuItem::Check("Inspector", m_editor.m_inspectorPanelVisible, [this] {
            m_editor.m_inspectorPanelVisible = !m_editor.m_inspectorPanelVisible;
            UpdateDockColumns();
        }));
        items.push_back(prism::MenuItem::Sub("Layout", {
            prism::MenuItem::Item("Default", [this] { ApplyLayoutPreset(0); }),
            prism::MenuItem::Item("Inspector under Outliner", [this] { ApplyLayoutPreset(1); }),
            prism::MenuItem::Item("Inspector beside Outliner", [this] { ApplyLayoutPreset(2); }),
            prism::MenuItem::Item("Wide Viewport", [this] { ApplyLayoutPreset(3); }),
        }, prism::Icon::Frame));
        items.push_back(prism::MenuItem::Item("Reset Panel Layout", [this] { ApplyLayoutPreset(0); }));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Check("Network Capture", m_editor.m_showCaptureWindow, [this] {
            m_editor.m_showCaptureWindow = !m_editor.m_showCaptureWindow;
            if (m_editor.m_showCaptureWindow) {
                m_editor.m_captureFocusPending = true;
            }
        }));
        items.push_back(prism::MenuItem::Check("Audio Mixer", m_editor.m_showAudioMixerWindow, [this] {
            m_editor.m_showAudioMixerWindow = !m_editor.m_showAudioMixerWindow;
            if (m_editor.m_showAudioMixerWindow) {
                m_editor.m_audioMixerFocusPending = true;
            }
        }));
        items.push_back(prism::MenuItem::Check("Bottom Dock", m_dockVisible, [this] {
            ShowDockPanel(m_dockPages ? m_dockPages->activeIndex() : 0, false);
        }));
        return items;
    }});
    desc.menus.push_back({"Tools", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Project Browser", [this] { ShowDockPanel(0, true); }));
        items.push_back(prism::MenuItem::Item("Import Queue", {}).disabled());
        items.push_back(prism::MenuItem::Sep());
        for (prism::MenuItem& item : CommandItems({"file.projectSettings", "file.preferences", "file.commandPalette"})) {
            items.push_back(std::move(item));
        }
        return items;
    }});
    desc.menus.push_back({"Build", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Build All Levels", {}).disabled(),
            prism::MenuItem::Item("Build Geometry", {}).disabled(),
            prism::MenuItem::Item("Build Lighting Only", {}).disabled(),
            prism::MenuItem::Item("Cook Content for Project", {}).disabled(),
        };
    }});
    desc.menus.push_back({"Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("UVE Editor Reference", {}).disabled(),
            prism::MenuItem::Item("About UNIVEX Engine", {}).disabled(),
        };
    }});
    desc.compactMenuTail = [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Sep(),
            prism::MenuItem::Item("Return to Hub", [] { prism::Application::get().quit(); }, "",
                                  prism::Icon::ChevronLeft),
        };
    };

    return BuildEditorHeader(std::move(desc));
}

prism::Ref<prism::Widget> EditorShell::BuildToolbarRow() {
    // Sub row 1 (moved up cleanly into row 2 on Android): open folder, save, mode | blueprint | Play/Pause/Stop | status.
    const bool singleSurface = m_host.App().platform().singleSurface();
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(5.0F, 1.0F, 5.0F, 1.0F));

    auto openFolder = ToolButton(prism::Icon::FolderOpen, "Open folder (Asset Table)");
    openFolder->onClick = [this] { ShowDockPanel(0, true); };
    row->add(openFolder, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto save = ToolButton(prism::Icon::Save, "Save all (Ctrl+Shift+S)");
    save->onClick = [this] { static_cast<void>(m_editor.RunEditorCommand("file.saveAll")); };
    row->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    m_selectionMode = prism::make<prism::ComboBox>(std::vector<std::string>{"Universal", "Move", "Rotate", "Scale"}, 0);
    m_selectionMode->setTooltip("Selection mode");
    m_selectionMode->onChange = [this](const int index) {
        using Mode = Editor::ViewportGizmoMode;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    auto modeBox = prism::make<prism::SizeBox>();
    (void)singleSurface;
    modeBox->setWidthOverride(136.0F);
    modeBox->setContent(m_selectionMode);
    row->add(modeBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparator());

    auto blueprint = ToolButton(prism::Icon::Blueprint, "Blueprint editor (beta)");
    blueprint->onClick = [] {
        if (prism::Application::exists()) {
            prism::Application::get().notify("Blueprint editor (beta) - coming in the blueprint milestone",
                                             prism::ToastLevel::Info);
        }
    };
    row->add(blueprint, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(HSeparator());

    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_playButton = ToolButton(prism::Icon::Play, "Play");
    m_playButton->onClick = [this] {
        if (m_editor.GetPlayModeState() == EditorPlayModeState::Paused) {
            static_cast<void>(m_editor.ResumePlayMode());
        } else {
            static_cast<void>(m_editor.EnterPlayMode());
        }
    };
    m_pauseButton = ToolButton(prism::Icon::Pause, "Pause");
    m_pauseButton->onClick = [this] { static_cast<void>(m_editor.PausePlayMode()); };
    m_stopButton = ToolButton(prism::Icon::Stop, "Stop");
    m_stopButton->onClick = [this] { static_cast<void>(m_editor.StopPlayMode()); };
    row->add(m_playButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(m_pauseButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(m_stopButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_statusLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    m_statusLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_statusLabel, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    m_rendererProfileButton = prism::make<prism::Button>("Forward+", prism::Icon::Profiler, prism::ButtonStyle::Toolbar);
    m_rendererProfileButton->setTooltip("Project Renderer Profile (Compatibility / Mobile / Forward+)");
    m_rendererProfileButton->onClick = [this] {
        const Render::RendererProfileResolution res = m_editor.GetRendererProfileResolution();
        static constexpr std::array<Platform::RendererProfile, 3> kProfiles{
            Platform::RendererProfile::Compatibility,
            Platform::RendererProfile::Mobile,
            Platform::RendererProfile::ForwardPlus,
        };
        std::vector<prism::MenuItem> items;
        for (const Platform::RendererProfile profile : kProfiles) {
            std::string label(Platform::GetRendererProfileDisplayName(profile));
            if (profile == Platform::RendererProfile::Compatibility) {
                label += " (Low-End / Fast RGBA8)";
            } else if (profile == Platform::RendererProfile::Mobile) {
                label += " (Balanced Mobile PBR)";
            } else {
                label += " (Desktop Full PBR)";
            }
            items.push_back(prism::MenuItem::Check(
                std::move(label), res.requested == profile, [this, profile] {
                    m_editor.SetRendererProfile(profile);
                    const Render::RendererProfileResolution updated = m_editor.GetRendererProfileResolution();
                    if (prism::Application::exists()) {
                        if (updated.fallbackApplied) {
                            prism::Application::get().notify(
                                std::string(Platform::GetRendererProfileDisplayName(updated.requested)) +
                                    " not supported on this GPU - using " +
                                    std::string(Platform::GetRendererProfileDisplayName(updated.effective)),
                                prism::ToastLevel::Warning);
                        } else {
                            prism::Application::get().notify(
                                "Renderer switched to " +
                                    std::string(Platform::GetRendererProfileDisplayName(updated.effective)),
                                prism::ToastLevel::Info);
                        }
                    }
                }));
        }
        prism::showMenu(std::move(items), m_rendererProfileButton->rect(), prism::PopupPlacement::Below);
    };
    row->add(m_rendererProfileButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(25.0F);
    sized->setContent(row);

    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::header));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    border->setChild(sized);
    return border;
}


prism::Ref<prism::Widget> EditorShell::BuildOutliner() {
    // #6 tab, #7 '+', #8 filter, #10 search, #11 tree (#12 its scrollbar).
    auto page = prism::vbox(4.0F);
    page->setPadding(prism::Margin(4.0F));

    auto bar = prism::hbox(4.0F);
    // A folder in one click, and the level's objects (Basic, Lights, Shapes, ...) under one button.
    m_addButton = ToolButton(prism::Icon::Folder, "New Folder");
    m_addButton->onClick = [this] {
        static_cast<void>(m_editor.CreateDocumentSceneObject(SceneObjectKind::Folder));
    };
    m_filterButton = ToolButton(prism::Icon::Cube, "Place an object in the level");
    m_filterButton->onClick = [this] { ShowAddMenu(); };
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
    m_outliner->onVisibilityToggle = [this](prism::TreeNode* node) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (entity != Scene::kInvalidEntity) {
            static_cast<void>(m_editor.SetEntityVisible(entity, node->visible));
        }
    };
    m_outliner->onRename = [this](prism::TreeNode* node, const std::string& name) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (entity != Scene::kInvalidEntity) {
            m_editor.SelectEntity(entity);
            static_cast<void>(m_editor.SetSelectedEntityName(name));
        }
    };
    m_outliner->onDelete = [this](prism::TreeNode* node) {
        const Scene::Entity entity = EntityForTreeId(node->id);
        if (entity != Scene::kInvalidEntity) {
            if (!m_editor.IsEntitySelected(entity)) {
                m_editor.SelectEntity(entity);
            }
            m_editor.RequestDeleteSelectedEntities();
        }
    };
    m_outliner->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        const Scene::Entity entity = node != nullptr ? EntityForTreeId(node->id) : Scene::kInvalidEntity;
        EditorObjectMenu::Hooks hooks;
        hooks.tree = m_outliner.get();
        hooks.addChild = [this](const Scene::Entity target) { ShowAddMenu(target); };
        EditorObjectMenu::Show(m_editor, entity, node, at, hooks);
    };
    // Drag an object onto another: inside it (a child), or before / after it (a sibling).
    m_outliner->canDropNode = [this](prism::TreeNode* dragged, prism::TreeNode* target, const prism::TreeView::DropWhere where) {
        const Scene::Entity entity = EntityForTreeId(dragged->id);
        const Scene::Entity onto = EntityForTreeId(target->id);
        if (entity == Scene::kInvalidEntity || onto == Scene::kInvalidEntity || !m_editor.IsReparentableObject(entity)) {
            return false;
        }
        if (where == prism::TreeView::DropWhere::Inside) {
            return m_editor.IsAllowedOutlinerParent(entity, onto);
        }
        Scene::Entity parent = Scene::kInvalidEntity;
        return m_editor.TryGetDocumentParent(onto, parent) && m_editor.IsAllowedOutlinerParent(entity, parent);
    };
    m_outliner->onDropNode = [this](prism::TreeNode* dragged, prism::TreeNode* target, const prism::TreeView::DropWhere where) {
        const Scene::Entity entity = EntityForTreeId(dragged->id);
        const Scene::Entity onto = EntityForTreeId(target->id);
        const int place = where == prism::TreeView::DropWhere::Before ? 0 : where == prism::TreeView::DropWhere::Inside ? 1 : 2;
        if (m_editor.PlaceDocumentEntity(entity, onto, place)) {
            m_editor.SelectEntity(entity);
        }
    };
    // Double-click a placed entity: it opens in the Entity Editor (its inside is edited there).
    m_outliner->onActivate = [this](prism::TreeNode* node) {
        const Scene::Entity entity = node != nullptr ? EntityForTreeId(node->id) : Scene::kInvalidEntity;
        if (!m_editor.IsSealedHierarchyRow(entity)) {
            return;
        }
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        const Asset::AssetGuid guid = em.GetComponent<Scene::PrefabInstanceComponent>(entity).sourcePrefabGuid;
        const std::filesystem::path source = m_editor.m_services->GetAssetDatabase().Resolve(guid);
        if (!source.empty()) {
            static_cast<void>(m_editor.OpenEntityEditor(source));
        }
    };
    // Content dragged from the Asset Table lands here, under the row it is dropped on.
    auto drop = prism::make<OutlinerDrop>(*this);
    drop->setChild(m_outliner);
    page->add(drop, prism::Slot{1.0F});

    m_outlinerTabs = prism::make<prism::TabView>();
    m_outlinerTabs->addTab("Outliner", prism::Icon::Layers, Panel(page));
    return m_outlinerTabs;
}

prism::Ref<prism::Widget> EditorShell::BuildViewport() {
    // #25: the viewport itself (its name is tab #2 under the title bar).
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    // Input reaches the viewport through the host's raw state (CaptureViewInput); onInput only
    // makes the view focusable so clicking it takes keyboard focus away from text fields.
    m_viewport->onInput = [](const prism::EngineInput&) {};
    // Every gizmo (navigation, transform, object gizmos and billboards) is PRISM's, drawn over the
    // engine's image of the scene; the renderer draws none of its own (hostDrawsGizmos).
    m_gizmos = prism::make<EditorViewportGizmos>(m_editor);
    m_cameraScreen = prism::make<CameraScreen>();
    m_cameraScreen->setVisible(false);
    // Content dragged from the Asset Table can be dropped straight into the view.
    auto stack = prism::make<ViewportDrop>(*this);
    stack->add(m_viewport);
    stack->add(m_gizmos);
    stack->add(m_cameraScreen);
    return stack;
}

prism::Ref<prism::Widget> EditorShell::BuildDetails() {
    // #17 tab, #18 search, #20 the selected object's components (#21 scroll), drawn from the
    // engine's component metadata (EditorDetails).
    m_detailsTabs = prism::make<prism::TabView>();
    m_detailsTabs->addTab("Inspector", prism::Icon::Properties, Panel(m_details2.Build()));
    return m_detailsTabs;
}

prism::Ref<prism::Widget> EditorShell::BuildDock() {
    // The dock panel (above the #16 dock bar): one page per dock tab.
    m_dockPages = prism::make<prism::StackSwitcher>();
    m_dockPages->addPage(Panel(m_assetTable.Build()));
    m_dockPages->addPage(Panel(m_dockPanels.BuildOutputLog()));
    m_dockPages->addPage(Panel(m_dockPanels.BuildConsole()));
    m_dockPages->addPage(Panel(m_dockPanels.BuildProfiler()));
    m_dockPages->addPage(Panel(m_dockPanels.BuildSearch()));
    return m_dockPages;
}

prism::Ref<prism::Widget> EditorShell::BuildDockBar() {
    // #16: the dock bar along the bottom; each tab shows its panel, the active one hides it.
    const bool singleSurface = m_host.App().platform().singleSurface();
    auto bar = prism::hbox(2.0F);
    bar->setPadding(prism::Margin(4.0F, 0.0F, 6.0F, 0.0F));
    struct DockTab { const char* label; prism::Icon icon; };
    static constexpr std::array<DockTab, 5> kTabs{{
        {"Asset Table", prism::Icon::AssetBrowser},
        {"Output Log", prism::Icon::OutputLog},
        {"Console", prism::Icon::Terminal},
        {"Profiler", prism::Icon::Profiler},
        {"Search", prism::Icon::Search},
    }};
    for (std::size_t i = 0; i < kTabs.size(); ++i) {
        auto tab = prism::make<prism::Button>(kTabs[i].label, kTabs[i].icon, prism::ButtonStyle::Toolbar);
        tab->setCheckable(true);
        const int activeDockTab = m_dockPages ? m_dockPages->activeIndex() : 0;
        tab->setChecked(m_dockVisible && static_cast<int>(i) == activeDockTab);
        tab->onClick = [this, index = static_cast<int>(i)] { ShowDockPanel(index, false); };
        m_dockTabs.push_back(tab);
        bar->add(tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    bar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto sized = prism::make<prism::SizeBox>();
    (void)singleSurface;
    sized->setHeightOverride(22.0F);
    sized->setContent(bar);
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    border->setChild(sized);
    return border;
}

namespace {

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

bool EditorShell::CanPlaceDroppedContent(const std::string& relativePath) const {
    const std::filesystem::path relative(relativePath);
    const std::string extension = LowerExtension(relative);
    return extension == ".uventity" || extension == ".uvprefab" || extension == ".uvmodel" ||
           extension == ".uvanim" || Editor::IsModelSourcePath(relative);
}

bool EditorShell::PlaceDroppedContent(const std::string& relativePath, const Scene::Entity parent,
                                      const std::string_view surface) {
    // Entity assets and models go through the same calls as the Asset Table's "Place in Scene".
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path relative(relativePath);
    const std::string extension = LowerExtension(relative);
    const std::filesystem::path clipSource =
        extension == ".uvanim" ? m_editor.FindAnimationClipSource(relative) : std::filesystem::path{};
    m_editor.m_contentStatusMessage.clear();
    Scene::Entity placed = Scene::kInvalidEntity;
    if (extension == ".uventity" || extension == ".uvprefab") {
        placed = m_editor.PlaceEntityAsset(snapshot.contentRoot / relative, parent);
    } else if (Editor::IsModelSourcePath(relative) || extension == ".uvmodel") {
        placed = m_editor.PlaceModelSource(relative, parent);
    } else if (!clipSource.empty()) {
        // The Asset Table lists an animation-only file as its clips: one places the file, playing it.
        placed = m_editor.PlaceModelSource(clipSource, parent, relative);
    } else {
        m_editor.m_contentStatusMessage =
            "Only entity assets and models can be placed in the " + std::string{surface} + ".";
    }
    // A drop point is for this placement only, whether or not it happened.
    m_editor.m_nextPlacementPoint.reset();
    if (placed == Scene::kInvalidEntity && m_editor.m_contentStatusMessage.empty()) {
        m_editor.m_contentStatusMessage = "Could not place " + relative.filename().string() + ".";
    }
    return placed != Scene::kInvalidEntity;
}

bool EditorShell::DropContentOnOutliner(const std::string& relativePath, const prism::Vec2 at) {
    // Dropped on a row, content goes under that row's object (else where a new object goes).
    Scene::Entity parent = Scene::kInvalidEntity;
    if (m_outliner) {
        if (const prism::TreeNode* node = m_outliner->nodeAt(at); node != nullptr) {
            parent = EntityForTreeId(node->id);
        }
    }
    return PlaceDroppedContent(relativePath, parent, "Outliner");
}

bool EditorShell::DropContentOnViewport(const std::string& relativePath, const prism::Vec2 at) {
    PreviewViewportDrop(std::nullopt);
    if (m_gizmos) {
        if (const std::optional<std::array<float, 3>> point = m_gizmos->WorldPointUnder(at)) {
            m_editor.m_nextPlacementPoint = Math::Vector3{(*point)[0], (*point)[1], (*point)[2]};
        }
    }
    return PlaceDroppedContent(relativePath, Scene::kInvalidEntity, "viewport");
}

void EditorShell::PreviewViewportDrop(const std::optional<prism::Vec2> at) {
    if (!m_gizmos) {
        return;
    }
    m_gizmos->SetDropPreview(at.has_value() ? m_gizmos->WorldPointUnder(*at) : std::nullopt);
}

void EditorShell::ApplyLayoutPreset(const int preset) {
    // Window > Layout: ready-made arrangements of the panels; every panel re-flows at once.
    ResetPanelLayout();
    if (!m_dockSpace || !m_outlinerTabs || !m_detailsTabs) {
        return;
    }
    const auto moveDetails = [this](const prism::TabView::DockZone zone) {
        const int index = m_detailsTabs->indexOf("Inspector");
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
                ShowDockPanel(m_dockPages ? m_dockPages->activeIndex() : 0, false);
            }
            break;
        default:
            if (!m_dockVisible) {
                ShowDockPanel(m_dockPages ? m_dockPages->activeIndex() : 0, true);
            }
            break;
    }
    UpdateDockColumns();
}

void EditorShell::UpdateDockColumns() {
    EditorLayoutStore::Instance().SetBool("level.outlinerVisible", m_editor.m_scenePanelVisible);
    EditorLayoutStore::Instance().SetBool("level.inspectorVisible", m_editor.m_inspectorPanelVisible);
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

void EditorShell::ResetPanelLayout() {
    if (!m_dockSpace || !m_outlinerTabs || !m_detailsTabs) {
        return;
    }
    EditorLayoutStore::Instance().ResetAll();
    // Every split-off or floating panel back into the Details area first, then each panel to its own.
    m_dockSpace->dissolve(*m_detailsTabs);
    const auto moveTo = [](prism::TabView& from, prism::TabView& to, const std::string& title) {
        const int index = from.indexOf(title);
        if (index >= 0) {
            to.insertTab(to.count(), from.takeTab(index));
        }
    };
    moveTo(*m_detailsTabs, *m_outlinerTabs, "Outliner");
    moveTo(*m_outlinerTabs, *m_detailsTabs, "Inspector");
    m_outlinerTabs->setCurrent(0);
    m_detailsTabs->setCurrent(0);
    m_editor.m_scenePanelVisible = true;
    m_editor.m_inspectorPanelVisible = true;
    UpdateDockColumns();
}

void EditorShell::ShowDockPanel(const int index, const bool forceVisible) {
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
    EditorLayoutStore::Instance().SetBool("level.dockVisible", m_dockVisible);
    EditorLayoutStore::Instance().SetInt("level.dockTab", m_dockPages->activeIndex());
}

// ---------------------------------------------------------------------------------- add / filter


void EditorShell::ShowCommandPalette() {
    const auto itemFor = [this](const EditorCommand& command, const bool withCategory) {
        const std::string id = command.id;
        prism::MenuItem item = prism::MenuItem::Item(withCategory ? Editor::CommandTitleText(command) : command.label,
                                                     [this, id] { static_cast<void>(m_editor.RunEditorCommand(id)); },
                                                     DisplayEditorShortcut(command.shortcuts[0]));
        if (!Editor::IsCommandEnabled(command)) {
            item.disabled();
        }
        return item;
    };
    std::vector<prism::MenuItem> items;
    // The commands run last, most recent first; then every command by its category.
    for (const std::string& recent : m_editor.m_recentCommandIds) {
        if (const EditorCommand* const command = m_editor.FindEditorCommand(recent)) {
            items.push_back(itemFor(*command, true));
        }
    }
    if (!items.empty()) {
        items.push_back(prism::MenuItem::Sep());
    }
    std::vector<std::string> categories;
    for (const EditorCommand& command : m_editor.GetEditorCommands()) {
        if (std::find(categories.begin(), categories.end(), command.category) == categories.end()) {
            categories.push_back(command.category);
        }
    }
    for (const std::string& category : categories) {
        std::vector<prism::MenuItem> group;
        for (const EditorCommand& command : m_editor.GetEditorCommands()) {
            if (command.category == category) {
                group.push_back(itemFor(command, false));
            }
        }
        items.push_back(prism::MenuItem::Sub(category, std::move(group)));
    }
    const prism::Vec2 size = m_host.App().windowSize();
    prism::showSearchMenu(std::move(items), prism::Rect{size.x * 0.5F - 240.0F, 70.0F, 480.0F, 1.0F}, prism::PopupPlacement::Below,
                          "Run a command");
}

void EditorShell::ShowAddMenu(const Scene::Entity explicitParent) {
    // GET CONTENT, PLACE OBJECTS (a flyout per group) and RECENTLY PLACED; typing searches them all.
    // Only what a level holds: a character's parts are built in an Entity, and an entity of the
    // project (a player, a character) is offered under Basic once it is in Content.
    const auto place = [this, explicitParent](const SceneObjectKind kind) {
        static_cast<void>(m_editor.CreateDocumentSceneObject(kind, explicitParent));
        const int key = static_cast<int>(kind);
        m_recentKinds.erase(std::remove(m_recentKinds.begin(), m_recentKinds.end(), key), m_recentKinds.end());
        m_recentKinds.insert(m_recentKinds.begin(), key);
        if (m_recentKinds.size() > 5U) {
            m_recentKinds.resize(5U);
        }
    };
    const auto itemFor = [this, &place](const SceneObjectKind kind, std::string label = {}) {
        const Scene::Objects::SceneObjectDescriptor* const descriptor = Scene::Objects::FindSceneObjectDescriptor(kind);
        if (label.empty()) {
            label = descriptor != nullptr ? std::string(descriptor->displayName) : std::string("Object");
        }
        prism::MenuItem item = prism::MenuItem::Item(label, [place, kind] { place(kind); }, "", ObjectKindGlyph(kind));
        if (const EditorIcons::Image icon = m_icons.Object(kind, 16); icon) {
            item.withImage(icon.pixels, icon.size);
        }
        return item;
    };
    const auto offered = [](const SceneObjectKind kind) {
        const Scene::Objects::SceneObjectDescriptor* const descriptor = Scene::Objects::FindSceneObjectDescriptor(kind);
        return descriptor != nullptr && descriptor->libraryCreatable && IsObjectOffered(kind, AddObjectContext::Level);
    };
    std::set<int> grouped;
    const auto group = [&](const char* const title, const prism::Icon icon, std::initializer_list<SceneObjectKind> kinds,
                           std::vector<prism::MenuItem> extra = {}) {
        std::vector<prism::MenuItem> sub = std::move(extra);
        for (const SceneObjectKind kind : kinds) {
            if (offered(kind)) {
                sub.push_back(itemFor(kind));
                grouped.insert(static_cast<int>(kind));
            }
        }
        return prism::MenuItem::Sub(title, std::move(sub), icon);
    };

    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("GET CONTENT"));
    items.push_back(prism::MenuItem::Item("Import Files...", [this] {
        static_cast<void>(m_editor.PromptImportFiles(m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot));
    }, "", prism::Icon::Open));
    items.push_back(prism::MenuItem::Item("Import Folder...", [this] {
        static_cast<void>(m_editor.PromptImportFolder(m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot));
    }, "", prism::Icon::FolderOpen));
    items.push_back(prism::MenuItem::Header("PLACE OBJECTS"));

    // The project's own entities (a player, a character): placed as they are in Content.
    std::vector<prism::MenuItem> entities;
    {
        const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
            if (entry.kind != Asset::ProjectFileEntryKind::File || entry.relativePath.extension() != ".uventity") {
                continue;
            }
            const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
            entities.push_back(prism::MenuItem::Item(entry.relativePath.stem().string(), [this, absolute] {
                static_cast<void>(m_editor.PlaceEntityAsset(absolute));
            }, "", prism::Icon::PreviewCharacter));
        }
    }
    items.push_back(group("Basic", prism::Icon::Object3D,
                          {SceneObjectKind::Object3D, SceneObjectKind::Marker3D, SceneObjectKind::SpawnPoint3D,
                           SceneObjectKind::Camera3D, SceneObjectKind::Light3D, SceneObjectKind::Area3D},
                          std::move(entities)));
    items.push_back(group("Lights", prism::Icon::Light,
                          {SceneObjectKind::DirectionalLight3D, SceneObjectKind::Light3D, SceneObjectKind::WorldEnvironment3D,
                           SceneObjectKind::Sky, SceneObjectKind::ReflectionProbe3D}));
    items.push_back(group("Shapes", prism::Icon::Cube,
                          {SceneObjectKind::BoxMesh3D, SceneObjectKind::SphereMesh3D, SceneObjectKind::PlaneMesh3D,
                           SceneObjectKind::MeshInstance3D, SceneObjectKind::LODGroup3D}));
    items.push_back(group("Cinematic", prism::Icon::Camera,
                          {SceneObjectKind::Camera3D, SceneObjectKind::SpringArm3D, SceneObjectKind::AnimationSequencer}));
    items.push_back(group("Visual Effects", prism::Icon::Environment,
                          {SceneObjectKind::ParticleEmitter3D, SceneObjectKind::Decal3D, SceneObjectKind::FogVolume3D}));
    items.push_back(group("Volumes", prism::Icon::Collider,
                          {SceneObjectKind::Area3D, SceneObjectKind::Collider3D, SceneObjectKind::NavMeshVolume3D,
                           SceneObjectKind::LevelStreamer3D, SceneObjectKind::WorldPartition3D, SceneObjectKind::Occluder3D,
                           SceneObjectKind::VisibilityRegion3D}));
    // Kept for what comes next; nothing in it yet.
    items.push_back(prism::MenuItem::Sub("More", {prism::MenuItem::Item("Nothing here yet", {}).disabled()}, prism::Icon::Plus));
    if (!m_recentKinds.empty()) {
        items.push_back(prism::MenuItem::Header("RECENTLY PLACED"));
        for (const int key : m_recentKinds) {
            items.push_back(itemFor(static_cast<SceneObjectKind>(key)));
        }
    }
    prism::showSearchMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                          "Search objects", true);
}

// ---------------------------------------------------------------------------------------- refresh

std::vector<Scene::Entity> EditorShell::OutlinerRoots() {
    // The scene root is structural and stays out of the Outliner, as it always has: when the
    // document has its Viewport, the rows start at the scene root's children (the Viewport and
    // anything beside it); a document without one lists its roots.
    if (m_editor.GetDocumentViewport() != Scene::kInvalidEntity) {
        const Scene::Entity sceneRoot = m_editor.GetDocumentSceneRoot();
        if (sceneRoot != Scene::kInvalidEntity) {
            return m_editor.m_services->GetSceneGraph().GetChildren(m_editor.m_services->GetEntityManager(),
                                                                         sceneRoot);
        }
    }
    return m_editor.GetDocumentRoots();
}

int EditorShell::TreeIdFor(const Scene::Entity entity) {
    m_treeEntities.push_back(entity);
    return static_cast<int>(m_treeEntities.size());
}

Scene::Entity EditorShell::EntityForTreeId(const int id) const {
    if (id <= 0 || static_cast<std::size_t>(id) > m_treeEntities.size()) {
        return Scene::kInvalidEntity;
    }
    return m_treeEntities[static_cast<std::size_t>(id - 1)];
}

std::uint64_t EditorShell::OutlinerSignature() {
    std::uint64_t h = 1469598103934665603ULL;
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const std::function<void(Scene::Entity, int)> visit = [&](const Scene::Entity e, const int depth) {
        Hash(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
        Hash(h, static_cast<std::uint64_t>(depth));
        Hash(h, m_editor.GetEntityDisplayLabel(e));
        Hash(h, m_editor.IsEntitySelected(e) ? 1U : 0U);
        if (m_editor.IsSealedHierarchyRow(e)) {
            return; // a placed entity is one row: its inside is the Entity Editor's
        }
        for (const Scene::Entity child : m_editor.m_services->GetSceneGraph().GetChildren(em, e)) {
            visit(child, depth + 1);
        }
    };
    for (const Scene::Entity root : OutlinerRoots()) {
        visit(root, 0);
    }
    for (const Scene::Entity e : m_editor.GetSelectedEntities()) {
        Hash(h, (static_cast<std::uint64_t>(e.index) << 32U) | e.generation);
    }
    return h;
}

void EditorShell::RefreshOutliner() {
    if (!m_outliner) {
        return;
    }
    // Walking the whole level for its signature is done a dozen times a second, not every frame;
    // a change of selection is taken at once (the tree shows it).
    {
        std::uint64_t selection = 1469598103934665603ULL;
        for (const Scene::Entity e : m_editor.m_selectedEntities) {
            selection = (selection ^ ((static_cast<std::uint64_t>(e.index) << 32U) | e.generation)) * 1099511628211ULL;
        }
        const auto now = std::chrono::steady_clock::now();
        if (selection == m_outlinerSelection && now - m_outlinerCheckedAt < std::chrono::milliseconds(80)) {
            return;
        }
        m_outlinerSelection = selection;
        m_outlinerCheckedAt = now;
    }
    const std::uint64_t signature = OutlinerSignature();
    if (signature == m_outlinerSignature) {
        return;
    }
    m_outlinerSignature = signature;
    m_syncingSelection = true;
    m_outliner->clear();
    m_treeEntities.clear();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    std::vector<int> selected;
    std::unordered_map<std::uint64_t, int> rowOf;
    const std::function<void(prism::TreeNode*, Scene::Entity)> add = [&](prism::TreeNode* parent,
                                                                           const Scene::Entity e) {
        const int id = TreeIdFor(e);
        const std::string_view typeName = m_editor.GetObjectTypeName(e);
        prism::TreeNode* node = m_outliner->addNode(parent, m_editor.GetEntityDisplayLabel(e), prism::Icon::Dot, id);
        // The engine's own icon for the object type (a folder in golden dark brown).
        if (const EditorIcons::Image icon = m_icons.Object(Scene::ResolveSceneObjectKind(em, e), 16);
            icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        node->badge = std::string(typeName);
        rowOf[(static_cast<std::uint64_t>(e.index) << 32U) | e.generation] = id;
        // A placed entity is one row, like a mesh is one object: what is inside it belongs to the
        // Entity Editor (double-click or Open Entity), not to the level's tree.
        if (m_editor.IsSealedHierarchyRow(e)) {
            return;
        }
        for (const Scene::Entity child : m_editor.m_services->GetSceneGraph().GetChildren(em, e)) {
            add(node, child);
        }
    };
    for (const Scene::Entity root : OutlinerRoots()) {
        add(nullptr, root);
    }
    // Something selected inside a placed entity shows as its entity's row.
    for (const Scene::Entity e : m_editor.GetSelectedEntities()) {
        const Scene::Entity owner = m_editor.ResolveHierarchyRowOwner(e);
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

void EditorShell::RefreshDetails() {
    m_details2.Refresh();
}

void EditorShell::RefreshToolbar() {
    const EditorPlayModeState play = m_editor.GetPlayModeState();
    if (m_playButton) {
        m_playButton->setChecked(play == EditorPlayModeState::Playing);
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(play == EditorPlayModeState::Paused);
    }
    m_viewToolbar.Refresh();
    using Mode = Editor::ViewportGizmoMode;
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
        auto* tab = static_cast<DocumentTab*>(m_viewportNameTab.get());
        const Scene::Entity viewport = m_editor.GetDocumentViewport();
        if (viewport != Scene::kInvalidEntity) {
            tab->setName(m_editor.GetEntityDisplayLabel(viewport));
            const EditorIcons::Image icon = m_icons.Object(
                Scene::ResolveSceneObjectKind(m_editor.m_services->GetEntityManager(), viewport), 16);
            tab->setIcon(icon.pixels, icon.size);
        } else {
            const std::string stem = m_editor.m_activeScenePath.stem().string();
            tab->setName(stem.empty() ? "Viewport" : stem);
            tab->setIcon(nullptr, 0);
        }
    }
    if (m_statusLabel) {
        const char* const state = play == EditorPlayModeState::Edit      ? "Editing"
                                  : play == EditorPlayModeState::Playing ? "Playing"
                                                                            : "Paused";
        std::string text = m_editor.m_sceneDirty ? "unsaved" : "saved";
        text += " | " + std::to_string(m_editor.m_selectedEntities.size()) + " selected | ";
        text += state;
        const Core::Version version = Core::EngineCore::GetEngineVersion();
        const std::string versionTag = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                                       std::to_string(version.patch) + "." + std::to_string(version.build);
        if (!m_host.App().platform().singleSurface()) {
            text += "    UVE Editor " + versionTag;
        } else {
            text += " | v" + versionTag;
        }
        if (m_statusLabel->text() != text) {
            m_statusLabel->setText(text);
        }
    }
    if (m_rendererProfileButton) {
        const Render::RendererProfileResolution res = m_editor.GetRendererProfileResolution();
        std::string label(Platform::GetRendererProfileDisplayName(res.effective));
        if (res.fallbackApplied) {
            label += " (Fallback)";
        }
        if (m_rendererProfileButton->text() != label) {
            m_rendererProfileButton->setText(label);
            if (res.fallbackApplied) {
                m_rendererProfileButton->setTooltip(
                    "Requested " + std::string(Platform::GetRendererProfileDisplayName(res.requested)) +
                    " -> Fallback: " + std::string(res.fallbackReason));
            } else {
                m_rendererProfileButton->setTooltip(
                    std::string(Platform::GetRendererProfileDescription(res.effective)));
            }
        }
    }
}

// ------------------------------------------------------------------------------------------ frame

void EditorShell::AppendCameraScreen(Editor& editor, const EditorDetails& details,
                                                CameraScreen* screen,
                                                std::vector<EditorHost::ViewImage>& views) {
    // A selected Camera3D's view in the viewport's corner, while the view itself stays the editor's
    // (not while the viewport already looks through that camera).
    if (screen == nullptr) {
        return;
    }
    const Scene::Entity selected = editor.GetSelectedEntity();
    const bool show = details.CameraScreenOpen() && editor.m_viewports.cameraPreview && editor.m_selectedEntities.size() == 1U &&
                      Scene::IsDocumentCameraEntity(editor.m_services->GetEntityManager(), selected) &&
                      editor.GetPreviewCamera() != selected;
    if (screen->visible() != show) {
        screen->setVisible(show);
    }
    if (!show) {
        return;
    }
    screen->setTitle("Camera3D: " + editor.GetEntityDisplayLabel(selected));
    const prism::Rect rect = screen->viewRect();
    if (rect.w < 8.0F || rect.h < 8.0F) {
        return;
    }
    static Editor::ViewportOverlayState state{};
    state.hostDrawsGizmos = false;
    state.gameWorkspaceActive = false;
    state.studioView = false;
    state.entityAuthoringView = editor.IsEntityDocumentActive();
    state.input = EditorViewportInput{};
    const float dpiScale = std::max(1.0F, prism::Application::get().platform().dpiScale());
    Math::Vector2 used{0.0F, 0.0F};
    const std::uint64_t texture = editor.m_viewports.cameraPreview(
        Math::Vector2{std::max(1.0F, std::floor(rect.w * dpiScale)), std::max(1.0F, std::floor(rect.h * dpiScale))},
        used, state);
    if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
        const float invScale = dpiScale > 1.0F ? (1.0F / dpiScale) : 1.0F;
        EditorHost::ViewImage view{};
        view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x * invScale, used.y * invScale};
        view.texture = static_cast<std::uint32_t>(texture);
        view.usedWidth = used.x;
        view.usedHeight = used.y;
        view.textureWidth = used.x;
        view.textureHeight = used.y;
        views.push_back(view);
    }
}

void EditorShell::Frame() {
    if (!m_host.IsReady()) {
        return;
    }
    m_host.BeginGpuTimer();
    if (m_safeFrame) {
        const std::array<float, 4> area = m_editor.SafeAreaInsets();
        if (area != m_safeArea) {
            m_safeArea = area;
            // Keep the 3D viewport and panels 100% edge-to-edge across camera punch-holes (zero
            // dark side bezels); shift upward only when the soft keyboard (area[3]) is open, and
            // add only minimal left/right clearance to the top title bar row if a cutout overlaps it.
            m_safeFrame->setPadding(prism::Margin(0.0F, 0.0F, 0.0F, area[3]));
            if (m_titleBarRow) {
                const float leftCutout = std::clamp(area[0], 0.0F, 26.0F);
                const float rightCutout = std::clamp(area[2], 0.0F, 26.0F);
                m_titleBarRow->setPadding(prism::Margin(leftCutout, 0.0F, rightCutout, 0.0F));
            }
        }
    }
    // The editor's own work per frame (UI, views, panels), logged every five seconds: what a slow
    // machine pays for the editor itself, apart from waiting on the display.
    const auto frameStart = std::chrono::steady_clock::now();
    struct FrameTimer final {
        EditorShell& shell;
        std::chrono::steady_clock::time_point start;
        ~FrameTimer() {
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
    const bool importingDuringStartup = m_startupCovered && m_editor.m_startupStep < 2;
    if (!m_host.HasOpaqueFullPage() && !importingDuringStartup) {
        RefreshOutliner();
        RefreshDetails();
        RefreshToolbar();
        m_assetTable.Refresh();
        const int page = m_dockPages ? m_dockPages->activeIndex() : 0;
        m_dockPanels.Refresh(m_dockVisible && page == 2, m_dockVisible && page == 3, m_host.GpuFrameMs());
    }
    // The Entity Editor's window follows the entity session; its panels read the entity's document.
    m_entityEditor.Sync();
    m_montageEditor.Sync();
    m_vfxEditor.Sync();
    m_retargetEditor.Sync();
    m_animationLibraryEditor.Sync();
    {
        const Editor::EntityDocumentScope scope(m_editor);
        m_entityEditor.Refresh();
    }
    if (m_loadingScreen) {
        const Editor::EditorLoadingState loading = m_editor.GetLoadingState();
        if (loading.startup && m_projectName.empty() && m_editor.m_projectFileSnapshotInitialized) {
            // The project's folder: the one that holds its content folder.
            std::error_code error;
            std::filesystem::path content = std::filesystem::absolute(m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot, error);
            if (content.filename().empty()) {
                content = content.parent_path();
            }
            m_projectName = content.parent_path().filename().string();
        }
        m_loadingScreen->Sync(loading, m_projectName);
        m_startupCovered = loading.startup;
    }
    // The level view starts gently: nothing while the loading screen covers it, then a frame now
    // and then, more often each second, until it draws every frame (about three seconds), so the
    // device is not hit with the full scene the moment the editor appears.
    bool renderView = !m_startupCovered;
    // While the Entity Editor's own window has focus its view is the one being looked at: the level
    // view keeps its last frame instead of drawing the whole scene again behind it.
    const bool entityEditorFocused = m_entityEditor.WindowId() != 0 && m_host.IsWindowFocused(m_entityEditor.WindowId());
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
        Render::IRenderer3D& renderer = m_editor.m_services->GetRenderer3D();
        if (!m_uploadBudgetSet) {
            renderer.SetMeshUploadBudget(2U);
            m_uploadBudgetSet = true;
        }
        const Render::Renderer3DFrameDiagnostics frame = renderer.GetLastFrameDiagnostics();
        const std::size_t loading = frame.pendingAssetLoads + frame.primitiveMaterialPending + frame.meshUploadsDeferred;
        if (warm < 0.05) {
            m_renderLoadPeak = 0U;
            m_renderLoadDone = false;
            m_renderLoadNames.clear();
        }
        // Names of the meshes just drawn for the first time (an imported part reads as its part name).
        for (const Asset::AssetGuid guid : frame.uploadedMeshes) {
            if (guid == Asset::AssetGuid{}) {
                continue;
            }
            std::string name = m_editor.m_services->GetAssetDatabase().Resolve(guid).stem().string();
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
            m_loadingScreen->SyncRender(!m_renderLoadDone && m_viewport->visible(), m_viewport->rect(), text, detail,
                                           std::min(warmPart, assetPart));
        }
    }
    if (m_loadingScreen && !m_startupCovered) {
        m_loadingScreen->Sync(m_editor.GetLoadingState(), m_projectName);
    }
    m_host.BeginFrame();

    const bool occludedByFullPage = m_host.HasOpaqueFullPage();
    std::vector<EditorHost::ViewImage> views;
    if (occludedByFullPage || (entityEditorFocused && m_lastView.texture != 0U)) {
        renderView = false;
    }
    if (!occludedByFullPage && !renderView && !m_startupCovered && m_viewport && m_viewport->visible() && m_lastView.texture != 0U) {
        views.push_back(m_lastView); // the last frame again, between warm-up frames
    } else if (!occludedByFullPage && renderView && m_viewport && m_viewport->visible() && m_editor.m_viewports.level) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            Editor::ViewportOverlayState& state = m_editor.m_viewportOverlayState;
            state.gameWorkspaceActive = m_editor.m_activeWorkspace == Editor::EditorWorkspace::Game;
            state.studioView = false;
            state.entityAuthoringView = false;
            state.bones.clear();
            state.colliderLines.clear();
            if (!state.gameWorkspaceActive) {
                m_editor.BuildSkeletonOverlay(state.bones);
                m_editor.BuildColliderOverlay(state.colliderLines);
            }
            state.pointerOverOverlay = false;
            state.hostDrawsGizmos = true;
            if (m_gizmos) {
                const std::array<float, 2> orbit = m_gizmos->TakeOrbitRequest();
                state.orbitRequestX = orbit[0];
                state.orbitRequestY = orbit[1];
                m_gizmos->ApplyTouchControllerToState(state);
            }
            state.input = m_host.CaptureViewInput(rect);
            const float dpiScale = std::max(1.0F, state.input.dpiScale);
            const bool compactSurface = prism::Application::get().platform().singleSurface();
            const float renderScale = state.resolvedRenderScale > 0.1F
                                          ? std::clamp(state.resolvedRenderScale, 0.5F, 1.0F)
                                          : (compactSurface ? 0.80F : 1.0F);
            const float effectiveScale = dpiScale * renderScale;
            float renderW = std::max(1.0F, std::floor(rect.w * effectiveScale));
            float renderH = std::max(1.0F, std::floor(rect.h * effectiveScale));
            if (prism::Splitter::anyDragging() && m_lastView.usedWidth >= 1.0F && m_lastView.usedHeight >= 1.0F) {
                // Hold FBO dimensions steady while a panel splitter is being dragged so resizing stays
                // smooth with zero framebuffer reallocation mid-drag; snaps cleanly on release.
                renderW = m_lastView.usedWidth;
                renderH = m_lastView.usedHeight;
            }
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
                m_editor.m_viewports.level(Math::Vector2{renderW, renderH}, used, state);
            m_host.ApplyViewCursor(state.input);
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
                m_lastView = view;
            }
        }
    }
    if (!occludedByFullPage) {
        AppendCameraScreen(m_editor, m_details2, m_cameraScreen.get(), views);
        m_host.Render(views);
    }
    m_host.EndGpuTimer();
    {
        const Editor::EntityDocumentScope scope(m_editor);
        m_entityEditor.Render();
    }
    m_montageEditor.Render();
    m_vfxEditor.Render();
    m_retargetEditor.Render();
    m_animationLibraryEditor.Render();
    m_fileDialog.Frame();
    m_entityClassWindow.Frame();
    m_preferencesWindow.Frame();
    m_projectSettingsWindow.Frame();
    m_shortcutsWindow.Frame();
    m_inputMapWindow.Frame();
    m_captureWindow.Frame();
    m_audioMixerWindow.Frame();
    if (m_editor.m_commandPalette.open) {
        m_editor.m_commandPalette.open = false;
        ShowCommandPalette();
    }
}

} // namespace UVE::Editor
