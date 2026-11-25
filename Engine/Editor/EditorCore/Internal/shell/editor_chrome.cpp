// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_chrome.h"

#include "prism/Icons.h"

#include "editor_host.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Editor {

prism::Ref<prism::Widget> Panel(prism::Ref<prism::Widget> child) {
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::panel));
    border->setChild(std::move(child));
    return border;
}

prism::Ref<prism::Button> ToolButton(const prism::Icon icon, std::string tooltip, std::string text) {
    auto button = prism::make<prism::Button>(std::move(text), icon, prism::ButtonStyle::Toolbar);
    button->setTooltip(std::move(tooltip));
    return button;
}

prism::Ref<prism::Widget> HSeparator() {
    return prism::make<prism::Separator>(prism::Orientation::Vertical);
}

namespace {

[[nodiscard]] int CurrentWindow() {
    return prism::Application::exists() ? prism::Application::get().currentWindow() : 0;
}

} // namespace

// ------------------------------------------------------------------------------------------ logo

// The slot is as wide as the logo plus its 1px border: the logo keeps its size, the empty space that
// stood beside it is gone.
prism::Vec2 LogoBox::measure(prism::Vec2) {
    if (m_compact) {
        if (m_spansTwoRows) {
            return m_desired = constrain(prism::Vec2(kCompactTwoRowLogo - 6.0F, kCompactTwoRowLogo));
        }
        return m_desired = constrain(prism::Vec2(kCompactLogoBox - 3.0F, kCompactTitleRow));
    }
    return m_desired = constrain(prism::Vec2(kLogoBox - 6.0F, kTitleRow + kTabRow));
}

void LogoBox::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.fillRect(r, t.header);
    const float inset = 1.0F;   // the right border
    const float side = std::max(10.0F, std::floor(std::min(r.w, r.h) - inset));
    const prism::Rect mark{r.x + (r.w - inset - side) * 0.5F, r.y + (r.h - side) * 0.5F, side, side};
    if (prism::hasBrandMark()) {
        prism::drawBrandMark(c, mark);
    } else {
        c.fillRoundRect(mark, side * 0.5F, t.accent);
    }
    c.fillRect(prism::Rect{r.right() - 1.0F, r.y, 1.0F, r.h}, t.border);
}

// ---------------------------------------------------------------------------------- document tab

void DocumentTab::setIcon(const std::uint32_t* pixels, const int size) {
    if (pixels != m_icon) {
        m_icon = pixels;
        m_iconSize = size;
        invalidate();
    }
}

void DocumentTab::setName(std::string name) {
    if (name != m_name) {
        m_name = std::move(name);
        relayout();
    }
}

void DocumentTab::setFallbackIcon(const prism::Icon icon) {
    if (icon != m_fallback) {
        m_fallback = icon;
        invalidate();
    }
}

void DocumentTab::setDirty(const bool dirty) {
    if (dirty != m_dirty) {
        m_dirty = dirty;
        invalidate();
    }
}

void DocumentTab::setCompact(const bool compact) {
    if (compact != m_compact) {
        m_compact = compact;
        relayout();
    }
}

bool DocumentTab::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && onClick) {
        onClick();
        return true;
    }
    return false;
}

prism::Vec2 DocumentTab::measure(prism::Vec2) {
    const float minW = m_compact ? 112.0F : 152.0F;
    const float extra = m_compact ? 44.0F : 54.0F;
    const float w = std::max(minW, std::ceil(prism::theme().ui->width(m_name)) + extra);
    return m_desired = constrain(prism::Vec2(w, m_compact ? kCompactTitleRow : kTabRow));
}

void DocumentTab::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    const float leftPad = m_compact ? 3.0F : 6.0F;
    const prism::Rect tab{r.x + leftPad, r.y + 2.0F, std::max(24.0F, r.w - leftPad), r.h - 2.0F};
    c.fillRoundRect(tab, 4.0F, t.panel);
    c.fillRect(prism::Rect{tab.x, tab.bottom() - 4.0F, tab.w, 4.0F}, t.panel);
    c.fillRect(prism::Rect{tab.x, tab.y, tab.w, 2.0F}, t.highlight);
    const float iconSz = m_compact ? 13.0F : 14.0F;
    const prism::Rect iconBox{tab.x + (m_compact ? 7.0F : 9.0F), std::floor(tab.y + (tab.h - iconSz) * 0.5F), iconSz,
                              iconSz};
    if (m_icon != nullptr && m_iconSize > 0) {
        c.blitBlend(m_icon, m_iconSize, m_iconSize, m_iconSize, iconBox);
    } else {
        prism::drawIcon(c, m_fallback, iconBox, t.highlight);
    }
    const float textX = m_compact ? 24.0F : 28.0F;
    const float textPadR = m_compact ? 18.0F : 20.0F;
    c.textIn(*t.ui, prism::Rect{tab.x + textX, tab.y, std::max(16.0F, tab.w - textX - textPadR), tab.h}, m_name,
             t.textBright, prism::HAlign::Left, true);
    if (m_dirty) {
        c.fillCircle(prism::Vec2{tab.right() - 10.0F, tab.y + tab.h * 0.5F}, 3.0F, t.textBright);
    }
}

// ------------------------------------------------------------------------------------ drag strip

prism::Vec2 DragStrip::measure(prism::Vec2) {
    return m_desired = constrain(prism::Vec2(m_extra ? 0.0F : 40.0F, m_extra ? 0.0F : kTitleRow));
}

void DragStrip::arrange(prism::Rect r) {
    prism::Widget::arrange(r);
    if (prism::Application::exists()) {
        const int window = CurrentWindow();
        prism::Application::get().platform().setWindowCaption(m_extra ? ExtraCaptionId(window) : window,
                                                              visible() ? r : prism::Rect{});
    }
}

void DragStrip::paint(prism::Canvas& c) {
    if (m_extra) {
        return;
    }
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

bool DragStrip::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && e.clicks >= 2 && prism::Application::exists()) {
        prism::Application::get().platform().toggleMaximizeWindow(CurrentWindow());
    }
    return e.button == prism::MouseButton::Left;
}

// --------------------------------------------------------------------------------- camera screen

namespace {
constexpr float kCameraScreenCaption = 20.0F;
constexpr float kCameraScreenMargin = 12.0F;

/// Clean PRISM window chrome button: Minimize, single Floating/Full toggle, or Close (red hover).
class WindowChromeButton final : public prism::Widget {
public:
    enum class Kind : std::uint8_t { Minimize, ToggleFloatFull, Close };
    WindowChromeButton(const Kind kind, std::function<void()> action)
        : m_kind(kind), m_action(std::move(action)) {}

    prism::Vec2 measure(prism::Vec2) override {
        const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
        const float w = singleSurface ? 28.0F : 26.0F;
        const float h = singleSurface ? 20.0F : 18.0F;
        return m_desired = constrain(prism::Vec2(w, h));
    }

    const std::string& tooltipAt(prism::Vec2) const override {
        static const std::string kMinimize{"Minimize"};
        static const std::string kClose{"Close"};
        static const std::string kFloating{"Floating Window"};
        static const std::string kFull{"Full Screen"};
        if (m_kind == Kind::Minimize) {
            return kMinimize;
        }
        if (m_kind == Kind::Close) {
            return kClose;
        }
        return IsMaximized() ? kFloating : kFull;
    }

    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        const bool danger = m_kind == Kind::Close;
        if (m_pressed || m_hovered) {
            const prism::Color bg = danger ? (m_pressed ? t.error.darken(0.15F) : t.error)
                                           : (m_pressed ? t.controlPress : t.controlHover);
            c.fillRoundRect(r.inflated(-1.0F), 3.0F, bg);
        }
        const prism::Color fg = (danger && (m_hovered || m_pressed)) ? t.textBright
                                : (m_hovered ? t.textBright : t.textDim);
        if (m_kind == Kind::Minimize) {
            const float y = std::floor(r.y + r.h * 0.5F + 2.0F);
            c.hline(r.x + 8.0F, r.right() - 8.0F, y, fg);
        } else if (m_kind == Kind::Close) {
            const float s = 8.0F;
            const prism::Rect box{std::floor(r.x + (r.w - s) * 0.5F), std::floor(r.y + (r.h - s) * 0.5F), s, s};
            prism::drawIcon(c, prism::Icon::Close, box, fg, 1.35F);
        } else {
            const bool full = IsMaximized();
            const float cx = std::floor(r.x + r.w * 0.5F);
            const float cy = std::floor(r.y + r.h * 0.5F);
            if (full) {
                // Currently Full -> show dual-pane Floating Window icon.
                const prism::Rect back{cx - 3.0F, cy - 5.0F, 8.0F, 7.0F};
                const prism::Rect front{cx - 5.0F, cy - 2.0F, 8.0F, 7.0F};
                c.strokeRect(back, fg, 1);
                c.fillRect(front, (m_hovered || m_pressed) ? t.controlHover : t.header);
                c.strokeRect(front, fg, 1);
                c.hline(front.x + 1.0F, front.right() - 1.0F, front.y + 1.0F, fg);
            } else {
                // Currently Floating -> show crisp Full Screen frame icon.
                const prism::Rect box{cx - 5.0F, cy - 4.0F, 10.0F, 8.0F};
                c.strokeRect(box, fg, 1);
                c.hline(box.x + 1.0F, box.right() - 1.0F, box.y + 1.0F, fg);
            }
        }
    }

    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button != prism::MouseButton::Left) {
            return false;
        }
        m_pressed = true;
        invalidate();
        return true;
    }

    bool onMouseUp(const prism::MouseEvent& e) override {
        if (!m_pressed) {
            return false;
        }
        m_pressed = false;
        invalidate();
        if (rect().contains(e.pos) && m_action) {
            m_action();
        }
        return true;
    }

    void onMouseLeave() override {
        m_pressed = false;
        invalidate();
    }

private:
    [[nodiscard]] static bool IsMaximized() {
        return prism::Application::exists() &&
               prism::Application::get().platform().windowMaximized(CurrentWindow());
    }

    Kind m_kind;
    std::function<void()> m_action;
    bool m_pressed = false;
};

} // namespace

CameraScreen::CameraScreen() {
    auto view = prism::make<prism::EngineView>();
    view->mode = prism::EngineView::Mode::Hole;
    m_view = view;
    addChild(m_view);
}

void CameraScreen::setTitle(std::string title) {
    if (title != m_title) {
        m_title = std::move(title);
        invalidate();
    }
}

prism::Vec2 CameraScreen::measure(prism::Vec2 available) {
    return m_desired = constrain(available);
}

void CameraScreen::arrange(const prism::Rect r) {
    // Its corner of the view: a quarter of the width, 16:9, never smaller than readable.
    const float w = std::floor(std::clamp(r.w * 0.28F, 180.0F, 420.0F));
    const float h = std::floor(w * 9.0F / 16.0F);
    const prism::Rect frame{r.right() - w - kCameraScreenMargin - 2.0F,
                            r.bottom() - h - kCameraScreenCaption - kCameraScreenMargin - 2.0F, w + 2.0F,
                            h + kCameraScreenCaption + 2.0F};
    prism::Widget::arrange(frame);
    m_viewRect = prism::Rect{frame.x + 1.0F, frame.y + kCameraScreenCaption + 1.0F, w, h};
    m_view->arrange(m_viewRect);
}

void CameraScreen::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.shadow(r, 4, 10, prism::Color(0, 0, 0, 160));
    c.fillRect(prism::Rect{r.x, r.y, r.w, kCameraScreenCaption + 1.0F}, t.header);
    c.textIn(*t.small, prism::Rect{r.x + 8.0F, r.y, r.w - 16.0F, kCameraScreenCaption}, m_title, t.textBright,
             prism::HAlign::Left, true);
    m_view->paint(c);
    c.strokeRect(r, t.borderStrong, 1);
}

bool CameraScreen::onMouseDown(const prism::MouseEvent& e) {
    return e.button == prism::MouseButton::Left;   // its own corner: a click there is not a viewport click
}

// -------------------------------------------------------------------------------- window buttons

void AddWindowButtons(prism::Box& row, std::function<void()> onClose, const bool mainWindow) {
    const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    if (singleSurface && mainWindow) {
        return;
    }
    if (!singleSurface) {
        auto minimize = prism::make<WindowChromeButton>(
            WindowChromeButton::Kind::Minimize,
            [] { prism::Application::get().platform().minimizeWindow(CurrentWindow()); });
        row.add(minimize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    // Single Floating <-> Full toggle button (works on both Desktop OS windows and Mobile single-surface pages).
    auto toggleFloatFull = prism::make<WindowChromeButton>(
        WindowChromeButton::Kind::ToggleFloatFull,
        [] { prism::Application::get().platform().toggleMaximizeWindow(CurrentWindow()); });
    auto close = prism::make<WindowChromeButton>(WindowChromeButton::Kind::Close, std::move(onClose));
    row.add(toggleFloatFull, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row.add(close, prism::Slot{0.0F, prism::Margin(0.0F, 0.0F, 3.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
}

prism::Ref<prism::Widget> BuildToolWindowTitleBar(std::string title, const prism::Icon icon,
                                                     std::function<void()> onClose,
                                                     prism::Ref<prism::Box>* const outRow) {
    const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    auto row = prism::hbox(5.0F);
    row->setPadding(prism::Margin(8.0F, 0.0F, 2.0F, 0.0F));
    if (icon != prism::Icon::None) {
        auto iconBtn = prism::make<prism::Button>("", icon, prism::ButtonStyle::Flat);
        iconBtn->setEnabled(false);
        row->add(iconBtn, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    auto titleText = prism::make<prism::Label>(std::move(title), prism::FontRole::Semibold);
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    row->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<DragStrip>(), prism::Slot{1.0F});
    AddWindowButtons(*row, std::move(onClose), /*mainWindow*/ false);
    if (outRow != nullptr) {
        *outRow = row;
    }
    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(singleSurface ? kCompactTitleRow + 2.0F : kTitleRow + 4.0F);
    sized->setContent(row);
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(sized);
    return bar;
}

prism::Ref<prism::Widget> BuildEditorHeader(EditorHeaderDesc desc) {
    const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    if (singleSurface) {
        // Mobile header:
        // - Clean plain "Menu" button (no arrow/chevron)
        // - When toolbarRow is present, LogoBox spans BOTH Row 1 (Menu + DocumentTab + Buttons) AND
        //   Row 2 (Play/Toolbar row) as a prominent 44x44 square!
        auto row1 = prism::hbox(2.0F);
        auto menuBtn = prism::make<prism::Button>("Menu", prism::Icon::None, prism::ButtonStyle::Toolbar);
        menuBtn->setTooltip("Menu");
        auto entries = std::make_shared<std::vector<EditorHeaderMenuEntry>>(std::move(desc.menus));
        auto tail = std::move(desc.compactMenuTail);
        std::weak_ptr<prism::Button> weakBtn = menuBtn;
        menuBtn->onClick = [entries, tail, weakBtn] {
            std::vector<prism::MenuItem> items;
            for (const EditorHeaderMenuEntry& entry : *entries) {
                std::vector<prism::MenuItem> sub = entry.factory ? entry.factory() : std::vector<prism::MenuItem>{};
                prism::MenuExtensions::global().merge(entry.title, sub);
                items.push_back(prism::MenuItem::Sub(entry.title, std::move(sub)));
            }
            if (tail) {
                for (prism::MenuItem& extra : tail()) {
                    items.push_back(std::move(extra));
                }
            }
            const prism::Rect anchor = weakBtn.lock() ? weakBtn.lock()->rect() : prism::Rect{0.0F, kCompactTitleRow, 0.0F, 0.0F};
            prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
        };
        row1->add(menuBtn, prism::Slot{0.0F, prism::Margin(2.0F, 0.0F, 2.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});

        if (desc.documentTab) {
            desc.documentTab->setCompact(true);
            row1->add(desc.documentTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
        }
        row1->add(prism::make<DragStrip>(), prism::Slot{1.0F});
        AddWindowButtons(*row1, std::move(desc.onClose), desc.mainWindow);

        auto row1Sized = prism::make<prism::SizeBox>();
        row1Sized->setHeightOverride(kCompactTitleRow);
        row1Sized->setContent(row1);

        // As on desktop: the logo sits beside the title row only, and the toolbar runs the full width
        // under it (a logo spanning both rows pushed every editor's toolbar right and took its height).
        auto outer = prism::hbox();
        outer->add(prism::make<LogoBox>(/*compact*/ true, /*spansTwoRows*/ false),
                   prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
        outer->add(row1Sized, prism::Slot{1.0F});

        if (desc.outTitleRow != nullptr) {
            *desc.outTitleRow = outer;
        }

        auto bar = prism::make<prism::Border>();
        bar->setBackground(prism::ThemeColor(&prism::Theme::header));
        bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
        bar->setChild(outer);
        if (desc.toolbarRow != nullptr) {
            auto combined = prism::vbox();
            combined->add(bar);
            combined->add(desc.toolbarRow);
            return combined;
        }
        return bar;
    }

    // Desktop: slim classic 2-row header with MenuBar on Row 1 and DocumentTab on Row 2 beside LogoBox.
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(10.0F);
    for (EditorHeaderMenuEntry& entry : desc.menus) {
        menus->addMenu(std::move(entry.title), std::move(entry.factory));
    }
    row->add(menus, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<DragStrip>(), prism::Slot{1.0F});
    AddWindowButtons(*row, std::move(desc.onClose), desc.mainWindow);

    auto tabRow = prism::hbox();
    if (desc.documentTab) {
        desc.documentTab->setCompact(false);
        tabRow->add(desc.documentTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    }
    tabRow->add(prism::make<DragStrip>(true), prism::Slot{1.0F});

    auto titleRow = prism::make<prism::SizeBox>();
    titleRow->setHeightOverride(kTitleRow);
    titleRow->setContent(row);
    auto tabRowBox = prism::make<prism::SizeBox>();
    tabRowBox->setHeightOverride(kTabRow);
    tabRowBox->setContent(tabRow);

    auto right = prism::vbox();
    right->add(titleRow);
    right->add(tabRowBox);

    auto top = prism::hbox();
    top->add(prism::make<LogoBox>(/*compact*/ false, /*spansTwoRows*/ false));
    top->add(right, prism::Slot{1.0F});
    if (desc.outTitleRow != nullptr) {
        *desc.outTitleRow = top;
    }

    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(top);
    if (desc.toolbarRow != nullptr) {
        auto combined = prism::vbox();
        combined->add(bar);
        combined->add(desc.toolbarRow);
        return combined;
    }
    return bar;
}

} // namespace UVE::Editor
