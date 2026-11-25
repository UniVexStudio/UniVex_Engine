// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_entity_class_window.h"

#include "editor.h"
#include "editor_content_catalogue.h"
#include "editor_chrome.h"
#include "editor_host.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstddef>

namespace UVE::Editor {
namespace {

constexpr int kWidth = 780;
constexpr int kHeight = 480;
constexpr float kRowHeight = 52.0F;

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Words of `text` laid in lines no wider than `width` in `font`.
[[nodiscard]] std::vector<std::string> Wrap(const prism::Font& font, std::string_view text, const float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t start = 0U;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string word(text.substr(start, end - start));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && font.width(candidate) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        start = end + 1U;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

/// The classes as rows: the object's icon, its name, the summary. Click picks, double-click
/// creates; Up / Down move the pick, Enter creates, Escape cancels.
class ClassList final : public prism::Widget {
public:
    explicit ClassList(EditorEntityClassWindow& window) : m_window(window) {}
    prism::Vec2 measure(const prism::Vec2 avail) override {
        return {avail.x, kRowHeight * static_cast<float>(m_window.Shown().size())};
    }
    bool focusable() const override { return true; }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const std::vector<std::string>& shown = m_window.Shown();
        const int picked = m_window.PickedIndex();
        for (std::size_t i = 0U; i < shown.size(); ++i) {
            const ContentCatalogueItem* const item = FindContentCatalogueItem(shown[i]);
            if (item == nullptr) {
                continue;
            }
            const prism::Rect row{m_rect.x, m_rect.y + kRowHeight * static_cast<float>(i), m_rect.w, kRowHeight};
            if (static_cast<int>(i) == picked) {
                c.fillRect(row, m_focused ? t.rowSelected : t.rowSelectedInactive);
                c.fillRect(prism::Rect{row.x, row.y, 2.0F, row.h}, t.highlight);
            } else if (static_cast<int>(i) == m_hover) {
                c.fillRect(row, t.rowHover);
            }
            if (const EditorIcons::Image icon = m_window.Icons().Object(GetContentCatalogueIconKind(*item), 32);
                icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{row.x + 10.0F, row.y + 10.0F, 32.0F, 32.0F});
            }
            const float x = row.x + 54.0F;
            c.textIn(*t.semibold, prism::Rect{x, row.y + 7.0F, row.w - x - 8.0F, 20.0F}, item->label, t.textBright);
            std::string_view summary;
            for (const ContentCatalogueEntityClass& entry : GetContentCatalogueEntityClasses()) {
                if (entry.itemId == item->id) {
                    summary = entry.summary;
                }
            }
            c.textIn(*t.small, prism::Rect{x, row.y + 27.0F, row.w - x - 8.0F, 18.0F}, summary, t.textDim);
        }
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        const int hover = RowAt(e.pos);
        if (hover != m_hover) {
            m_hover = hover;
            invalidate();
        }
        return true;
    }
    void onMouseLeave() override {
        m_hover = -1;
        invalidate();
    }
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button != prism::MouseButton::Left) {
            return false;
        }
        if (prism::Application::exists()) {
            prism::Application::get().setFocus(this);
        }
        const int row = RowAt(e.pos);
        if (row >= 0) {
            m_window.Pick(row);
            if (e.clicks >= 2) {
                m_window.Create();
            }
        }
        return true;
    }
    bool onKeyDown(const prism::KeyEvent& e) override {
        const int count = static_cast<int>(m_window.Shown().size());
        const int picked = m_window.PickedIndex();
        if (e.key == prism::Key::Down && count > 0) {
            m_window.Pick(std::min(count - 1, picked + 1));
            return true;
        }
        if (e.key == prism::Key::Up && count > 0) {
            m_window.Pick(std::max(0, picked - 1));
            return true;
        }
        if (e.key == prism::Key::Enter) {
            m_window.Create();
            return true;
        }
        if (e.key == prism::Key::Escape) {
            m_window.Cancel();
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] int RowAt(const prism::Vec2 p) const {
        if (!m_rect.contains(p)) {
            return -1;
        }
        const int row = static_cast<int>((p.y - m_rect.y) / kRowHeight);
        return row < static_cast<int>(m_window.Shown().size()) ? row : -1;
    }
    EditorEntityClassWindow& m_window;
    int m_hover = -1;
};

/// The picked class: its name, summary, what it is for, the file it makes and what is inside.
class ClassDetail final : public prism::Widget {
public:
    explicit ClassDetail(EditorEntityClassWindow& window) : m_window(window) {}
    prism::Vec2 measure(const prism::Vec2 avail) override {
        const float w = std::max(180.0F, avail.x);
        const float textW = std::max(120.0F, w - 32.0F);
        float h = 240.0F;
        const int picked = m_window.PickedIndex();
        const std::vector<std::string>& shown = m_window.Shown();
        if (picked >= 0 && picked < static_cast<int>(shown.size())) {
            if (const ContentCatalogueItem* const item =
                    FindContentCatalogueItem(shown[static_cast<std::size_t>(picked)])) {
                const prism::Theme& t = prism::theme();
                float y = 14.0F + 64.0F;
                const auto measureBlock = [&](const std::string_view text) {
                    y += 18.0F;
                    if (t.ui) {
                        y += 18.0F * static_cast<float>(std::max<std::size_t>(1U, Wrap(*t.ui, text, textW).size()));
                    } else {
                        y += 36.0F;
                    }
                    y += 10.0F;
                };
                for (const ContentCatalogueEntityClass& candidate : GetContentCatalogueEntityClasses()) {
                    if (candidate.itemId == item->id) {
                        measureBlock(candidate.detail);
                        break;
                    }
                }
                measureBlock(m_window.MakesText(item->id));
                measureBlock(item->objects.size() <= 1U
                                 ? std::string_view("Only the body itself: what it wears is yours to add once it is open.")
                                 : std::string_view("The body and the objects it starts with."));
                h = y + 16.0F;
            }
        }
        return m_desired = constrain({w, std::max(avail.y, h)});
    }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const int picked = m_window.PickedIndex();
        const std::vector<std::string>& shown = m_window.Shown();
        if (picked < 0 || picked >= static_cast<int>(shown.size())) {
            c.textIn(*t.ui, prism::Rect{m_rect.x + 16.0F, m_rect.y + 16.0F, m_rect.w - 32.0F, 20.0F},
                     "Nothing matches the search.", t.textDim);
            return;
        }
        const ContentCatalogueItem* const item = FindContentCatalogueItem(shown[static_cast<std::size_t>(picked)]);
        if (item == nullptr) {
            return;
        }
        const ContentCatalogueEntityClass* entry = nullptr;
        for (const ContentCatalogueEntityClass& candidate : GetContentCatalogueEntityClasses()) {
            if (candidate.itemId == item->id) {
                entry = &candidate;
            }
        }
        const float left = m_rect.x + 16.0F;
        const float width = m_rect.w - 32.0F;
        float y = m_rect.y + 14.0F;
        if (const EditorIcons::Image icon = m_window.Icons().Object(GetContentCatalogueIconKind(*item), 48);
            icon) {
            c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{left, y, 48.0F, 48.0F});
        }
        c.textIn(*t.semibold, prism::Rect{left + 60.0F, y + 4.0F, width - 60.0F, 22.0F}, item->label, t.textBright);
        if (entry != nullptr) {
            c.textIn(*t.ui, prism::Rect{left + 60.0F, y + 26.0F, width - 60.0F, 20.0F}, entry->summary, t.text);
        }
        y += 64.0F;
        const auto heading = [&](const char* text) {
            c.textIn(*t.small, prism::Rect{left, y, width, 16.0F}, text, t.textDim);
            y += 18.0F;
        };
        const auto paragraph = [&](std::string_view text) {
            for (const std::string& line : Wrap(*t.ui, text, width)) {
                c.textIn(*t.ui, prism::Rect{left, y, width, 18.0F}, line, t.text, prism::HAlign::Left, false);
                y += 18.0F;
            }
            y += 10.0F;
        };
        if (entry != nullptr) {
            heading("WHAT IT IS FOR");
            paragraph(entry->detail);
        }
        heading("MAKES");
        paragraph(m_window.MakesText(item->id));
        heading("INSIDE");
        paragraph(item->objects.size() <= 1U ? std::string_view("Only the body itself: what it wears is yours to add once it is open.")
                                              : std::string_view("The body and the objects it starts with."));
    }

private:
    EditorEntityClassWindow& m_window;
};

} // namespace

EditorEntityClassWindow::EditorEntityClassWindow(Editor& editor, EditorHost& host)
    : m_editor(editor), m_host(host) {}

EditorEntityClassWindow::~EditorEntityClassWindow() = default;

std::string EditorEntityClassWindow::MakesText(const std::string_view itemId) const {
    const ContentCatalogueItem* const item = FindContentCatalogueItem(itemId);
    if (item == nullptr || !m_editor.m_entityClassPicker.has_value()) {
        return {};
    }
    const std::filesystem::path& directory = m_editor.m_entityClassPicker->directory;
    const std::string stem(item->fileStem.empty() ? item->label : item->fileStem);
    const std::filesystem::path path = Editor::MakeUniqueContentPath(directory, stem, ".uventity");
    const std::string folder = directory.filename().string();
    return path.filename().string() + (folder.empty() ? std::string() : "   in " + folder);
}

int EditorEntityClassWindow::PickedIndex() const noexcept {
    if (!m_editor.m_entityClassPicker.has_value()) {
        return -1;
    }
    const auto it = std::find(m_shown.begin(), m_shown.end(), m_editor.m_entityClassPicker->pickedItemId);
    return it == m_shown.end() ? -1 : static_cast<int>(it - m_shown.begin());
}

void EditorEntityClassWindow::Pick(const int index) {
    if (!m_editor.m_entityClassPicker.has_value() || index < 0 || index >= static_cast<int>(m_shown.size())) {
        return;
    }
    m_editor.m_entityClassPicker->pickedItemId = m_shown[static_cast<std::size_t>(index)];
    m_editor.m_entityClassPicker->status.clear();
    RefreshDetail();
}

void EditorEntityClassWindow::Create() {
    if (!m_editor.m_entityClassPicker.has_value() || PickedIndex() < 0) {
        return;
    }
    const std::string id = m_editor.m_entityClassPicker->pickedItemId;
    if (!m_editor.CreateEntityClass(id)) {
        RefreshDetail();   // the picker says why
        return;
    }
    m_closePending = true;    // the picker is closed and the Entity Editor is opening the body
}

void EditorEntityClassWindow::Cancel() {
    m_editor.CloseEntityClassPicker();
    m_closePending = true;
}

void EditorEntityClassWindow::Rebuild() {
    m_shown.clear();
    const std::string query = m_search ? Lower(m_search->text()) : std::string();
    for (const ContentCatalogueEntityClass& entry : GetContentCatalogueEntityClasses()) {
        const ContentCatalogueItem* const item = FindContentCatalogueItem(entry.itemId);
        if (item == nullptr) {
            continue;
        }
        const std::string haystack = Lower(std::string(item->label) + " " + std::string(entry.summary) + " " +
                                              std::string(entry.detail));
        if (query.empty() || haystack.find(query) != std::string::npos) {
            m_shown.emplace_back(entry.itemId);
        }
    }
    if (m_editor.m_entityClassPicker.has_value() && PickedIndex() < 0 && !m_shown.empty()) {
        m_editor.m_entityClassPicker->pickedItemId = m_shown.front();
    }
    if (m_list) {
        m_list->relayout();
    }
    RefreshDetail();
}

void EditorEntityClassWindow::RefreshDetail() {
    if (m_list) {
        m_list->invalidate();
    }
    if (m_detail) {
        m_detail->invalidate();
    }
    if (m_editor.m_entityClassPicker.has_value() && m_status) {
        m_status->setText(m_editor.m_entityClassPicker->status);
        m_status->setColor(m_editor.m_entityClassPicker->statusIsError ? prism::ThemeColor(&prism::Theme::error)
                                                                      : prism::ThemeColor(&prism::Theme::textDim));
    }
    if (m_create) {
        m_create->setEnabled(PickedIndex() >= 0 && m_editor.IsAuthoringCommandAllowed());
    }
}

prism::Ref<prism::Widget> EditorEntityClassWindow::Build() {
    auto window = prism::vbox();
    window->add(BuildToolWindowTitleBar("Entity Class", prism::Icon::BlueprintClass, [this] { Cancel(); }));

    auto searchRow = prism::hbox(6.0F);
    searchRow->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    auto prompt = prism::make<prism::Label>("Pick the class the new entity is built on");
    prompt->setColor(prism::ThemeColor(&prism::Theme::textDim));
    searchRow->add(prompt, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_search = prism::make<prism::TextInput>("", "Search");
    m_search->onChanged = [this](const std::string&) { Rebuild(); };
    m_search->onCommit = [this](const std::string&) { Create(); };
    auto searchBox = prism::make<prism::SizeBox>();
    searchBox->setWidthOverride(240.0F);
    searchBox->setContent(m_search);
    searchRow->add(searchBox, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    window->add(searchRow);

    m_list = prism::make<ClassList>(*this);
    m_detail = prism::make<ClassDetail>(*this);
    auto listScroll = prism::make<prism::ScrollView>();
    listScroll->setContent(m_list);
    auto detailScroll = prism::make<prism::ScrollView>();
    detailScroll->setContent(m_detail);
    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(Panel(listScroll), prism::Splitter::Pane{300.0F, 180.0F, 500.0F, false});
    body->add(Panel(detailScroll), prism::Splitter::Pane{480.0F, 240.0F, 1e9F, true});
    window->add(body, prism::Slot{1.0F});

    auto buttons = prism::hbox(6.0F);
    buttons->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 8.0F));
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    buttons->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_create = prism::make<prism::Button>("Create", prism::Icon::Check, prism::ButtonStyle::Primary);
    m_create->onClick = [this] { Create(); };
    auto cancel = prism::make<prism::Button>("Cancel", prism::Icon::None, prism::ButtonStyle::Default);
    cancel->onClick = [this] { Cancel(); };
    buttons->add(m_create, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    buttons->add(cancel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footer->setChild(buttons);
    window->add(footer);
    return window;
}

void EditorEntityClassWindow::Frame() {
    const bool open = m_editor.IsEntityClassPickerOpen();
    if (m_window != 0 && m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        m_editor.CloseEntityClassPicker();
        m_closePending = true;
    }
    if (m_window != 0 && (m_closePending || !open)) {
        // Closed here, between frames: never from inside the window's own input.
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_closePending = false;
        m_search.reset();
        m_list.reset();
        m_detail.reset();
        m_status.reset();
        m_create.reset();
        m_shown.clear();
        return;
    }
    m_closePending = false;
    if (!open) {
        return;
    }
    const bool freshlyOpened = (m_window == 0);
    if (freshlyOpened) {
        m_window = m_host.OpenWindow(Build(), "Entity Class", kWidth, kHeight);
        if (m_window == 0) {
            m_editor.CloseEntityClassPicker();
            return;
        }
        m_host.App().runInWindow(m_window, [this] {
            Rebuild();
            if (m_list && prism::Application::exists()) {
                prism::Application::get().setFocus(m_list.get());
            }
        });
    }
    if (m_editor.m_entityClassPicker.has_value() && (freshlyOpened || m_editor.m_entityClassPicker->focusPending)) {
        m_editor.m_entityClassPicker->focusPending = false;
        if (!freshlyOpened) {
            m_host.App().runInWindow(m_window, [this] { Rebuild(); });
        }
        m_host.FocusWindow(m_window);
    }
    m_host.RenderWindow(m_window, {});
}

} // namespace UVE::Editor
