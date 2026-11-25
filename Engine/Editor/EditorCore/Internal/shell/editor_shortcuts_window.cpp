// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_shortcuts_window.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_key_capture.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kWidth = 760;
constexpr int kHeight = 560;

} // namespace

EditorShortcutsWindow::EditorShortcutsWindow(Editor& editor, EditorHost& host) : m_editor(editor), m_host(host) {}

EditorShortcutsWindow::~EditorShortcutsWindow() = default;

void EditorShortcutsWindow::Rebuild() {
    m_dirty = false;
    if (!m_rows) {
        return;
    }
    m_rows->clear();
    const std::string query = m_search ? m_search->text() : std::string();
    std::string category;
    int section = -1;
    for (const EditorCommand& command : m_editor.GetEditorCommands()) {
        const std::string title = Editor::CommandTitleText(command);
        const std::string keys = DisplayEditorShortcut(command.shortcuts[0]) + " " + DisplayEditorShortcut(command.shortcuts[1]);
        if (!query.empty() && ScoreFuzzyMatch(title, query) < 0 && ScoreFuzzyMatch(keys, query) < 0) {
            continue;
        }
        if (section < 0 || command.category != category) {
            category = command.category;
            section = m_rows->addCategory(category);
        }
        auto cell = prism::hbox(4.0F);
        const std::string id = command.id;
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            auto capture = prism::make<EditorKeyCapture>(command.shortcuts[slot]);
            const std::vector<std::string> users = m_editor.CommandsUsingShortcut(command.shortcuts[slot], command.id);
            if (!users.empty()) {
                capture->setConflict(true);
                std::string tip = "Also the shortcut for:";
                for (const std::string& user : users) {
                    tip += "\n  " + user;
                }
                capture->setTooltip(tip);
            } else {
                capture->setTooltip(slot == 0U ? "Click, then press the new shortcut." : "An alternate shortcut: click, then press it.");
            }
            capture->onCaptured = [this, id, slot](const EditorShortcut& shortcut) {
                static_cast<void>(m_editor.AssignCommandShortcut(id, slot, shortcut));
                m_dirty = true;
            };
            cell->add(capture, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            auto clear = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
            clear->setTooltip("Remove this shortcut");
            clear->setEnabled(!command.shortcuts[slot].IsEmpty());
            clear->onClick = [this, id, slot] {
                static_cast<void>(m_editor.AssignCommandShortcut(id, slot, EditorShortcut{}));
                m_dirty = true;
            };
            cell->add(clear, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
        reset->setTooltip("Back to its own shortcuts");
        reset->setEnabled(command.shortcuts != command.defaultShortcuts);
        reset->onClick = [this, id] {
            m_editor.ResetCommandShortcuts(id);
            m_dirty = true;
        };
        cell->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        m_rows->addRow(section, command.label, cell, title);
    }
}

prism::Ref<prism::Widget> EditorShortcutsWindow::Build() {
    auto window = prism::vbox();
    window->add(BuildToolWindowTitleBar("Keyboard Shortcuts", prism::Icon::Settings, [this] {
        m_editor.m_shortcutsWindow.visible = false;
        m_closePending = true;
    }));

    auto searchRow = prism::hbox(6.0F);
    searchRow->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    m_search = prism::make<prism::TextInput>("", "Search commands or shortcuts");
    m_search->onChanged = [this](const std::string&) { m_dirty = true; };
    searchRow->add(m_search, prism::Slot{1.0F});
    window->add(searchRow);

    m_rows = prism::make<prism::PropertyGrid>();
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_rows);
    window->add(Panel(scroll), prism::Slot{1.0F});
    return window;
}

void EditorShortcutsWindow::Frame() {
    bool& visible = m_editor.m_shortcutsWindow.visible;
    if (m_window != 0 && m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        visible = false;
    }
    if (m_window != 0 && (m_closePending || !visible)) {
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_closePending = false;
        m_search.reset();
        m_rows.reset();
        return;
    }
    m_closePending = false;
    if (!visible) {
        return;
    }
    const bool freshlyOpened = (m_window == 0);
    if (freshlyOpened) {
        m_window = m_host.OpenWindow(Build(), "Keyboard Shortcuts", kWidth, kHeight);
        if (m_window == 0) {
            visible = false;
            return;
        }
        m_host.App().runInWindow(m_window, [this] {
            Rebuild();
            if (m_search && prism::Application::exists() &&
                !prism::Application::get().platform().singleSurface()) {
                prism::Application::get().setFocus(m_search.get());
            }
        });
    }
    if (freshlyOpened || m_editor.m_shortcutsWindow.focusPending) {
        m_editor.m_shortcutsWindow.focusPending = false;
        m_host.FocusWindow(m_window);
    }
    if (m_dirty) {
        m_host.App().runInWindow(m_window, [this] { Rebuild(); });
    }
    m_host.RenderWindow(m_window, {});
}

} // namespace UVE::Editor
