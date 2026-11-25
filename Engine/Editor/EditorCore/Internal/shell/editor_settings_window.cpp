// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_settings_window.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_key_capture.h"
#include "editor_settings.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace UVE::Editor {
namespace {

using Config::SettingDescriptor;
using Config::SettingType;
using Config::SettingValue;

constexpr int kWidth = 920;
constexpr int kHeight = 620;

/// "Editor/Viewport/Grid" as "Viewport / Grid" (the window's own root dropped).
[[nodiscard]] std::string CategoryTitle(const std::string_view category) {
    const std::size_t first = category.find('/');
    const std::string_view rest = first == std::string_view::npos ? category : category.substr(first + 1U);
    std::string title;
    for (const char c : rest) {
        title += c == '/' ? std::string(" / ") : std::string(1, c);
    }
    return title.empty() ? std::string(category) : title;
}

[[nodiscard]] std::string NumberText(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

[[nodiscard]] std::optional<double> ParseNumber(const std::string& text) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] double Clamp(const SettingDescriptor& descriptor, double value) {
    if (descriptor.minimum) {
        value = std::max(value, *descriptor.minimum);
    }
    if (descriptor.maximum) {
        value = std::min(value, *descriptor.maximum);
    }
    return value;
}

/// A text field that commits on Enter or when it loses focus.
[[nodiscard]] prism::Ref<prism::TextInput> Field(const std::string& text, std::function<void(const std::string&)> commit) {
    auto field = prism::make<prism::TextInput>(text);
    field->onCommit = [commit](const std::string& value) { commit(value); };
    return field;
}

} // namespace

EditorSettingsWindow::EditorSettingsWindow(Editor& editor, EditorHost& host, bool& visible,
                                                 std::function<Source()> source, bool* focusPending)
    : m_editor(editor), m_host(host), m_visible(visible), m_focusPending(focusPending), m_sourceOf(std::move(source)) {}

EditorSettingsWindow::~EditorSettingsWindow() = default;

void EditorSettingsWindow::Set(const std::string& id, const SettingValue& value) {
    if (m_source.set) {
        static_cast<void>(m_source.set(id, value));
    }
    m_rowsDirty = true;   // rebuilt between frames: never from inside the control's own event
}

prism::Ref<prism::Widget> EditorSettingsWindow::EditorFor(const SettingDescriptor& descriptor, const SettingValue& value) {
    const std::string id = descriptor.id;
    switch (descriptor.type) {
    case SettingType::Bool: {
        auto box = prism::make<prism::CheckBox>("", std::get<bool>(value));
        box->onChange = [this, id](const bool on) { Set(id, SettingValue{on}); };
        return box;
    }
    case SettingType::Int:
    case SettingType::LayerMask: {
        const auto current = std::get<std::int64_t>(value);
        return Field(std::to_string(current), [this, id, descriptor](const std::string& text) {
            if (const std::optional<double> parsed = ParseNumber(text)) {
                double v = descriptor.type == SettingType::LayerMask ? std::clamp(*parsed, 0.0, 4294967295.0) : Clamp(descriptor, *parsed);
                Set(id, SettingValue{static_cast<std::int64_t>(std::llround(v))});
            } else {
                m_rowsDirty = true;
            }
        });
    }
    case SettingType::Float: {
        return Field(NumberText(std::get<double>(value)), [this, id, descriptor](const std::string& text) {
            if (const std::optional<double> parsed = ParseNumber(text)) {
                Set(id, SettingValue{Clamp(descriptor, *parsed)});
            } else {
                m_rowsDirty = true;
            }
        });
    }
    case SettingType::String:
    case SettingType::FilePath:
    case SettingType::AssetReference: {
        const std::size_t limit = descriptor.maxLength;
        return Field(std::get<std::string>(value), [this, id, limit](const std::string& text) {
            Set(id, SettingValue{limit != 0U && text.size() > limit ? text.substr(0U, limit) : text});
        });
    }
    case SettingType::StringList: {
        const std::size_t count = std::get<Config::SettingStringList>(value).size();
        auto label = prism::make<prism::Label>(std::to_string(count) + (count == 1U ? " entry" : " entries"));
        label->setColor(prism::ThemeColor(&prism::Theme::textDim));
        return label;
    }
    case SettingType::KeyBinding: {
        const std::optional<EditorShortcut> parsed = ParseEditorShortcut(std::get<std::string>(value));
        auto capture = prism::make<EditorKeyCapture>(parsed.value_or(EditorShortcut{}));
        capture->onCaptured = [this, id](const EditorShortcut& shortcut) {
            Set(id, SettingValue{FormatEditorShortcut(shortcut)});
        };
        auto row = prism::hbox(4.0F);
        row->add(capture, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        auto clear = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
        clear->setTooltip("Remove this shortcut");
        clear->onClick = [this, id] { Set(id, SettingValue{std::string{}}); };
        row->add(clear, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        return row;
    }
    case SettingType::Enum: {
        std::vector<std::string> labels;
        int selected = 0;
        const std::int64_t current = std::get<std::int64_t>(value);
        for (std::size_t i = 0U; i < descriptor.enumEntries.size(); ++i) {
            labels.push_back(descriptor.enumEntries[i].label);
            if (descriptor.enumEntries[i].value == current) {
                selected = static_cast<int>(i);
            }
        }
        auto combo = prism::make<prism::ComboBox>(std::move(labels), selected);
        const std::vector<Config::SettingEnumEntry> entries = descriptor.enumEntries;
        combo->onChange = [this, id, entries](const int index) {
            if (index >= 0 && static_cast<std::size_t>(index) < entries.size()) {
                Set(id, SettingValue{entries[static_cast<std::size_t>(index)].value});
            }
        };
        return combo;
    }
    case SettingType::Color: {
        const auto& c = std::get<Config::SettingColor>(value);
        const auto byte = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
        auto field = prism::make<prism::ColorField>(prism::Color(byte(c.r), byte(c.g), byte(c.b), byte(c.a)));
        const bool alpha = descriptor.colorHasAlpha;
        field->onChange = [this, id, alpha](const prism::Color colour) {
            Set(id, SettingValue{Config::SettingColor{colour.r / 255.0F, colour.g / 255.0F, colour.b / 255.0F,
                                                                alpha ? colour.a / 255.0F : 1.0F}});
        };
        return field;
    }
    case SettingType::Vector2:
    case SettingType::Vector3:
    case SettingType::Vector4: {
        std::vector<double> parts;
        if (const auto* v2 = std::get_if<Config::SettingVector2>(&value)) {
            parts = {v2->x, v2->y};
        } else if (const auto* v3 = std::get_if<Config::SettingVector3>(&value)) {
            parts = {v3->x, v3->y, v3->z};
        } else if (const auto* v4 = std::get_if<Config::SettingVector4>(&value)) {
            parts = {v4->x, v4->y, v4->z, v4->w};
        }
        auto row = prism::hbox(4.0F);
        for (std::size_t i = 0U; i < parts.size(); ++i) {
            row->add(Field(NumberText(parts[i]), [this, id, descriptor, parts, i](const std::string& text) {
                std::vector<double> next = parts;
                if (const std::optional<double> parsed = ParseNumber(text)) {
                    next[i] = Clamp(descriptor, *parsed);
                }
                if (next.size() == 2U) {
                    Set(id, SettingValue{Config::SettingVector2{next[0], next[1]}});
                } else if (next.size() == 3U) {
                    Set(id, SettingValue{Config::SettingVector3{next[0], next[1], next[2]}});
                } else {
                    Set(id, SettingValue{Config::SettingVector4{next[0], next[1], next[2], next[3]}});
                }
            }), prism::Slot{1.0F});
        }
        return row;
    }
    }
    return prism::make<prism::Label>("");
}

void EditorSettingsWindow::RebuildTree() {
    if (!m_tree || m_source.registry == nullptr) {
        return;
    }
    std::vector<const SettingDescriptor*> visible;
    for (const SettingDescriptor* descriptor : m_source.registry->GetAll()) {
        if (!descriptor->HasFlag(Config::kSettingFlagHidden) && !descriptor->HasFlag(Config::kSettingFlagDeprecated)) {
            visible.push_back(descriptor);
        }
    }
    m_tree->clear();
    m_categoryPaths.clear();
    m_tree->addNode(nullptr, "All Settings", prism::Icon::Layers, 0)->toggles = false;
    std::vector<prism::TreeNode*> parents;
    for (const SettingCategoryObject& object : BuildSettingCategoryTree(visible)) {
        m_categoryPaths.push_back(object.path);
        parents.resize(static_cast<std::size_t>(object.depth) + 1U, nullptr);
        prism::TreeNode* parent = object.depth > 0 ? parents[static_cast<std::size_t>(object.depth) - 1U] : nullptr;
        prism::TreeNode* node = m_tree->addNode(parent, object.name, prism::Icon::Folder, static_cast<int>(m_categoryPaths.size()));
        node->toggles = false;   // a category, not an object: no eye
        parents[static_cast<std::size_t>(object.depth)] = node;
    }
    m_tree->refresh();
}

void EditorSettingsWindow::RebuildRows() {
    m_rowsDirty = false;
    if (!m_rows || m_source.registry == nullptr) {
        return;
    }
    m_rows->clear();
    const std::string query = m_search ? m_search->text() : std::string();
    const bool modifiedOnly = m_modifiedOnly && m_modifiedOnly->checked();
    const bool advanced = m_advanced && m_advanced->checked();
    std::vector<const SettingDescriptor*> shown;
    std::size_t restartChanges = 0U;
    for (const SettingDescriptor* descriptor : m_source.registry->GetAll()) {
        if (descriptor->HasFlag(Config::kSettingFlagHidden) || descriptor->HasFlag(Config::kSettingFlagDeprecated)) {
            continue;
        }
        const std::optional<SettingValue> value = m_source.get(descriptor->id);
        if (!value) {
            continue;
        }
        if (descriptor->HasFlag(Config::kSettingFlagRestartRequired)) {
            if (const auto base = m_restartBaseline.find(descriptor->id); base != m_restartBaseline.end() && base->second != *value) {
                ++restartChanges;
            }
        }
        if (descriptor->HasFlag(Config::kSettingFlagAdvanced) && !advanced) {
            continue;
        }
        if (!MatchesSettingSearch(*descriptor, query)) {
            continue;
        }
        if (query.empty() && !m_category.empty() && !IsInSettingCategory(descriptor->category, m_category)) {
            continue;
        }
        if (modifiedOnly && *value == descriptor->defaultValue) {
            continue;
        }
        shown.push_back(descriptor);
    }
    std::stable_sort(shown.begin(), shown.end(), [](const SettingDescriptor* a, const SettingDescriptor* b) { return a->category < b->category; });
    std::string current;
    int category = -1;
    for (const SettingDescriptor* descriptor : shown) {
        if (category < 0 || descriptor->category != current) {
            current = descriptor->category;
            category = m_rows->addCategory(CategoryTitle(current));
        }
        const SettingValue value = *m_source.get(descriptor->id);
        const bool modified = value != descriptor->defaultValue;
        auto editor = EditorFor(*descriptor, value);
        auto cell = prism::hbox(4.0F);
        cell->add(editor, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        if (modified) {
            auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
            reset->setTooltip("Reset to " + FormatSettingValue(*descriptor, descriptor->defaultValue));
            const std::string id = descriptor->id;
            const SettingValue initial = descriptor->defaultValue;
            reset->onClick = [this, id, initial] { Set(id, initial); };
            cell->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        std::string tip = descriptor->tooltip;
        if (descriptor->HasFlag(Config::kSettingFlagRestartRequired)) {
            tip += (tip.empty() ? "" : "\n") + std::string("Takes effect after the editor restarts.");
        }
        tip += (tip.empty() ? "" : "\n") + std::string("Default: ") + FormatSettingValue(*descriptor, descriptor->defaultValue);
        const std::string label = (modified ? "\xE2\x80\xA2 " : "") + (descriptor->displayName.empty() ? descriptor->id : descriptor->displayName);
        m_rows->addRow(category, label, cell, tip);
    }
    if (shown.empty()) {
        const int none = m_rows->addCategory("Nothing matches");
        m_rows->addRow(none, "", prism::make<prism::Label>("Try another search, or clear the filters."));
    }
    if (m_restart) {
        m_restart->setText(restartChanges == 0U ? std::string()
                                                : "Restart the editor to apply " + std::to_string(restartChanges) + " changed setting" +
                                                      (restartChanges == 1U ? "." : "s."));
    }
    if (m_note) {
        m_note->setText(m_source.note ? m_source.note() : std::string());
    }
    if (m_save) {
        m_save->setEnabled(m_source.dirty && m_source.dirty());
    }
}

prism::Ref<prism::Widget> EditorSettingsWindow::Build() {
    auto window = prism::vbox();
    window->add(BuildToolWindowTitleBar(m_source.title, prism::Icon::Settings, [this] { Close(); }));

    auto filters = prism::hbox(10.0F);
    filters->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    m_search = prism::make<prism::TextInput>("", "Search settings");
    m_search->onChanged = [this](const std::string&) { m_rowsDirty = true; };
    filters->add(m_search, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    m_modifiedOnly = prism::make<prism::CheckBox>("Modified only", false);
    m_modifiedOnly->onChange = [this](bool) { m_rowsDirty = true; };
    filters->add(m_modifiedOnly, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_advanced = prism::make<prism::CheckBox>("Advanced", false);
    m_advanced->onChange = [this](bool) { m_rowsDirty = true; };
    filters->add(m_advanced, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    window->add(filters);

    m_restart = prism::make<prism::Label>("", prism::FontRole::Small);
    m_restart->setColor(prism::ThemeColor(&prism::Theme::warning));
    auto restartRow = prism::hbox();
    restartRow->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 2.0F));
    restartRow->add(m_restart, prism::Slot{1.0F});
    window->add(restartRow);

    m_tree = prism::make<prism::TreeView>();
    m_tree->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        m_category = id > 0 && static_cast<std::size_t>(id) <= m_categoryPaths.size() ? m_categoryPaths[static_cast<std::size_t>(id) - 1U]
                                                                                       : std::string();
        m_rowsDirty = true;
    };
    m_rows = prism::make<prism::PropertyGrid>();
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_rows);
    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(Panel(m_tree), prism::Splitter::Pane{220.0F, 160.0F, 400.0F, false});
    body->add(Panel(scroll), prism::Splitter::Pane{680.0F, 320.0F, 1e9F, true});
    window->add(body, prism::Slot{1.0F});

    auto buttons = prism::hbox(6.0F);
    buttons->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 8.0F));
    m_note = prism::make<prism::Label>("", prism::FontRole::Small);
    m_note->setColor(prism::ThemeColor(&prism::Theme::textDim));
    buttons->add(m_note, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    if (m_source.save) {
        m_save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Primary);
        m_save->onClick = [this] {
            static_cast<void>(m_source.save());
            m_rowsDirty = true;
        };
        buttons->add(m_save, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    auto close = prism::make<prism::Button>("Close", prism::Icon::None, prism::ButtonStyle::Default);
    close->onClick = [this] { Close(); };
    buttons->add(close, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footer->setChild(buttons);
    window->add(footer);
    return window;
}

void EditorSettingsWindow::Close() {
    m_visible = false;
    m_closePending = true;
}

void EditorSettingsWindow::Frame() {
    if (m_window != 0 && m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        Close();
    }
    if (m_window != 0 && (m_closePending || !m_visible)) {
        // Closed here, between frames: never from inside the window's own input. Unsaved project
        // settings are kept, as the old window did when it closed.
        if (m_source.save && m_source.dirty && m_source.dirty()) {
            static_cast<void>(m_source.save());
        }
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_closePending = false;
        m_search.reset();
        m_modifiedOnly.reset();
        m_advanced.reset();
        m_tree.reset();
        m_rows.reset();
        m_restart.reset();
        m_note.reset();
        m_save.reset();
        return;
    }
    m_closePending = false;
    if (!m_visible) {
        return;
    }
    const bool freshlyOpened = (m_window == 0);
    if (freshlyOpened) {
        m_source = m_sourceOf();
        if (m_source.registry == nullptr) {
            m_visible = false;
            return;
        }
        // What the restart-only settings were when the window opened: a change to one is announced.
        m_restartBaseline.clear();
        for (const SettingDescriptor* descriptor : m_source.registry->GetAll()) {
            if (descriptor->HasFlag(Config::kSettingFlagRestartRequired)) {
                if (const std::optional<SettingValue> value = m_source.get(descriptor->id)) {
                    m_restartBaseline.emplace(descriptor->id, *value);
                }
            }
        }
        m_window = m_host.OpenWindow(Build(), m_source.title, kWidth, kHeight);
        if (m_window == 0) {
            m_visible = false;
            return;
        }
        m_host.App().runInWindow(m_window, [this] {
            RebuildTree();
            RebuildRows();
            if (m_search && prism::Application::exists() &&
                !prism::Application::get().platform().singleSurface()) {
                prism::Application::get().setFocus(m_search.get());
            }
        });
    }
    const bool focusRequested = m_focusPending != nullptr && *m_focusPending;
    if (freshlyOpened || focusRequested) {
        if (m_focusPending != nullptr) {
            *m_focusPending = false;
        }
        m_host.FocusWindow(m_window);
    }
    if (m_rowsDirty) {
        m_host.App().runInWindow(m_window, [this] { RebuildRows(); });
    }
    m_host.RenderWindow(m_window, {});
}

} // namespace UVE::Editor
