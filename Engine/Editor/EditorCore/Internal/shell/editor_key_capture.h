// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_commands.h"
#include "editor_shortcut_keys.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Theme.h"
#include "prism/Widget.h"

#include <functional>
#include <string>
#include <utility>

namespace UVE::Editor {

/// A shortcut slot: shows its shortcut ("Ctrl+S", or "-" when empty); a click starts listening
/// ("Press keys..."), the next key with its modifiers becomes the shortcut, Escape cancels.
/// `conflict` draws it in the warning colour (another command has the same shortcut).
class EditorKeyCaptureUVE final : public prism::Widget {
public:
    explicit EditorKeyCaptureUVE(EditorShortcutUVE shortcut) : m_shortcut(shortcut) {}
    std::function<void(const EditorShortcutUVE&)> onCaptured;
    void setConflict(const bool conflict) { m_conflict = conflict; invalidate(); }

    bool focusable() const override { return true; }
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain({120.0F, prism::theme().controlH}); }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRoundRect(m_rect, 3.0F, m_listening ? t.rowSelected : (m_hot ? t.rowHover : t.field));
        c.strokeRoundRect(m_rect, 3.0F, m_listening ? t.accent : t.border, 1);
        std::string text = m_listening ? std::string("Press keys...") : DisplayEditorShortcutUVE(m_shortcut);
        if (text.empty()) {
            text = "-";
        }
        const prism::Color colour = m_listening ? t.textBright : (m_conflict ? prism::Color(238, 178, 77) : t.text);
        c.textIn(*t.ui, prism::Rect{m_rect.x + 8.0F, m_rect.y, m_rect.w - 16.0F, m_rect.h}, text, colour);
    }
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button != prism::MouseButton::Left) {
            return false;
        }
        m_listening = true;
        if (prism::Application::exists()) {
            prism::Application::get().setFocus(this);
        }
        invalidate();
        return true;
    }
    bool onMouseMove(const prism::MouseEvent&) override {
        if (!m_hot) {
            m_hot = true;
            invalidate();
        }
        return false;
    }
    void onMouseLeave() override {
        m_hot = false;
        invalidate();
    }
    void onFocusChanged(const bool focused) override {
        if (!focused && m_listening) {
            m_listening = false;
            invalidate();
        }
    }
    bool onKeyDown(const prism::KeyEvent& e) override {
        if (!m_listening) {
            return false;
        }
        if (e.key == prism::Key::Escape) {
            m_listening = false;
            invalidate();
            return true;
        }
        if (const std::optional<EditorShortcutUVE> pressed = ShortcutFromPrismKeyUVE(e.key, e.mods)) {
            m_listening = false;
            m_shortcut = *pressed;
            invalidate();
            if (onCaptured) {
                onCaptured(*pressed);
            }
        }
        return true;
    }

private:
    EditorShortcutUVE m_shortcut;
    bool m_listening = false;
    bool m_hot = false;
    bool m_conflict = false;
};

} // namespace UVE::Editor
