// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_shell.h"

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

/// The viewport shading modes, in ViewportOverlayStateUVE::shadingMode order.
constexpr std::array<const char*, 5> kShadingNamesUVE{"Lit", "Unshaded", "Wireframe", "Lighting", "Normals"};

[[nodiscard]] prism::Color ToPrismColorUVE(const float r, const float g, const float b) {
    const auto c = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(c(r), c(g), c(b), 255);
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

EditorPrismShellUVE::EditorPrismShellUVE(EditorUVE& editor)
    : m_editor(editor), m_contentBrowser(editor), m_details2(editor), m_dockPanels(editor) {}

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
    window->add(BuildSubRow2UVE());

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

// Title bar (#15) and viewport-name tab (#2) row heights, and the logo box (#1) spanning both.
constexpr float kTitleRowUVE = 24.0F;
constexpr float kTabRowUVE = 24.0F;
constexpr float kLogoBoxUVE = kTitleRowUVE + kTabRowUVE; // square: as wide as the two rows are tall
// Sub row 2: the viewport settings strip.
constexpr float kSubRow2UVE = 27.0F;

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

/// #1: the engine logo box. It spans the title bar row and the viewport-name tab row, the same
/// mark the editor has always shown there (a rounded blue square with the U), kept small like a
/// desktop editor's app mark so the title bar stays thin.
class LogoBoxUVE final : public prism::Widget {
public:
    prism::Vec2 measure(prism::Vec2) override {
        return m_desired = constrain(prism::Vec2(kLogoBoxUVE, kTitleRowUVE + kTabRowUVE));
    }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        c.fillRect(r, t.header);
        // The slot hugs the mark: a few pixels of the bar around it, no more.
        const float side = std::floor(std::min(r.w, r.h) - 8.0F);
        const prism::Rect mark{r.x + (r.w - side) * 0.5F, r.y + (r.h - side) * 0.5F, side, side};
        c.fillRoundRect(mark, 5.0F, prism::Color(32, 112, 228));
        c.strokeRoundRect(mark, 5.0F, prism::Color(82, 160, 255, 220), 1);
        // The U, drawn: two stems joined by a rounded bowl.
        const float uw = side * 0.46F;
        const float uh = side * 0.52F;
        const float th = std::max(2.0F, std::round(side * 0.12F));
        const float ux = mark.x + (mark.w - uw) * 0.5F;
        const float uy = mark.y + (mark.h - uh) * 0.5F;
        const prism::Color white(255, 255, 255);
        c.fillRect(prism::Rect{ux, uy, th, uh - uw * 0.5F}, white);
        c.fillRect(prism::Rect{ux + uw - th, uy, th, uh - uw * 0.5F}, white);
        const prism::Vec2 bowl{ux + uw * 0.5F, uy + uh - uw * 0.5F};
        c.fillCircle(bowl, uw * 0.5F, white);
        c.fillCircle(bowl, uw * 0.5F - th, prism::Color(32, 112, 228));
        c.fillRect(prism::Rect{ux + th, uy, uw - 2.0F * th, uh - uw * 0.5F}, prism::Color(32, 112, 228));
        c.fillRect(prism::Rect{r.right() - 1.0F, r.y, 1.0F, r.h}, t.border);
    }
};

/// #2: the document's Viewport - the same object the Outliner starts at (it holds the scene), with
/// the same name and the same icon. Clicking it selects that Viewport.
class ViewportNameTabUVE final : public prism::Widget {
public:
    void setIcon(const std::uint32_t* pixels, const int size) {
        if (pixels != m_icon) {
            m_icon = pixels;
            m_iconSize = size;
            invalidate();
        }
    }
    std::function<void()> onClick;
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Left && onClick) {
            onClick();
            return true;
        }
        return false;
    }
    prism::Vec2 measure(prism::Vec2) override {
        const float w = std::max(170.0F, std::ceil(prism::theme().ui->width(m_name)) + 60.0F);
        return m_desired = constrain(prism::Vec2(w, kTabRowUVE));
    }
    void setName(std::string name) {
        if (name != m_name) {
            m_name = std::move(name);
            relayout();
        }
    }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        const prism::Rect tab{r.x + 6.0F, r.y + 2.0F, r.w - 6.0F, r.h - 2.0F};
        c.fillRoundRect(tab, 5.0F, t.panel);
        c.fillRect(prism::Rect{tab.x, tab.bottom() - 5.0F, tab.w, 5.0F}, t.panel);
        c.fillRect(prism::Rect{tab.x, tab.y, tab.w, 2.0F}, t.accent);
        const prism::Rect iconBox{tab.x + 10.0F, std::floor(tab.y + (tab.h - 16.0F) * 0.5F), 16.0F, 16.0F};
        if (m_icon != nullptr && m_iconSize > 0) {
            c.blitBlend(m_icon, m_iconSize, m_iconSize, m_iconSize, iconBox);
        } else {
            prism::drawIcon(c, prism::Icon::Globe, iconBox, t.accentHover);
        }
        c.textIn(*t.ui, prism::Rect{tab.x + 32.0F, tab.y, tab.w - 40.0F, tab.h}, m_name, t.textBright, prism::HAlign::Left,
                 true);
    }

private:
    std::string m_name = "Viewport";
    const std::uint32_t* m_icon = nullptr;
    int m_iconSize = 0;
};

/// #15: the drag strip. Reports itself as the window caption (the host moves the window from it);
/// a double click toggles maximize.
class DragStripUVE final : public prism::Widget {
public:
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain(prism::Vec2(40.0F, kTitleRowUVE)); }
    void arrange(prism::Rect r) override {
        prism::Widget::arrange(r);
        if (prism::Application::exists()) {
            prism::Application::get().platform().setWindowCaption(0, r);
        }
    }
    void paint(prism::Canvas& c) override {
        const prism::Rect r = rect();
        const prism::Rect hatch{r.x + 12.0F, r.y + 7.0F, std::max(0.0F, r.w - 24.0F), r.h - 14.0F};
        if (hatch.w > 4.0F) {
            c.pushClip(hatch);
            for (float x = hatch.x - hatch.h; x < hatch.right(); x += 6.0F) {
                c.line(prism::Vec2(x, hatch.bottom()), prism::Vec2(x + hatch.h, hatch.y), prism::Color(255, 255, 255, 12), 1.0F);
            }
            c.popClip();
        }
    }
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Left && e.clicks >= 2 && prism::Application::exists()) {
            prism::Application::get().platform().toggleMaximizeWindow(0);
        }
        return e.button == prism::MouseButton::Left;
    }
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
    auto minimize = prism::make<prism::Button>("", prism::Icon::Minus, prism::ButtonStyle::Flat);
    minimize->setTooltip("Minimize");
    minimize->onClick = [] { prism::Application::get().platform().minimizeWindow(0); };
    auto maximize = prism::make<prism::Button>("", prism::Icon::Frame, prism::ButtonStyle::Flat);
    maximize->setTooltip("Maximize / Restore");
    maximize->onClick = [] { prism::Application::get().platform().toggleMaximizeWindow(0); };
    auto close = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
    close->setTooltip("Close");
    close->onClick = [] { prism::Application::get().quit(); };
    row->add(minimize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(maximize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(close, prism::Slot{0.0F, prism::Margin(0.0F, 0.0F, 4.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});

    // [#1 logo] [ #15 title row      ]
    //           [ #2 viewport name tab ]
    auto tabRow = prism::hbox();
    auto viewportTab = prism::make<ViewportNameTabUVE>();
    viewportTab->onClick = [this] {
        const Scene::EntityUVE viewport = m_editor.GetDocumentViewportUVE();
        if (viewport != Scene::kInvalidEntityUVE) {
            m_editor.SelectEntityUVE(viewport);
        }
    };
    m_viewportNameTab = viewportTab;
    tabRow->add(m_viewportNameTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    tabRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
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

prism::Ref<prism::Widget> EditorPrismShellUVE::BuildSubRow2UVE() {
    // Sub row 2 (full width): gizmo mode, snapping, grid, projection - the viewport settings row.
    auto settings = prism::hbox(4.0F);
    settings->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
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
    settings->add(m_snapButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_gridButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparatorUVE());

    // Projection and facing: the engine's own setters (SetViewportOrthographicUVE /
    // RequestViewportViewUVE), the same entries the previous toolbar had.
    using ViewUVE = EditorUVE::ViewportViewUVE;
    m_projectionButton = prism::make<prism::Button>("Perspective", prism::Icon::Camera, prism::ButtonStyle::Toolbar);
    m_projectionButton->setTooltip("Projection and facing");
    m_projectionButton->onClick = [this] {
        EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Perspective", !state.orthographic && state.view == ViewUVE::User, [this] {
            m_editor.SetViewportOrthographicUVE(false);
            m_editor.RequestViewportViewUVE(ViewUVE::User);
        }));
        items.push_back(prism::MenuItem::Check("Orthographic", state.orthographic && state.view == ViewUVE::User, [this] {
            m_editor.SetViewportOrthographicUVE(true);
            m_editor.RequestViewportViewUVE(ViewUVE::User);
        }));
        items.push_back(prism::MenuItem::Sep());
        for (const ViewUVE view : {ViewUVE::Top, ViewUVE::Bottom, ViewUVE::Front, ViewUVE::Back, ViewUVE::Right, ViewUVE::Left}) {
            items.push_back(prism::MenuItem::Check(EditorUVE::GetViewportViewNameUVE(view), state.view == view,
                                                   [this, view] { m_editor.RequestViewportViewUVE(view); }));
        }
        prism::showMenu(std::move(items), m_projectionButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_projectionButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    // Lit: the viewport shading modes.
    m_shadingButton = prism::make<prism::Button>("Lit", prism::Icon::Shaded, prism::ButtonStyle::Toolbar);
    m_shadingButton->setTooltip("Shading");
    m_shadingButton->onClick = [this] {
        std::vector<prism::MenuItem> items;
        for (std::size_t i = 0; i < kShadingNamesUVE.size(); ++i) {
            items.push_back(prism::MenuItem::Check(kShadingNamesUVE[i],
                                                   m_editor.m_viewportOverlayState.shadingMode == static_cast<int>(i),
                                                   [this, i] { m_editor.m_viewportOverlayState.shadingMode = static_cast<int>(i); }));
        }
        prism::showMenu(std::move(items), m_shadingButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_shadingButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparatorUVE());

    // Preview: the editor-only sun and sky, and their settings.
    m_sunButton = ToolButtonUVE(prism::Icon::Light, "Preview sun");
    m_sunButton->setCheckable(true);
    m_sunButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewSun = on; };
    m_skyButton = ToolButtonUVE(prism::Icon::Globe, "Preview sky");
    m_skyButton->setCheckable(true);
    m_skyButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewEnvironment = on; };
    auto previewSettings = prism::make<prism::Button>("Preview", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    previewSettings->setTooltip("Preview lighting & sky");
    std::weak_ptr<prism::Button> weakPreview = previewSettings;
    previewSettings->onClick = [this, weakPreview] {
        if (auto anchor = weakPreview.lock()) {
            ShowPreviewSettingsUVE(anchor->rect());
        }
    };
    settings->add(m_sunButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_skyButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(previewSettings, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::panel));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(kSubRow2UVE);
    sized->setContent(settings);
    bar->setChild(sized);
    return bar;
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

void EditorPrismShellUVE::ShowPreviewSettingsUVE(const prism::Rect anchor) {
    // Preview lighting & sky: the editor-only sun and sky the viewport uses while the scene has no
    // DirectionalLight3D / WorldEnvironment3D of its own - the same fields the previous popup edited.
    EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
    const auto color3 = [](const std::array<float, 3>& c) { return ToPrismColorUVE(c[0], c[1], c[2]); };
    const auto store3 = [](std::array<float, 3>& out, const prism::Color c) {
        out = {static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F, static_cast<float>(c.b) / 255.0F};
    };

    auto grid = prism::make<prism::PropertyGrid>();
    const int sun = grid->addCategory("Preview Sun", true);
    auto sunOn = prism::make<prism::CheckBox>("", state.previewSun);
    sunOn->onChange = [this](const bool on) { m_editor.m_viewportOverlayState.previewSun = on; };
    grid->addRow(sun, "Enabled", sunOn);
    auto sunColor = prism::make<prism::ColorField>(color3(state.previewSunColor));
    sunColor->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewSunColor, c); };
    grid->addRow(sun, "Color", sunColor);
    const auto slider = [](const float lo, const float hi, const float value, const int decimals, const char* suffix) {
        auto s = prism::make<prism::Slider>(lo, hi, value);
        s->setDecimals(decimals);
        if (suffix != nullptr) {
            s->setSuffix(suffix);
        }
        return s;
    };
    auto energy = slider(0.0F, 8.0F, state.previewSunEnergy, 2, nullptr);
    energy->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunEnergy = v; };
    grid->addRow(sun, "Energy", energy);
    auto azimuth = slider(-180.0F, 180.0F, state.previewSunAzimuth, 0, " deg");
    azimuth->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunAzimuth = v; };
    grid->addRow(sun, "Azimuth", azimuth, "Horizontal compass bearing around the world Y axis.");
    auto altitude = slider(-90.0F, 90.0F, state.previewSunAltitude, 0, " deg");
    altitude->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunAltitude = v; };
    grid->addRow(sun, "Altitude", altitude, "Elevation above the horizon. Higher values cast shorter shadows.");
    auto addSun = prism::make<prism::Button>("Add Sun to Scene", prism::Icon::Plus, prism::ButtonStyle::Primary);
    addSun->setEnabled(m_editor.IsAuthoringCommandAllowedUVE());
    addSun->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewSunToSceneUVE());
        prism::Application::get().closeAllPopups();
    };
    grid->addRow(sun, "", addSun, "Creates a DirectionalLight3D with these sun settings and shadows on.");

    const int sky = grid->addCategory("Preview Sky", true);
    auto skyOn = prism::make<prism::CheckBox>("", state.previewEnvironment);
    skyOn->onChange = [this](const bool on) { m_editor.m_viewportOverlayState.previewEnvironment = on; };
    grid->addRow(sky, "Enabled", skyOn);
    auto skyColor = prism::make<prism::ColorField>(color3(state.previewSkyColor));
    skyColor->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewSkyColor, c); };
    grid->addRow(sky, "Sky", skyColor);
    auto horizon = prism::make<prism::ColorField>(color3(state.previewHorizonColor));
    horizon->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewHorizonColor, c); };
    grid->addRow(sky, "Horizon", horizon);
    auto ground = prism::make<prism::ColorField>(color3(state.previewGroundColor));
    ground->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewGroundColor, c); };
    grid->addRow(sky, "Ground", ground);
    auto ambient = slider(0.0F, 4.0F, state.previewAmbientEnergy, 2, nullptr);
    ambient->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewAmbientEnergy = v; };
    grid->addRow(sky, "Ambient", ambient, "Sky and ground ambient light intensity across the scene.");
    auto addEnv = prism::make<prism::Button>("Add Environment to Scene", prism::Icon::Plus, prism::ButtonStyle::Primary);
    addEnv->setEnabled(m_editor.IsAuthoringCommandAllowedUVE());
    addEnv->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewEnvironmentToSceneUVE());
        prism::Application::get().closeAllPopups();
    };
    grid->addRow(sky, "", addEnv, "Creates a WorldEnvironment3D carrying these sky and ambient settings.");

    auto header = prism::hbox(4.0F);
    header->add(prism::make<prism::Label>("Preview Lighting & Sky", prism::FontRole::Semibold),
                prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto reset = prism::make<prism::Button>("Reset", prism::Icon::Refresh, prism::ButtonStyle::Flat);
    reset->onClick = [this, anchor] {
        EditorUVE::ViewportOverlayStateUVE& s = m_editor.m_viewportOverlayState;
        const EditorUVE::ViewportOverlayStateUVE defaults{};
        s.previewSun = defaults.previewSun;
        s.previewSunColor = defaults.previewSunColor;
        s.previewSunEnergy = defaults.previewSunEnergy;
        s.previewSunAzimuth = defaults.previewSunAzimuth;
        s.previewSunAltitude = defaults.previewSunAltitude;
        s.previewEnvironment = defaults.previewEnvironment;
        s.previewSkyColor = defaults.previewSkyColor;
        s.previewHorizonColor = defaults.previewHorizonColor;
        s.previewGroundColor = defaults.previewGroundColor;
        s.previewAmbientEnergy = defaults.previewAmbientEnergy;
        prism::Application::get().closeAllPopups();
        ShowPreviewSettingsUVE(anchor); // reopen showing the defaults
    };
    header->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto body = prism::vbox(6.0F);
    body->setPadding(prism::Margin(10.0F));
    body->add(header);
    body->add(grid, prism::Slot{1.0F});
    auto size = prism::make<prism::SizeBox>();
    size->setWidthOverride(340.0F);
    size->setHeightOverride(430.0F);
    size->setContent(body);
    auto panel = prism::make<prism::Border>();
    panel->setBackground(prism::ThemeColor(&prism::Theme::panel));
    panel->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    panel->setChild(size);
    prism::PopupOptions options{};
    options.placement = prism::PopupPlacement::Below;
    options.anchor = anchor;
    prism::Application::get().showPopup(panel, options);
}

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
    if (m_snapButton) {
        m_snapButton->setChecked(m_editor.m_transformSnappingSettings.enabled);
    }
    if (m_gridButton) {
        m_gridButton->setChecked(m_editor.m_viewportOverlayState.gridVisible);
    }
    {
        const EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
        if (m_projectionButton) {
            const std::string text = state.view != EditorUVE::ViewportViewUVE::User
                                         ? std::string(EditorUVE::GetViewportViewNameUVE(state.view))
                                         : (state.orthographic ? "Orthographic" : "Perspective");
            if (m_projectionButton->text() != text) {
                m_projectionButton->setText(text);
            }
        }
        if (m_shadingButton) {
            const std::string text =
                kShadingNamesUVE[static_cast<std::size_t>(std::clamp(state.shadingMode, 0, static_cast<int>(kShadingNamesUVE.size()) - 1))];
            if (m_shadingButton->text() != text) {
                m_shadingButton->setText(text);
            }
        }
        if (m_sunButton) {
            m_sunButton->setChecked(state.previewSun);
        }
        if (m_skyButton) {
            m_skyButton->setChecked(state.previewEnvironment);
        }
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
    if (m_viewportNameTab) {
        // The same Viewport object the Outliner shows: its own name and its own icon.
        auto* tab = static_cast<ViewportNameTabUVE*>(m_viewportNameTab.get());
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
}

} // namespace UVE::Editor
