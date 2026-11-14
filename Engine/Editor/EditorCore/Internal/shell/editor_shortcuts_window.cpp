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

constexpr int kWidthUVE = 760;
constexpr int kHeightUVE = 560;

} // namespace

EditorShortcutsWindowUVE::EditorShortcutsWindowUVE(EditorUVE& editor, EditorHostUVE& host) : m_editor(editor), m_host(host) {}

EditorShortcutsWindowUVE::~EditorShortcutsWindowUVE() = default;

void EditorShortcutsWindowUVE::RebuildUVE() {
    m_dirty = false;
    if (!m_rows) {
        return;
    }
    m_rows->clear();
    const std::string query = m_search ? m_search->text() : std::string();
    std::string category;
    int section = -1;
    for (const EditorCommandUVE& command : m_editor.GetEditorCommandsUVE()) {
        const std::string title = EditorUVE::CommandTitleTextUVE(command);
        const std::string keys = DisplayEditorShortcutUVE(command.shortcuts[0]) + " " + DisplayEditorShortcutUVE(command.shortcuts[1]);
        if (!query.empty() && ScoreFuzzyMatchUVE(title, query) < 0 && ScoreFuzzyMatchUVE(keys, query) < 0) {
            continue;
        }
        if (section < 0 || command.category != category) {
            category = command.category;
            section = m_rows->addCategory(category);
        }
        auto cell = prism::hbox(4.0F);
        const std::string id = command.id;
        for (std::size_t slot = 0U; slot < command.shortcuts.size(); ++slot) {
            auto capture = prism::make<EditorKeyCaptureUVE>(command.shortcuts[slot]);
            const std::vector<std::string> users = m_editor.CommandsUsingShortcutUVE(command.shortcuts[slot], command.id);
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
            capture->onCaptured = [this, id, slot](const EditorShortcutUVE& shortcut) {
                static_cast<void>(m_editor.AssignCommandShortcutUVE(id, slot, shortcut));
                m_dirty = true;
            };
            cell->add(capture, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            auto clear = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
            clear->setTooltip("Remove this shortcut");
            clear->setEnabled(!command.shortcuts[slot].IsEmptyUVE());
            clear->onClick = [this, id, slot] {
                static_cast<void>(m_editor.AssignCommandShortcutUVE(id, slot, EditorShortcutUVE{}));
                m_dirty = true;
            };
            cell->add(clear, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
        reset->setTooltip("Back to its own shortcuts");
        reset->setEnabled(command.shortcuts != command.defaultShortcuts);
        reset->onClick = [this, id] {
            m_editor.ResetCommandShortcutsUVE(id);
            m_dirty = true;
        };
        cell->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        m_rows->addRow(section, command.label, cell, title);
    }
}

prism::Ref<prism::Widget> EditorShortcutsWindowUVE::BuildUVE() {
    auto window = prism::vbox();
    auto title = prism::hbox(6.0F);
    title->setPadding(prism::Margin(10.0F, 0.0F, 0.0F, 0.0F));
    auto titleText = prism::make<prism::Label>("Keyboard Shortcuts");
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    title->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    title->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*title, [this] {
        m_editor.m_shortcutsWindow.visible = false;
        m_closePending = true;
    });
    auto titleBox = prism::make<prism::SizeBox>();
    titleBox->setHeightOverride(kTitleRowUVE + 4.0F);
    titleBox->setContent(title);
    auto titleBar = prism::make<prism::Border>();
    titleBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    titleBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    titleBar->setChild(titleBox);
    window->add(titleBar);

    auto searchRow = prism::hbox(6.0F);
    searchRow->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    m_search = prism::make<prism::TextInput>("", "Search commands or shortcuts");
    m_search->onChanged = [this](const std::string&) { m_dirty = true; };
    searchRow->add(m_search, prism::Slot{1.0F});
    window->add(searchRow);

    m_rows = prism::make<prism::PropertyGrid>();
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_rows);
    window->add(PanelUVE(scroll), prism::Slot{1.0F});
    return window;
}

void EditorShortcutsWindowUVE::FrameUVE() {
    bool& visible = m_editor.m_shortcutsWindow.visible;
    if (m_window != 0 && m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        visible = false;
    }
    if (m_window != 0 && (m_closePending || !visible)) {
        m_host.CloseWindowUVE(m_window);
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
    if (m_window == 0) {
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Keyboard Shortcuts", kWidthUVE, kHeightUVE);
        if (m_window == 0) {
            visible = false;
            return;
        }
        m_host.AppUVE().runInWindow(m_window, [this] {
            RebuildUVE();
            if (m_search && prism::Application::exists()) {
                prism::Application::get().setFocus(m_search.get());
            }
        });
        m_host.FocusWindowUVE(m_window);
    }
    if (m_dirty) {
        m_host.AppUVE().runInWindow(m_window, [this] { RebuildUVE(); });
    }
    m_host.RenderWindowUVE(m_window, {});
}

} // namespace UVE::Editor
