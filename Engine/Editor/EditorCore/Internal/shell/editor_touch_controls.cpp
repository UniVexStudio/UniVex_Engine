// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_touch_controls.h"

#if defined(__ANDROID__)
#include "android_glfw.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Theme.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#endif

namespace UVE::Editor {

#if defined(__ANDROID__)
namespace {

[[nodiscard]] bool TouchOnlyUVE() {
    return !Window::AndroidGlfwHasHardwareInputUVE();
}

/// Sends a key as a keyboard would - modifiers down, key down and up, modifiers up - so the shortcut
/// reaches whatever had focus (a Blueprint's own undo, the editor's Save, ...). The latched
/// Ctrl/Shift/Alt are left out: Undo is Ctrl+Z even with Shift latched.
void SendChordUVE(const int key, const int mods) {
    static constexpr std::array<std::pair<int, int>, 3> kModKeys{{
        {GLFW_MOD_CONTROL, GLFW_KEY_LEFT_CONTROL},
        {GLFW_MOD_SHIFT, GLFW_KEY_LEFT_SHIFT},
        {GLFW_MOD_ALT, GLFW_KEY_LEFT_ALT},
    }};
    const int sticky = Window::AndroidGlfwStickyModsUVE();
    Window::SetAndroidGlfwStickyModsUVE(0);
    int held = 0;
    for (const auto& [bit, modKey] : kModKeys) {
        if ((mods & bit) != 0) {
            held |= bit;
            Window::DispatchAndroidGlfwKeyUVE(modKey, GLFW_PRESS, held);
        }
    }
    Window::DispatchAndroidGlfwKeyUVE(key, GLFW_PRESS, held);
    Window::DispatchAndroidGlfwKeyUVE(key, GLFW_RELEASE, held);
    for (auto it = kModKeys.rbegin(); it != kModKeys.rend(); ++it) {
        if ((mods & it->first) != 0) {
            held &= ~it->first;
            Window::DispatchAndroidGlfwKeyUVE(it->second, GLFW_RELEASE, held);
        }
    }
    Window::SetAndroidGlfwStickyModsUVE(sticky);
}

/// A panel button that leaves keyboard focus where it was, so the shortcut it sends goes to the panel
/// or graph being worked in, not to the button.
class PanelButtonUVE final : public prism::Button {
public:
    using prism::Button::Button;
    bool focusable() const override { return false; }
};

/// The toolbar's Controls button: hidden (not drawn, not hit) while a mouse or keyboard is attached.
class ControlsToolButtonUVE final : public prism::Button {
public:
    using prism::Button::Button;
    bool focusable() const override { return false; }
    void paint(prism::Canvas& c) override {
        if (TouchOnlyUVE()) {
            prism::Button::paint(c);
        }
    }
    prism::Widget* hitTest(const prism::Vec2 p) override { return TouchOnlyUVE() ? prism::Button::hitTest(p) : nullptr; }
};

std::weak_ptr<prism::Widget> g_panelUVE;
prism::Vec2 g_panelPosUVE{-1.0F, -1.0F};   // where the panel was last left, for the session

/// The panel's title bar: dragging it moves the panel.
class PanelTitleUVE final : public prism::Border {
public:
    std::weak_ptr<prism::Widget> panel;
    bool onMouseDown(const prism::MouseEvent& e) override {
        const auto p = panel.lock();
        if (e.button != prism::MouseButton::Left || !p) {
            return false;
        }
        m_grab = e.pos - prism::Vec2(p->rect().x, p->rect().y);
        m_drag = true;
        return true;
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        if (!m_drag || std::abs(e.pos.x) > 1.0e4F) {   // far off-screen: a touch host's cancel
            return m_drag;
        }
        if (const auto p = panel.lock()) {
            prism::Application::get().movePopup(p.get(), e.pos - m_grab);
            g_panelPosUVE = {p->rect().x, p->rect().y};
        }
        return true;
    }
    bool onMouseUp(const prism::MouseEvent&) override {
        const bool was = m_drag;
        m_drag = false;
        return was;
    }
    bool hitSelf() const override { return true; }
    prism::Cursor cursor() const override { return prism::Cursor::SizeAll; }

private:
    prism::Vec2 m_grab;
    bool m_drag = false;
};

void ClosePanelUVE() {
    if (const auto p = g_panelUVE.lock()) {
        prism::Application::get().closePopup(p.get());
    }
    g_panelUVE.reset();
}

void OpenPanelUVE(const prism::Rect anchor) {
    auto root = prism::make<prism::Border>();
    root->setBackground(prism::ThemeColor(&prism::Theme::panel));
    root->setBorder(prism::ThemeColor(&prism::Theme::borderStrong));
    root->setRadius(6.0F);
    const std::weak_ptr<prism::Widget> weakRoot = root;

    auto col = prism::vbox(4.0F);
    auto title = prism::make<PanelTitleUVE>();
    title->panel = weakRoot;
    title->setBackground(prism::ThemeColor(&prism::Theme::header));
    title->setPadding(prism::Margin(10.0F, 0.0F, 2.0F, 0.0F));
    title->setFixedHeight(28.0F);
    auto titleRow = prism::hbox();
    titleRow->add(prism::make<prism::Label>("Controls", prism::FontRole::Semibold), prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto close = prism::make<PanelButtonUVE>("", prism::Icon::Close, prism::ButtonStyle::Toolbar);
    close->onClick = [] { ClosePanelUVE(); };
    titleRow->add(close, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    title->setChild(titleRow);
    col->add(title);

    // Each action runs on the next tick, outside this button's own click handling.
    const auto action = [](std::string label, const prism::Icon icon, const int key, const int mods) {
        auto b = prism::make<PanelButtonUVE>(std::move(label), icon, prism::ButtonStyle::Default);
        b->setMinSize(76.0F, 34.0F);
        b->onClick = [key, mods] {
            prism::Application::get().addTimer(0, [key, mods] {
                SendChordUVE(key, mods);
                return false;
            });
        };
        return b;
    };
    // Ctrl, Shift and Alt latch: lit while on, they ride along every tap and key until tapped off.
    const auto latch = [](std::string label, const int bit) {
        auto b = prism::make<PanelButtonUVE>(std::move(label), prism::Icon::None, prism::ButtonStyle::Default);
        b->setMinSize(76.0F, 34.0F);
        b->setCheckable(true);
        b->setChecked((Window::AndroidGlfwStickyModsUVE() & bit) != 0);
        b->onToggle = [bit](const bool on) {
            const int mods = Window::AndroidGlfwStickyModsUVE();
            Window::SetAndroidGlfwStickyModsUVE(on ? (mods | bit) : (mods & ~bit));
        };
        return b;
    };
    const auto row = [](std::initializer_list<prism::Ref<prism::Widget>> buttons) {
        auto r = prism::hbox(4.0F);
        for (const prism::Ref<prism::Widget>& b : buttons) {
            r->add(b, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        return r;
    };
    auto grid = prism::vbox(4.0F);
    grid->setPadding(prism::Margin(6.0F, 2.0F, 6.0F, 6.0F));
    grid->add(row({action("Undo", prism::Icon::Undo, GLFW_KEY_Z, GLFW_MOD_CONTROL),
                   action("Redo", prism::Icon::Redo, GLFW_KEY_Y, GLFW_MOD_CONTROL)}));
    grid->add(row({latch("Ctrl", GLFW_MOD_CONTROL), latch("Shift", GLFW_MOD_SHIFT), latch("Alt", GLFW_MOD_ALT)}));
    grid->add(row({action("Delete", prism::Icon::Trash, GLFW_KEY_DELETE, 0),
                   action("Duplicate", prism::Icon::Duplicate, GLFW_KEY_D, GLFW_MOD_CONTROL)}));
    grid->add(row({action("Save", prism::Icon::Save, GLFW_KEY_S, GLFW_MOD_CONTROL),
                   action("Copy", prism::Icon::Copy, GLFW_KEY_C, GLFW_MOD_CONTROL),
                   action("Paste", prism::Icon::Paste, GLFW_KEY_V, GLFW_MOD_CONTROL)}));
    col->add(grid);
    root->setChild(col);
    root->setFixedWidth(270.0F);

    prism::PopupOptions o;
    o.placement = prism::PopupPlacement::AtPoint;
    o.anchor = g_panelPosUVE.x >= 0.0F ? prism::Rect{g_panelPosUVE.x, g_panelPosUVE.y, 0.0F, 0.0F}
                                       : prism::Rect{anchor.x, anchor.bottom() + 4.0F, 0.0F, 0.0F};
    o.modal = false;
    o.dismissOnOutside = false;   // it floats: the work goes on under it
    o.onClose = [] { g_panelUVE.reset(); };
    prism::Application::get().showPopup(root, o);
    g_panelUVE = root;
}

} // namespace

prism::Ref<prism::Widget> MakeTouchControlsButtonUVE() {
    auto button = prism::make<ControlsToolButtonUVE>("", prism::Icon::Dots, prism::ButtonStyle::Toolbar);
    button->setTooltip("Controls: Undo, Redo, Ctrl/Shift/Alt, Delete, Duplicate, Save, Copy, Paste");
    prism::Button* const self = button.get();
    button->onClick = [self] {
        if (g_panelUVE.lock()) {
            ClosePanelUVE();
        } else {
            OpenPanelUVE(self->rect());
        }
    };
    return button;
}

#else

prism::Ref<prism::Widget> MakeTouchControlsButtonUVE() {
    return nullptr;
}

#endif

} // namespace UVE::Editor
