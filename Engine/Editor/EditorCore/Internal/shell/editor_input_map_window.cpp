// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_input_map_window.h"
#include "editor_layout_store.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_input_map.h"

#include "input_binding.h"
#include "input_names.h"
#include "key_code.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/SearchMenu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using Input::InputActionType;
using Input::InputAction;
using Input::InputBinding;

constexpr int kWidth = 860;
constexpr int kHeight = 560;

/// The engine key a PRISM key is (letters, digits, function keys, arrows and the named keys).
[[nodiscard]] std::optional<Input::KeyCode> KeyCodeOf(const prism::Key key) {
    const int k = static_cast<int>(key);
    if (key >= prism::Key::A && key <= prism::Key::Z) {
        return static_cast<Input::KeyCode>(static_cast<int>(Input::KeyCode::A) + (k - static_cast<int>(prism::Key::A)));
    }
    if (key >= prism::Key::N0 && key <= prism::Key::N9) {
        return static_cast<Input::KeyCode>(static_cast<int>(Input::KeyCode::Num0) + (k - static_cast<int>(prism::Key::N0)));
    }
    if (key >= prism::Key::F1 && key <= prism::Key::F12) {
        return static_cast<Input::KeyCode>(static_cast<int>(Input::KeyCode::F1) + (k - static_cast<int>(prism::Key::F1)));
    }
    switch (key) {
        case prism::Key::Up: return Input::KeyCode::Up;
        case prism::Key::Down: return Input::KeyCode::Down;
        case prism::Key::Left: return Input::KeyCode::Left;
        case prism::Key::Right: return Input::KeyCode::Right;
        case prism::Key::Space: return Input::KeyCode::Space;
        case prism::Key::Enter: return Input::KeyCode::Enter;
        case prism::Key::Tab: return Input::KeyCode::Tab;
        case prism::Key::Backspace: return Input::KeyCode::Backspace;
        default: return std::nullopt;
    }
}

/// The banner shown while listening for an input. It holds the keyboard: a key pressed is a key to
/// bind (Escape cancels); a gamepad is read by the window each frame.
class InputCapture final : public prism::Widget {
public:
    explicit InputCapture(EditorInputMapWindow& window) : m_window(window) {}
    bool focusable() const override { return true; }
    prism::Vec2 measure(prism::Vec2 avail) override {
        return m_desired = constrain({avail.x, m_window.Listening() ? 44.0F : 0.0F});
    }
    void paint(prism::Canvas& c) override {
        if (!m_window.Listening()) {
            return;
        }
        const prism::Theme& t = prism::theme();
        c.fillRoundRect(m_rect, 4.0F, t.rowSelected);
        c.strokeRoundRect(m_rect, 4.0F, t.highlight, 1);
        c.textIn(*t.ui, prism::Rect{m_rect.x + 12.0F, m_rect.y, m_rect.w - 24.0F, m_rect.h},
                 "Press a key or a gamepad button, or push a stick.   Esc cancels.", t.textBright);
    }
    bool onMouseDown(const prism::MouseEvent&) override { return m_window.Listening(); }
    bool onKeyDown(const prism::KeyEvent& e) override {
        if (!m_window.Listening()) {
            return false;
        }
        if (e.key == prism::Key::Escape) {
            m_window.CancelListening();
        } else if (const std::optional<Input::KeyCode> code = KeyCodeOf(e.key)) {
            m_window.Captured(Input::KeyBinding(*code));
        }
        return true;
    }

private:
    EditorInputMapWindow& m_window;
};

[[nodiscard]] std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

EditorInputMapWindow::EditorInputMapWindow(Editor& editor, EditorHost& host) : m_editor(editor), m_host(host) {}

EditorInputMapWindow::~EditorInputMapWindow() = default;

bool EditorInputMapWindow::Listening() const noexcept {
    return m_editor.m_inputMapWindow.listening;
}

void EditorInputMapWindow::CancelListening() {
    m_editor.m_inputMapWindow.listening = false;
    m_detailDirty = true;
}

void EditorInputMapWindow::Commit(std::vector<InputAction> actions) {
    m_editor.CommitInputMap(std::move(actions));
    m_listDirty = true;
    m_detailDirty = true;
}

void EditorInputMapWindow::Listen(const bool negative, const std::optional<std::size_t> replace) {
    auto& state = m_editor.m_inputMapWindow;
    state.listening = true;
    state.listenAction = state.selected;
    state.listenNegative = negative;
    state.listenReplace = replace;
    m_detailDirty = true;
}

void EditorInputMapWindow::Captured(const InputBinding& binding) {
    auto& state = m_editor.m_inputMapWindow;
    std::vector<InputAction> actions = m_editor.m_services->GetInputMap().GetActions();
    if (state.listening && state.listenAction < actions.size()) {
        InputAction& target = actions[state.listenAction];
        std::vector<InputBinding>& bindings = state.listenNegative ? target.negativeBindings : target.positiveBindings;
        if (state.listenReplace && *state.listenReplace < bindings.size()) {
            bindings[*state.listenReplace] = binding;
        } else {
            bindings.push_back(binding);
        }
        state.listening = false;
        Commit(std::move(actions));
        return;
    }
    state.listening = false;
    m_detailDirty = true;
}

void EditorInputMapWindow::ShowChooseMenu(const bool negative, const prism::Rect anchor) {
    // Every input by device, for binding one without pressing it (a pad not connected, a mouse
    // button, a modifier key).
    const auto pick = [this, negative](const InputBinding binding) {
        return [this, negative, binding] {
            Listen(negative, std::nullopt);
            Captured(binding);
        };
    };
    std::vector<prism::MenuItem> keyboard;
    for (int value = static_cast<int>(Input::KeyCode::A); value < static_cast<int>(Input::KeyCode::Count); ++value) {
        const auto key = static_cast<Input::KeyCode>(value);
        keyboard.push_back(prism::MenuItem::Item(std::string(Input::GetKeyName(key)), pick(Input::KeyBinding(key))));
    }
    std::vector<prism::MenuItem> mouse;
    for (int value = 0; value < static_cast<int>(Input::MouseButton::Count); ++value) {
        const auto button = static_cast<Input::MouseButton>(value);
        mouse.push_back(prism::MenuItem::Item(std::string(Input::GetMouseButtonName(button)), pick(Input::MouseBinding(button))));
    }
    std::vector<prism::MenuItem> items{prism::MenuItem::Sub("Keyboard", std::move(keyboard)), prism::MenuItem::Sub("Mouse", std::move(mouse))};
    for (std::size_t pad = 0U; pad < Input::kMaximumGamepadCount; ++pad) {
        std::vector<prism::MenuItem> pads;
        for (int value = 0; value < static_cast<int>(Input::GamepadButton::Count); ++value) {
            const auto button = static_cast<Input::GamepadButton>(value);
            pads.push_back(prism::MenuItem::Item(std::string(Input::GetGamepadButtonName(button)),
                                                 pick(Input::GamepadButtonBinding(pad, button))));
        }
        for (int value = 0; value < static_cast<int>(Input::GamepadAxis::Count); ++value) {
            const auto axis = static_cast<Input::GamepadAxis>(value);
            const std::string name(Input::GetGamepadAxisName(axis));
            pads.push_back(prism::MenuItem::Item(name + " +", pick(Input::GamepadAxisBinding(pad, axis, 1.0F))));
            pads.push_back(prism::MenuItem::Item(name + " -", pick(Input::GamepadAxisBinding(pad, axis, -1.0F))));
        }
        items.push_back(prism::MenuItem::Sub("Gamepad " + std::to_string(pad + 1U), std::move(pads)));
    }
    prism::showSearchMenu(std::move(items), anchor, prism::PopupPlacement::Below, "Search inputs", true);
}

prism::Ref<prism::Widget> EditorInputMapWindow::BindingSection(const std::vector<InputAction>& actions, const std::size_t index,
                                                                     const bool negative, const char* const title) {
    auto section = prism::vbox(4.0F);
    auto heading = prism::make<prism::Label>(title, prism::FontRole::Small);
    heading->setColor(prism::ThemeColor(&prism::Theme::textDim));
    section->add(heading);
    const std::vector<InputBinding>& bindings = negative ? actions[index].negativeBindings : actions[index].positiveBindings;
    for (std::size_t b = 0U; b < bindings.size(); ++b) {
        auto row = prism::hbox(6.0F);
        auto name = prism::make<prism::Label>(Input::DescribeInputBinding(bindings[b]));
        const std::vector<std::string> conflicts = FindInputConflicts(actions, index, bindings[b]);
        if (!conflicts.empty()) {
            name->setColor(prism::ThemeColor(&prism::Theme::warning));
            std::string tip = "Also bound to:";
            for (const std::string& other : conflicts) {
                tip += "\n  " + other;
            }
            name->setTooltip(tip);
        }
        row->add(name, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto change = prism::make<prism::Button>("Change", prism::Icon::None, prism::ButtonStyle::Default);
        change->onClick = [this, negative, b] { Listen(negative, b); };
        row->add(change, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto remove = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
        remove->setTooltip("Remove this binding");
        remove->onClick = [this, index, negative, b] {
            std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
            if (index < next.size()) {
                std::vector<InputBinding>& list = negative ? next[index].negativeBindings : next[index].positiveBindings;
                if (b < list.size()) {
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(b));
                }
                Commit(std::move(next));
            }
        };
        row->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        section->add(row);
    }
    if (bindings.empty()) {
        auto none = prism::make<prism::Label>("Nothing bound yet.");
        none->setColor(prism::ThemeColor(&prism::Theme::textDim));
        section->add(none);
    }
    auto buttons = prism::hbox(6.0F);
    auto listen = prism::make<prism::Button>("Add Binding", prism::Icon::Plus, prism::ButtonStyle::Default);
    listen->setTooltip("Then press the key, the gamepad button or push the stick.");
    listen->onClick = [this, negative] { Listen(negative, std::nullopt); };
    buttons->add(listen, prism::Slot{0.0F});
    auto choose = prism::make<prism::Button>("Choose...", prism::Icon::Search, prism::ButtonStyle::Default);
    choose->setTooltip("Pick an input from a list: a mouse button, a modifier key, a pad not connected.");
    prism::Button* raw = choose.get();
    choose->onClick = [this, negative, raw] { ShowChooseMenu(negative, raw->rect()); };
    buttons->add(choose, prism::Slot{0.0F});
    section->add(buttons);
    return section;
}

void EditorInputMapWindow::RebuildList() {
    m_listDirty = false;
    if (!m_list) {
        return;
    }
    const std::vector<InputAction> actions = m_editor.m_services->GetInputMap().GetActions();
    const std::string filter = m_filter ? Lower(m_filter->text()) : std::string();
    m_list->clear();
    m_shown.clear();
    for (std::size_t i = 0U; i < actions.size(); ++i) {
        if (!filter.empty() && Lower(actions[i].name).find(filter) == std::string::npos) {
            continue;
        }
        m_shown.push_back(i);
        prism::TreeNode* node = m_list->addNode(nullptr, actions[i].name,
                                                actions[i].type == InputActionType::Axis1D ? prism::Icon::Move : prism::Icon::Select,
                                                static_cast<int>(m_shown.size()));
        node->toggles = false;
        node->badge = actions[i].type == InputActionType::Axis1D ? "Axis" : "Button";
    }
    m_list->refresh();
    const std::size_t selected = m_editor.m_inputMapWindow.selected;
    for (std::size_t row = 0U; row < m_shown.size(); ++row) {
        if (m_shown[row] == selected) {
            m_list->setSelection({static_cast<int>(row) + 1});
        }
    }
}

void EditorInputMapWindow::RebuildDetail() {
    m_detailDirty = false;
    if (!m_detail) {
        return;
    }
    m_detail->clearChildren();
    Core::InputMapDocument& map = m_editor.m_services->GetInputMap();
    const std::vector<InputAction> actions = map.GetActions();
    const std::size_t index = m_editor.m_inputMapWindow.selected;
    if (m_capture) {
        m_detail->add(m_capture);
        m_capture->relayout();
    }
    if (index >= actions.size()) {
        auto hint = prism::make<prism::Label>(actions.empty() ? "Add an action, then give it the keys and buttons that trigger it."
                                                              : "Select an action.");
        hint->setColor(prism::ThemeColor(&prism::Theme::textDim));
        m_detail->add(hint);
    } else {
        const InputAction& action = actions[index];
        auto nameRow = prism::hbox(8.0F);
        nameRow->add(prism::make<prism::Label>("Name"), prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto name = prism::make<prism::TextInput>(action.name, "action name");
        name->onCommit = [this, index](const std::string& text) {
            std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
            const bool taken = std::any_of(next.begin(), next.end(), [&](const InputAction& other) { return other.name == text; });
            if (index < next.size() && !text.empty() && !taken) {
                next[index].name = text;
                Commit(std::move(next));
            } else {
                m_detailDirty = true;   // an empty or taken name is not kept
            }
        };
        nameRow->add(name, prism::Slot{1.0F});
        m_detail->add(nameRow);
        auto kindRow = prism::hbox(8.0F);
        kindRow->add(prism::make<prism::Label>("Kind"), prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto kind = prism::make<prism::ComboBox>(std::vector<std::string>{"Button", "Axis"}, action.type == InputActionType::Axis1D ? 1 : 0);
        kind->onChange = [this, index](const int value) {
            std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
            if (index < next.size()) {
                next[index].type = value == 1 ? InputActionType::Axis1D : InputActionType::Button;
                Commit(std::move(next));
            }
        };
        kindRow->add(kind, prism::Slot{0.0F});
        m_detail->add(kindRow);
        if (action.type == InputActionType::Axis1D) {
            m_detail->add(BindingSection(actions, index, false, "POSITIVE"));
            m_detail->add(BindingSection(actions, index, true, "NEGATIVE"));
        } else {
            m_detail->add(BindingSection(actions, index, false, "BINDINGS"));
        }
    }
    const std::string file = map.GetPath().filename().string();
    if (m_note) {
        m_note->setText(map.IsDirty() ? "Unsaved changes to " + file + ". Play uses them already." : "Saved in " + file + ", with the project.");
    }
    if (m_save) {
        m_save->setEnabled(map.IsDirty());
    }
    m_detail->relayout();
}

prism::Ref<prism::Widget> EditorInputMapWindow::Build() {
    auto window = prism::vbox();
    window->add(BuildToolWindowTitleBar("Input Map", prism::Icon::Settings, [this] {
        m_editor.m_inputMapWindow.visible = false;
        m_closePending = true;
    }));

    // Left: the actions.
    auto left = prism::vbox(6.0F);
    left->setPadding(prism::Margin(6.0F));
    m_filter = prism::make<prism::TextInput>("", "Filter actions");
    m_filter->onChanged = [this](const std::string&) { m_listDirty = true; };
    left->add(m_filter);
    m_list = prism::make<prism::TreeView>();
    m_list->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (!nodes.empty() && nodes.front()->id > 0 && static_cast<std::size_t>(nodes.front()->id) <= m_shown.size()) {
            m_editor.m_inputMapWindow.selected = m_shown[static_cast<std::size_t>(nodes.front()->id) - 1U];
            m_editor.m_inputMapWindow.listening = false;
            m_detailDirty = true;
        }
    };
    left->add(m_list, prism::Slot{1.0F});
    auto listButtons = prism::hbox(4.0F);
    auto add = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    add->onClick = [this] {
        std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
        next.push_back(InputAction{MakeUniqueActionName(next, "new_action"), InputActionType::Button, {}, {}});
        m_editor.m_inputMapWindow.selected = next.size() - 1U;
        Commit(std::move(next));
    };
    auto duplicate = prism::make<prism::Button>("Duplicate", prism::Icon::None, prism::ButtonStyle::Default);
    duplicate->onClick = [this] {
        std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
        const std::size_t index = m_editor.m_inputMapWindow.selected;
        if (index < next.size()) {
            InputAction copy = next[index];
            copy.name = MakeUniqueActionName(next, copy.name);
            next.insert(next.begin() + static_cast<std::ptrdiff_t>(index) + 1, copy);
            m_editor.m_inputMapWindow.selected = index + 1U;
            Commit(std::move(next));
        }
    };
    auto remove = prism::make<prism::Button>("Delete", prism::Icon::None, prism::ButtonStyle::Default);
    remove->onClick = [this] {
        std::vector<InputAction> next = m_editor.m_services->GetInputMap().GetActions();
        const std::size_t index = m_editor.m_inputMapWindow.selected;
        if (index < next.size()) {
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
            m_editor.m_inputMapWindow.selected = next.empty() ? 0U : std::min(index, next.size() - 1U);
            Commit(std::move(next));
        }
    };
    listButtons->add(add, prism::Slot{0.0F});
    listButtons->add(duplicate, prism::Slot{0.0F});
    listButtons->add(remove, prism::Slot{0.0F});
    left->add(listButtons);

    // Right: the picked action.
    m_detail = prism::vbox(10.0F);
    m_detail->setPadding(prism::Margin(12.0F));
    m_capture = prism::make<InputCapture>(*this);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_detail);

    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(Panel(left), prism::Splitter::Pane{280.0F, 200.0F, 480.0F, false});
    body->add(Panel(scroll), prism::Splitter::Pane{580.0F, 320.0F, 1e9F, true});
    EditorLayoutStore::Instance().BindSplitter("inputMap.body", *body);
    window->add(body, prism::Slot{1.0F});

    auto buttons = prism::hbox(6.0F);
    buttons->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 8.0F));
    m_note = prism::make<prism::Label>("", prism::FontRole::Small);
    m_note->setColor(prism::ThemeColor(&prism::Theme::textDim));
    buttons->add(m_note, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Primary);
    m_save->onClick = [this] {
        static_cast<void>(m_editor.SaveInputMap());
        m_detailDirty = true;
    };
    buttons->add(m_save, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto close = prism::make<prism::Button>("Close", prism::Icon::None, prism::ButtonStyle::Default);
    close->onClick = [this] {
        m_editor.m_inputMapWindow.visible = false;
        m_closePending = true;
    };
    buttons->add(close, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footer->setChild(buttons);
    window->add(footer);
    return window;
}

void EditorInputMapWindow::Frame() {
    auto& state = m_editor.m_inputMapWindow;
    if (m_window != 0 && m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        state.visible = false;
    }
    if (m_window != 0 && (m_closePending || !state.visible)) {
        // Closing the window keeps what was changed in it.
        state.listening = false;
        static_cast<void>(m_editor.SaveInputMap());
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_closePending = false;
        m_filter.reset();
        m_list.reset();
        m_detail.reset();
        m_capture.reset();
        m_note.reset();
        m_save.reset();
        return;
    }
    m_closePending = false;
    if (!state.visible) {
        return;
    }
    const bool freshlyOpened = (m_window == 0);
    if (freshlyOpened) {
        m_window = m_host.OpenWindow(Build(), "Input Map", kWidth, kHeight);
        if (m_window == 0) {
            state.visible = false;
            return;
        }
        m_listDirty = true;
        m_detailDirty = true;
    }
    if (freshlyOpened || state.focusPending) {
        state.focusPending = false;
        m_host.FocusWindow(m_window);
    }
    // While listening, a gamepad button or a stick pushed anywhere binds (keys come through the
    // capture banner, which has the keyboard).
    if (state.listening) {
        if (const std::optional<InputBinding> pad =
                CaptureInputBinding(m_editor.m_services->GetInputSystem(), m_editor.m_services->GetGamepadInputSystem(), false);
            pad && pad->source != Input::InputBindingSource::Keyboard) {
            Captured(*pad);
        }
    }
    if (m_listDirty || m_detailDirty) {
        const bool focusCapture = state.listening;
        m_host.App().runInWindow(m_window, [this, focusCapture] {
            if (m_listDirty) {
                RebuildList();
            }
            if (m_detailDirty) {
                RebuildDetail();
            }
            if (focusCapture && m_capture && prism::Application::exists()) {
                prism::Application::get().setFocus(m_capture.get());
            }
        });
    }
    m_host.RenderWindow(m_window, {});
}

} // namespace UVE::Editor
