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

namespace UVE::Editor {
namespace {

constexpr int kWidthUVE = 780;
constexpr int kHeightUVE = 480;
constexpr float kRowHeightUVE = 52.0F;

[[nodiscard]] std::string LowerUVE(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Words of `text` laid in lines no wider than `width` in `font`.
[[nodiscard]] std::vector<std::string> WrapUVE(const prism::Font& font, std::string_view text, const float width) {
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
class ClassListUVE final : public prism::Widget {
public:
    explicit ClassListUVE(EditorEntityClassWindowUVE& window) : m_window(window) {}
    prism::Vec2 measure(const prism::Vec2 avail) override {
        return {avail.x, kRowHeightUVE * static_cast<float>(m_window.ShownUVE().size())};
    }
    bool focusable() const override { return true; }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const std::vector<std::string>& shown = m_window.ShownUVE();
        const int picked = m_window.PickedIndexUVE();
        for (std::size_t i = 0U; i < shown.size(); ++i) {
            const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(shown[i]);
            if (item == nullptr) {
                continue;
            }
            const prism::Rect row{m_rect.x, m_rect.y + kRowHeightUVE * static_cast<float>(i), m_rect.w, kRowHeightUVE};
            if (static_cast<int>(i) == picked) {
                c.fillRect(row, m_focused ? t.rowSelected : t.rowSelectedInactive);
                c.fillRect(prism::Rect{row.x, row.y, 2.0F, row.h}, t.accent);
            } else if (static_cast<int>(i) == m_hover) {
                c.fillRect(row, t.rowHover);
            }
            if (const EditorIconsUVE::ImageUVE icon = m_window.IconsUVE().ObjectUVE(GetContentCatalogueIconKindUVE(*item), 32);
                icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{row.x + 10.0F, row.y + 10.0F, 32.0F, 32.0F});
            }
            const float x = row.x + 54.0F;
            c.textIn(*t.semibold, prism::Rect{x, row.y + 7.0F, row.w - x - 8.0F, 20.0F}, item->label, t.textBright);
            std::string_view summary;
            for (const ContentCatalogueEntityClassUVE& entry : GetContentCatalogueEntityClassesUVE()) {
                if (entry.itemId == item->id) {
                    summary = entry.summary;
                }
            }
            c.textIn(*t.small, prism::Rect{x, row.y + 27.0F, row.w - x - 8.0F, 18.0F}, summary, t.textDim);
        }
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        const int hover = RowAtUVE(e.pos);
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
        const int row = RowAtUVE(e.pos);
        if (row >= 0) {
            m_window.PickUVE(row);
            if (e.clicks >= 2) {
                m_window.CreateUVE();
            }
        }
        return true;
    }
    bool onKeyDown(const prism::KeyEvent& e) override {
        const int count = static_cast<int>(m_window.ShownUVE().size());
        const int picked = m_window.PickedIndexUVE();
        if (e.key == prism::Key::Down && count > 0) {
            m_window.PickUVE(std::min(count - 1, picked + 1));
            return true;
        }
        if (e.key == prism::Key::Up && count > 0) {
            m_window.PickUVE(std::max(0, picked - 1));
            return true;
        }
        if (e.key == prism::Key::Enter) {
            m_window.CreateUVE();
            return true;
        }
        if (e.key == prism::Key::Escape) {
            m_window.CancelUVE();
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] int RowAtUVE(const prism::Vec2 p) const {
        if (!m_rect.contains(p)) {
            return -1;
        }
        const int row = static_cast<int>((p.y - m_rect.y) / kRowHeightUVE);
        return row < static_cast<int>(m_window.ShownUVE().size()) ? row : -1;
    }
    EditorEntityClassWindowUVE& m_window;
    int m_hover = -1;
};

/// The picked class: its name, summary, what it is for, the file it makes and what is inside.
class ClassDetailUVE final : public prism::Widget {
public:
    explicit ClassDetailUVE(EditorEntityClassWindowUVE& window) : m_window(window) {}
    prism::Vec2 measure(const prism::Vec2 avail) override { return avail; }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRect(m_rect, t.panel);
        const int picked = m_window.PickedIndexUVE();
        const std::vector<std::string>& shown = m_window.ShownUVE();
        if (picked < 0 || picked >= static_cast<int>(shown.size())) {
            c.textIn(*t.ui, prism::Rect{m_rect.x + 16.0F, m_rect.y + 16.0F, m_rect.w - 32.0F, 20.0F},
                     "Nothing matches the search.", t.textDim);
            return;
        }
        const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(shown[static_cast<std::size_t>(picked)]);
        if (item == nullptr) {
            return;
        }
        const ContentCatalogueEntityClassUVE* entry = nullptr;
        for (const ContentCatalogueEntityClassUVE& candidate : GetContentCatalogueEntityClassesUVE()) {
            if (candidate.itemId == item->id) {
                entry = &candidate;
            }
        }
        const float left = m_rect.x + 16.0F;
        const float width = m_rect.w - 32.0F;
        float y = m_rect.y + 14.0F;
        if (const EditorIconsUVE::ImageUVE icon = m_window.IconsUVE().ObjectUVE(GetContentCatalogueIconKindUVE(*item), 48);
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
            for (const std::string& line : WrapUVE(*t.ui, text, width)) {
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
        paragraph(m_window.MakesTextUVE(item->id));
        heading("INSIDE");
        paragraph(item->objects.size() <= 1U ? std::string_view("Only the body itself: what it wears is yours to add once it is open.")
                                              : std::string_view("The body and the objects it starts with."));
    }

private:
    EditorEntityClassWindowUVE& m_window;
};

} // namespace

EditorEntityClassWindowUVE::EditorEntityClassWindowUVE(EditorUVE& editor, EditorHostUVE& host)
    : m_editor(editor), m_host(host) {}

EditorEntityClassWindowUVE::~EditorEntityClassWindowUVE() = default;

std::string EditorEntityClassWindowUVE::MakesTextUVE(const std::string_view itemId) const {
    const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(itemId);
    if (item == nullptr || !m_editor.m_entityClassPicker.has_value()) {
        return {};
    }
    const std::filesystem::path& directory = m_editor.m_entityClassPicker->directory;
    const std::string stem(item->fileStem.empty() ? item->label : item->fileStem);
    const std::filesystem::path path = EditorUVE::MakeUniqueContentPathUVE(directory, stem, ".uventity");
    const std::string folder = directory.filename().string();
    return path.filename().string() + (folder.empty() ? std::string() : "   in " + folder);
}

int EditorEntityClassWindowUVE::PickedIndexUVE() const noexcept {
    if (!m_editor.m_entityClassPicker.has_value()) {
        return -1;
    }
    const auto it = std::find(m_shown.begin(), m_shown.end(), m_editor.m_entityClassPicker->pickedItemId);
    return it == m_shown.end() ? -1 : static_cast<int>(it - m_shown.begin());
}

void EditorEntityClassWindowUVE::PickUVE(const int index) {
    if (!m_editor.m_entityClassPicker.has_value() || index < 0 || index >= static_cast<int>(m_shown.size())) {
        return;
    }
    m_editor.m_entityClassPicker->pickedItemId = m_shown[static_cast<std::size_t>(index)];
    m_editor.m_entityClassPicker->status.clear();
    RefreshDetailUVE();
}

void EditorEntityClassWindowUVE::CreateUVE() {
    if (!m_editor.m_entityClassPicker.has_value() || PickedIndexUVE() < 0) {
        return;
    }
    const std::string id = m_editor.m_entityClassPicker->pickedItemId;
    if (!m_editor.CreateEntityClassUVE(id)) {
        RefreshDetailUVE();   // the picker says why
        return;
    }
    m_closePending = true;    // the picker is closed and the Entity Editor is opening the body
}

void EditorEntityClassWindowUVE::CancelUVE() {
    m_editor.CloseEntityClassPickerUVE();
    m_closePending = true;
}

void EditorEntityClassWindowUVE::RebuildUVE() {
    m_shown.clear();
    const std::string query = m_search ? LowerUVE(m_search->text()) : std::string();
    for (const ContentCatalogueEntityClassUVE& entry : GetContentCatalogueEntityClassesUVE()) {
        const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(entry.itemId);
        if (item == nullptr) {
            continue;
        }
        const std::string haystack = LowerUVE(std::string(item->label) + " " + std::string(entry.summary) + " " +
                                              std::string(entry.detail));
        if (query.empty() || haystack.find(query) != std::string::npos) {
            m_shown.emplace_back(entry.itemId);
        }
    }
    if (m_editor.m_entityClassPicker.has_value() && PickedIndexUVE() < 0 && !m_shown.empty()) {
        m_editor.m_entityClassPicker->pickedItemId = m_shown.front();
    }
    if (m_list) {
        m_list->relayout();
    }
    RefreshDetailUVE();
}

void EditorEntityClassWindowUVE::RefreshDetailUVE() {
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
        m_create->setEnabled(PickedIndexUVE() >= 0 && m_editor.IsAuthoringCommandAllowedUVE());
    }
}

prism::Ref<prism::Widget> EditorEntityClassWindowUVE::BuildUVE() {
    auto window = prism::vbox();

    auto title = prism::hbox(6.0F);
    title->setPadding(prism::Margin(10.0F, 0.0F, 0.0F, 0.0F));
    auto titleText = prism::make<prism::Label>("Entity Class");
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    title->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    title->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*title, [this] { CancelUVE(); });
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
    auto prompt = prism::make<prism::Label>("Pick the class the new entity is built on");
    prompt->setColor(prism::ThemeColor(&prism::Theme::textDim));
    searchRow->add(prompt, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_search = prism::make<prism::TextInput>("", "Search");
    m_search->onChanged = [this](const std::string&) { RebuildUVE(); };
    m_search->onCommit = [this](const std::string&) { CreateUVE(); };
    auto searchBox = prism::make<prism::SizeBox>();
    searchBox->setWidthOverride(240.0F);
    searchBox->setContent(m_search);
    searchRow->add(searchBox, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    window->add(searchRow);

    m_list = prism::make<ClassListUVE>(*this);
    m_detail = prism::make<ClassDetailUVE>(*this);
    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(PanelUVE(m_list), prism::Splitter::Pane{300.0F, 220.0F, 500.0F, false});
    body->add(PanelUVE(m_detail), prism::Splitter::Pane{480.0F, 300.0F, 1e9F, true});
    window->add(body, prism::Slot{1.0F});

    auto buttons = prism::hbox(6.0F);
    buttons->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 8.0F));
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    buttons->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_create = prism::make<prism::Button>("Create", prism::Icon::Check, prism::ButtonStyle::Primary);
    m_create->onClick = [this] { CreateUVE(); };
    auto cancel = prism::make<prism::Button>("Cancel", prism::Icon::None, prism::ButtonStyle::Default);
    cancel->onClick = [this] { CancelUVE(); };
    buttons->add(m_create, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    buttons->add(cancel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footer->setChild(buttons);
    window->add(footer);
    return window;
}

void EditorEntityClassWindowUVE::FrameUVE() {
    const bool open = m_editor.IsEntityClassPickerOpenUVE();
    if (m_window != 0 && m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        m_editor.CloseEntityClassPickerUVE();
        m_closePending = true;
    }
    if (m_window != 0 && (m_closePending || !open)) {
        // Closed here, between frames: never from inside the window's own input.
        m_host.CloseWindowUVE(m_window);
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
    if (m_window == 0) {
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Entity Class", kWidthUVE, kHeightUVE);
        if (m_window == 0) {
            m_editor.CloseEntityClassPickerUVE();
            return;
        }
        m_host.AppUVE().runInWindow(m_window, [this] {
            RebuildUVE();
            if (m_list && prism::Application::exists()) {
                prism::Application::get().setFocus(m_list.get());
            }
        });
        m_host.FocusWindowUVE(m_window);
    }
    m_host.RenderWindowUVE(m_window, {});
}

} // namespace UVE::Editor
