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

using Config::SettingDescriptorUVE;
using Config::SettingTypeUVE;
using Config::SettingValueUVE;

constexpr int kWidthUVE = 920;
constexpr int kHeightUVE = 620;

/// "Editor/Viewport/Grid" as "Viewport / Grid" (the window's own root dropped).
[[nodiscard]] std::string CategoryTitleUVE(const std::string_view category) {
    const std::size_t first = category.find('/');
    const std::string_view rest = first == std::string_view::npos ? category : category.substr(first + 1U);
    std::string title;
    for (const char c : rest) {
        title += c == '/' ? std::string(" / ") : std::string(1, c);
    }
    return title.empty() ? std::string(category) : title;
}

[[nodiscard]] std::string NumberTextUVE(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

[[nodiscard]] std::optional<double> ParseNumberUVE(const std::string& text) {
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] double ClampUVE(const SettingDescriptorUVE& descriptor, double value) {
    if (descriptor.minimum) {
        value = std::max(value, *descriptor.minimum);
    }
    if (descriptor.maximum) {
        value = std::min(value, *descriptor.maximum);
    }
    return value;
}

/// A text field that commits on Enter or when it loses focus.
[[nodiscard]] prism::Ref<prism::TextInput> FieldUVE(const std::string& text, std::function<void(const std::string&)> commit) {
    auto field = prism::make<prism::TextInput>(text);
    field->onCommit = [commit](const std::string& value) { commit(value); };
    return field;
}

} // namespace

EditorSettingsWindowUVE::EditorSettingsWindowUVE(EditorUVE& editor, EditorHostUVE& host, bool& visible,
                                                 std::function<SourceUVE()> source)
    : m_editor(editor), m_host(host), m_visible(visible), m_sourceOf(std::move(source)) {}

EditorSettingsWindowUVE::~EditorSettingsWindowUVE() = default;

void EditorSettingsWindowUVE::SetUVE(const std::string& id, const SettingValueUVE& value) {
    if (m_source.set) {
        static_cast<void>(m_source.set(id, value));
    }
    m_rowsDirty = true;   // rebuilt between frames: never from inside the control's own event
}

prism::Ref<prism::Widget> EditorSettingsWindowUVE::EditorForUVE(const SettingDescriptorUVE& descriptor, const SettingValueUVE& value) {
    const std::string id = descriptor.id;
    switch (descriptor.type) {
    case SettingTypeUVE::Bool: {
        auto box = prism::make<prism::CheckBox>("", std::get<bool>(value));
        box->onChange = [this, id](const bool on) { SetUVE(id, SettingValueUVE{on}); };
        return box;
    }
    case SettingTypeUVE::Int:
    case SettingTypeUVE::LayerMask: {
        const auto current = std::get<std::int64_t>(value);
        return FieldUVE(std::to_string(current), [this, id, descriptor](const std::string& text) {
            if (const std::optional<double> parsed = ParseNumberUVE(text)) {
                double v = descriptor.type == SettingTypeUVE::LayerMask ? std::clamp(*parsed, 0.0, 4294967295.0) : ClampUVE(descriptor, *parsed);
                SetUVE(id, SettingValueUVE{static_cast<std::int64_t>(std::llround(v))});
            } else {
                m_rowsDirty = true;
            }
        });
    }
    case SettingTypeUVE::Float: {
        return FieldUVE(NumberTextUVE(std::get<double>(value)), [this, id, descriptor](const std::string& text) {
            if (const std::optional<double> parsed = ParseNumberUVE(text)) {
                SetUVE(id, SettingValueUVE{ClampUVE(descriptor, *parsed)});
            } else {
                m_rowsDirty = true;
            }
        });
    }
    case SettingTypeUVE::String:
    case SettingTypeUVE::FilePath:
    case SettingTypeUVE::AssetReference: {
        const std::size_t limit = descriptor.maxLength;
        return FieldUVE(std::get<std::string>(value), [this, id, limit](const std::string& text) {
            SetUVE(id, SettingValueUVE{limit != 0U && text.size() > limit ? text.substr(0U, limit) : text});
        });
    }
    case SettingTypeUVE::StringList: {
        const std::size_t count = std::get<Config::SettingStringListUVE>(value).size();
        auto label = prism::make<prism::Label>(std::to_string(count) + (count == 1U ? " entry" : " entries"));
        label->setColor(prism::ThemeColor(&prism::Theme::textDim));
        return label;
    }
    case SettingTypeUVE::KeyBinding: {
        const std::optional<EditorShortcutUVE> parsed = ParseEditorShortcutUVE(std::get<std::string>(value));
        auto capture = prism::make<EditorKeyCaptureUVE>(parsed.value_or(EditorShortcutUVE{}));
        capture->onCaptured = [this, id](const EditorShortcutUVE& shortcut) {
            SetUVE(id, SettingValueUVE{FormatEditorShortcutUVE(shortcut)});
        };
        auto row = prism::hbox(4.0F);
        row->add(capture, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        auto clear = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
        clear->setTooltip("Remove this shortcut");
        clear->onClick = [this, id] { SetUVE(id, SettingValueUVE{std::string{}}); };
        row->add(clear, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        return row;
    }
    case SettingTypeUVE::Enum: {
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
        const std::vector<Config::SettingEnumEntryUVE> entries = descriptor.enumEntries;
        combo->onChange = [this, id, entries](const int index) {
            if (index >= 0 && static_cast<std::size_t>(index) < entries.size()) {
                SetUVE(id, SettingValueUVE{entries[static_cast<std::size_t>(index)].value});
            }
        };
        return combo;
    }
    case SettingTypeUVE::Color: {
        const auto& c = std::get<Config::SettingColorUVE>(value);
        const auto byte = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
        auto field = prism::make<prism::ColorField>(prism::Color(byte(c.r), byte(c.g), byte(c.b), byte(c.a)));
        const bool alpha = descriptor.colorHasAlpha;
        field->onChange = [this, id, alpha](const prism::Color colour) {
            SetUVE(id, SettingValueUVE{Config::SettingColorUVE{colour.r / 255.0F, colour.g / 255.0F, colour.b / 255.0F,
                                                                alpha ? colour.a / 255.0F : 1.0F}});
        };
        return field;
    }
    case SettingTypeUVE::Vector2:
    case SettingTypeUVE::Vector3:
    case SettingTypeUVE::Vector4: {
        std::vector<double> parts;
        if (const auto* v2 = std::get_if<Config::SettingVector2UVE>(&value)) {
            parts = {v2->x, v2->y};
        } else if (const auto* v3 = std::get_if<Config::SettingVector3UVE>(&value)) {
            parts = {v3->x, v3->y, v3->z};
        } else if (const auto* v4 = std::get_if<Config::SettingVector4UVE>(&value)) {
            parts = {v4->x, v4->y, v4->z, v4->w};
        }
        auto row = prism::hbox(4.0F);
        for (std::size_t i = 0U; i < parts.size(); ++i) {
            row->add(FieldUVE(NumberTextUVE(parts[i]), [this, id, descriptor, parts, i](const std::string& text) {
                std::vector<double> next = parts;
                if (const std::optional<double> parsed = ParseNumberUVE(text)) {
                    next[i] = ClampUVE(descriptor, *parsed);
                }
                if (next.size() == 2U) {
                    SetUVE(id, SettingValueUVE{Config::SettingVector2UVE{next[0], next[1]}});
                } else if (next.size() == 3U) {
                    SetUVE(id, SettingValueUVE{Config::SettingVector3UVE{next[0], next[1], next[2]}});
                } else {
                    SetUVE(id, SettingValueUVE{Config::SettingVector4UVE{next[0], next[1], next[2], next[3]}});
                }
            }), prism::Slot{1.0F});
        }
        return row;
    }
    }
    return prism::make<prism::Label>("");
}

void EditorSettingsWindowUVE::RebuildTreeUVE() {
    if (!m_tree || m_source.registry == nullptr) {
        return;
    }
    std::vector<const SettingDescriptorUVE*> visible;
    for (const SettingDescriptorUVE* descriptor : m_source.registry->GetAllUVE()) {
        if (!descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE) && !descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) {
            visible.push_back(descriptor);
        }
    }
    m_tree->clear();
    m_categoryPaths.clear();
    m_tree->addNode(nullptr, "All Settings", prism::Icon::Layers, 0)->toggles = false;
    std::vector<prism::TreeNode*> parents;
    for (const SettingCategoryObjectUVE& object : BuildSettingCategoryTreeUVE(visible)) {
        m_categoryPaths.push_back(object.path);
        parents.resize(static_cast<std::size_t>(object.depth) + 1U, nullptr);
        prism::TreeNode* parent = object.depth > 0 ? parents[static_cast<std::size_t>(object.depth) - 1U] : nullptr;
        prism::TreeNode* node = m_tree->addNode(parent, object.name, prism::Icon::Folder, static_cast<int>(m_categoryPaths.size()));
        node->toggles = false;   // a category, not an object: no eye
        parents[static_cast<std::size_t>(object.depth)] = node;
    }
    m_tree->refresh();
}

void EditorSettingsWindowUVE::RebuildRowsUVE() {
    m_rowsDirty = false;
    if (!m_rows || m_source.registry == nullptr) {
        return;
    }
    m_rows->clear();
    const std::string query = m_search ? m_search->text() : std::string();
    const bool modifiedOnly = m_modifiedOnly && m_modifiedOnly->checked();
    const bool advanced = m_advanced && m_advanced->checked();
    std::vector<const SettingDescriptorUVE*> shown;
    std::size_t restartChanges = 0U;
    for (const SettingDescriptorUVE* descriptor : m_source.registry->GetAllUVE()) {
        if (descriptor->HasFlagUVE(Config::kSettingFlagHiddenUVE) || descriptor->HasFlagUVE(Config::kSettingFlagDeprecatedUVE)) {
            continue;
        }
        const std::optional<SettingValueUVE> value = m_source.get(descriptor->id);
        if (!value) {
            continue;
        }
        if (descriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE)) {
            if (const auto base = m_restartBaseline.find(descriptor->id); base != m_restartBaseline.end() && base->second != *value) {
                ++restartChanges;
            }
        }
        if (descriptor->HasFlagUVE(Config::kSettingFlagAdvancedUVE) && !advanced) {
            continue;
        }
        if (!MatchesSettingSearchUVE(*descriptor, query)) {
            continue;
        }
        if (query.empty() && !m_category.empty() && !IsInSettingCategoryUVE(descriptor->category, m_category)) {
            continue;
        }
        if (modifiedOnly && *value == descriptor->defaultValue) {
            continue;
        }
        shown.push_back(descriptor);
    }
    std::stable_sort(shown.begin(), shown.end(), [](const SettingDescriptorUVE* a, const SettingDescriptorUVE* b) { return a->category < b->category; });
    std::string current;
    int category = -1;
    for (const SettingDescriptorUVE* descriptor : shown) {
        if (category < 0 || descriptor->category != current) {
            current = descriptor->category;
            category = m_rows->addCategory(CategoryTitleUVE(current));
        }
        const SettingValueUVE value = *m_source.get(descriptor->id);
        const bool modified = value != descriptor->defaultValue;
        auto editor = EditorForUVE(*descriptor, value);
        auto cell = prism::hbox(4.0F);
        cell->add(editor, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        if (modified) {
            auto reset = prism::make<prism::Button>("", prism::Icon::Undo, prism::ButtonStyle::Flat);
            reset->setTooltip("Reset to " + FormatSettingValueUVE(*descriptor, descriptor->defaultValue));
            const std::string id = descriptor->id;
            const SettingValueUVE initial = descriptor->defaultValue;
            reset->onClick = [this, id, initial] { SetUVE(id, initial); };
            cell->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        std::string tip = descriptor->tooltip;
        if (descriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE)) {
            tip += (tip.empty() ? "" : "\n") + std::string("Takes effect after the editor restarts.");
        }
        tip += (tip.empty() ? "" : "\n") + std::string("Default: ") + FormatSettingValueUVE(*descriptor, descriptor->defaultValue);
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

prism::Ref<prism::Widget> EditorSettingsWindowUVE::BuildUVE() {
    auto window = prism::vbox();

    auto title = prism::hbox(6.0F);
    title->setPadding(prism::Margin(10.0F, 0.0F, 0.0F, 0.0F));
    auto titleText = prism::make<prism::Label>(m_source.title);
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    title->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    title->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*title, [this] { CloseUVE(); });
    auto titleBox = prism::make<prism::SizeBox>();
    titleBox->setHeightOverride(kTitleRowUVE + 4.0F);
    titleBox->setContent(title);
    auto titleBar = prism::make<prism::Border>();
    titleBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    titleBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    titleBar->setChild(titleBox);
    window->add(titleBar);

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
    body->add(PanelUVE(m_tree), prism::Splitter::Pane{220.0F, 160.0F, 400.0F, false});
    body->add(PanelUVE(scroll), prism::Splitter::Pane{680.0F, 320.0F, 1e9F, true});
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
    close->onClick = [this] { CloseUVE(); };
    buttons->add(close, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footer->setChild(buttons);
    window->add(footer);
    return window;
}

void EditorSettingsWindowUVE::CloseUVE() {
    m_visible = false;
    m_closePending = true;
}

void EditorSettingsWindowUVE::FrameUVE() {
    if (m_window != 0 && m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        CloseUVE();
    }
    if (m_window != 0 && (m_closePending || !m_visible)) {
        // Closed here, between frames: never from inside the window's own input. Unsaved project
        // settings are kept, as the old window did when it closed.
        if (m_source.save && m_source.dirty && m_source.dirty()) {
            static_cast<void>(m_source.save());
        }
        m_host.CloseWindowUVE(m_window);
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
    if (m_window == 0) {
        m_source = m_sourceOf();
        if (m_source.registry == nullptr) {
            m_visible = false;
            return;
        }
        // What the restart-only settings were when the window opened: a change to one is announced.
        m_restartBaseline.clear();
        for (const SettingDescriptorUVE* descriptor : m_source.registry->GetAllUVE()) {
            if (descriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE)) {
                if (const std::optional<SettingValueUVE> value = m_source.get(descriptor->id)) {
                    m_restartBaseline.emplace(descriptor->id, *value);
                }
            }
        }
        m_window = m_host.OpenWindowUVE(BuildUVE(), m_source.title, kWidthUVE, kHeightUVE);
        if (m_window == 0) {
            m_visible = false;
            return;
        }
        m_host.AppUVE().runInWindow(m_window, [this] {
            RebuildTreeUVE();
            RebuildRowsUVE();
            if (m_search && prism::Application::exists()) {
                prism::Application::get().setFocus(m_search.get());
            }
        });
        m_host.FocusWindowUVE(m_window);
    }
    if (m_rowsDirty) {
        m_host.AppUVE().runInWindow(m_window, [this] { RebuildRowsUVE(); });
    }
    m_host.RenderWindowUVE(m_window, {});
}

} // namespace UVE::Editor
