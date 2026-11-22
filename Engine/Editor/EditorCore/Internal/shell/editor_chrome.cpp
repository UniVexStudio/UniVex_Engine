// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_chrome.h"

#include "prism/Icons.h"

#include "editor_host.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/EngineView.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Editor {

prism::Ref<prism::Widget> PanelUVE(prism::Ref<prism::Widget> child) {
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::panel));
    border->setChild(std::move(child));
    return border;
}

prism::Ref<prism::Button> ToolButtonUVE(const prism::Icon icon, std::string tooltip, std::string text) {
    auto button = prism::make<prism::Button>(std::move(text), icon, prism::ButtonStyle::Toolbar);
    button->setTooltip(std::move(tooltip));
    return button;
}

prism::Ref<prism::Widget> HSeparatorUVE() {
    return prism::make<prism::Separator>(prism::Orientation::Vertical);
}

namespace {

[[nodiscard]] int CurrentWindowUVE() {
    return prism::Application::exists() ? prism::Application::get().currentWindow() : 0;
}

} // namespace

// ------------------------------------------------------------------------------------------ logo

prism::Vec2 LogoBoxUVE::measure(prism::Vec2) {
    if (m_compact) {
        return m_desired = constrain(prism::Vec2(kCompactLogoBoxUVE, kCompactTitleRowUVE));
    }
    return m_desired = constrain(prism::Vec2(kLogoBoxUVE, kTitleRowUVE + kTabRowUVE));
}

void LogoBoxUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.fillRect(r, t.header);
    const float inset = m_compact ? 4.0F : 7.0F;
    const float side = std::max(10.0F, std::floor(std::min(r.w, r.h) - inset));
    const prism::Rect mark{r.x + (r.w - side) * 0.5F, r.y + (r.h - side) * 0.5F, side, side};
    if (prism::hasBrandMark()) {
        prism::drawBrandMark(c, mark);
    } else {
        c.fillRoundRect(mark, side * 0.5F, t.accent);
    }
    c.fillRect(prism::Rect{r.right() - 1.0F, r.y, 1.0F, r.h}, t.border);
}

// ---------------------------------------------------------------------------------- document tab

void DocumentTabUVE::setIcon(const std::uint32_t* pixels, const int size) {
    if (pixels != m_icon) {
        m_icon = pixels;
        m_iconSize = size;
        invalidate();
    }
}

void DocumentTabUVE::setName(std::string name) {
    if (name != m_name) {
        m_name = std::move(name);
        relayout();
    }
}

void DocumentTabUVE::setFallbackIcon(const prism::Icon icon) {
    if (icon != m_fallback) {
        m_fallback = icon;
        invalidate();
    }
}

void DocumentTabUVE::setDirty(const bool dirty) {
    if (dirty != m_dirty) {
        m_dirty = dirty;
        invalidate();
    }
}

void DocumentTabUVE::setCompact(const bool compact) {
    if (compact != m_compact) {
        m_compact = compact;
        relayout();
    }
}

bool DocumentTabUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && onClick) {
        onClick();
        return true;
    }
    return false;
}

prism::Vec2 DocumentTabUVE::measure(prism::Vec2) {
    const float minW = m_compact ? 112.0F : 152.0F;
    const float extra = m_compact ? 44.0F : 54.0F;
    const float w = std::max(minW, std::ceil(prism::theme().ui->width(m_name)) + extra);
    return m_desired = constrain(prism::Vec2(w, m_compact ? kCompactTitleRowUVE : kTabRowUVE));
}

void DocumentTabUVE::paint(prism::Canvas& c) {
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

prism::Vec2 DragStripUVE::measure(prism::Vec2) {
    return m_desired = constrain(prism::Vec2(m_extra ? 0.0F : 40.0F, m_extra ? 0.0F : kTitleRowUVE));
}

void DragStripUVE::arrange(prism::Rect r) {
    prism::Widget::arrange(r);
    if (prism::Application::exists()) {
        const int window = CurrentWindowUVE();
        prism::Application::get().platform().setWindowCaption(m_extra ? ExtraCaptionIdUVE(window) : window,
                                                              visible() ? r : prism::Rect{});
    }
}

void DragStripUVE::paint(prism::Canvas& c) {
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

bool DragStripUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && e.clicks >= 2 && prism::Application::exists()) {
        prism::Application::get().platform().toggleMaximizeWindow(CurrentWindowUVE());
    }
    return e.button == prism::MouseButton::Left;
}

// --------------------------------------------------------------------------------- camera screen

namespace {
constexpr float kCameraScreenCaptionUVE = 20.0F;
constexpr float kCameraScreenMarginUVE = 12.0F;
} // namespace

CameraScreenUVE::CameraScreenUVE() {
    auto view = prism::make<prism::EngineView>();
    view->mode = prism::EngineView::Mode::Hole;
    m_view = view;
    addChild(m_view);
}

void CameraScreenUVE::setTitle(std::string title) {
    if (title != m_title) {
        m_title = std::move(title);
        invalidate();
    }
}

prism::Vec2 CameraScreenUVE::measure(prism::Vec2 available) {
    return m_desired = constrain(available);
}

void CameraScreenUVE::arrange(const prism::Rect r) {
    // Its corner of the view: a quarter of the width, 16:9, never smaller than readable.
    const float w = std::floor(std::clamp(r.w * 0.28F, 180.0F, 420.0F));
    const float h = std::floor(w * 9.0F / 16.0F);
    const prism::Rect frame{r.right() - w - kCameraScreenMarginUVE - 2.0F,
                            r.bottom() - h - kCameraScreenCaptionUVE - kCameraScreenMarginUVE - 2.0F, w + 2.0F,
                            h + kCameraScreenCaptionUVE + 2.0F};
    prism::Widget::arrange(frame);
    m_viewRect = prism::Rect{frame.x + 1.0F, frame.y + kCameraScreenCaptionUVE + 1.0F, w, h};
    m_view->arrange(m_viewRect);
}

void CameraScreenUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.shadow(r, 4, 10, prism::Color(0, 0, 0, 160));
    c.fillRect(prism::Rect{r.x, r.y, r.w, kCameraScreenCaptionUVE + 1.0F}, t.header);
    c.textIn(*t.small, prism::Rect{r.x + 8.0F, r.y, r.w - 16.0F, kCameraScreenCaptionUVE}, m_title, t.textBright,
             prism::HAlign::Left, true);
    m_view->paint(c);
    c.strokeRect(r, t.borderStrong, 1);
}

bool CameraScreenUVE::onMouseDown(const prism::MouseEvent& e) {
    return e.button == prism::MouseButton::Left;   // its own corner: a click there is not a viewport click
}

// -------------------------------------------------------------------------------- window buttons

void AddWindowButtonsUVE(prism::Box& row, std::function<void()> onClose, const bool mainWindow) {
    // One full-screen surface (phones): the main window belongs to the system and has no buttons;
    // another window is a page over it, which minimize hides and close ends - it cannot be resized.
    const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    if (singleSurface && mainWindow) {
        return;
    }
    auto minimize = prism::make<prism::Button>("", prism::Icon::Minus, prism::ButtonStyle::Flat);
    minimize->setTooltip("Minimize");
    minimize->onClick = [] { prism::Application::get().platform().minimizeWindow(CurrentWindowUVE()); };
    auto maximize = prism::make<prism::Button>("", prism::Icon::Frame, prism::ButtonStyle::Flat);
    maximize->setTooltip("Maximize / Restore");
    maximize->onClick = [] { prism::Application::get().platform().toggleMaximizeWindow(CurrentWindowUVE()); };
    auto close = prism::make<prism::Button>("", prism::Icon::Close, prism::ButtonStyle::Flat);
    close->setTooltip("Close");
    close->onClick = [onClose = std::move(onClose)] {
        if (onClose) {
            onClose();
        }
    };
    row.add(minimize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    if (!singleSurface) {
        row.add(maximize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    row.add(close, prism::Slot{0.0F, prism::Margin(0.0F, 0.0F, 4.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
}

prism::Ref<prism::Widget> BuildEditorHeaderUVE(EditorHeaderDescUVE desc) {
    const bool singleSurface = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    if (singleSurface) {
        // Consolidated single slim row on mobile:
        // [Slim Logo] [Menu ▾] [Viewport / Document Tab] [Spacer] [- x (sub-windows only)]
        auto row = prism::hbox(2.0F);
        row->add(prism::make<LogoBoxUVE>(/*compact*/ true),
                 prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});

        auto menuBtn = prism::make<prism::Button>("Menu", prism::Icon::ChevronDown, prism::ButtonStyle::Toolbar);
        menuBtn->setTooltip("Editor menu");
        auto entries = std::make_shared<std::vector<EditorHeaderMenuEntryUVE>>(std::move(desc.menus));
        auto tail = std::move(desc.compactMenuTail);
        std::weak_ptr<prism::Button> weakBtn = menuBtn;
        menuBtn->onClick = [entries, tail, weakBtn] {
            std::vector<prism::MenuItem> items;
            for (const EditorHeaderMenuEntryUVE& entry : *entries) {
                std::vector<prism::MenuItem> sub = entry.factory ? entry.factory() : std::vector<prism::MenuItem>{};
                prism::MenuExtensions::global().merge(entry.title, sub);
                items.push_back(prism::MenuItem::Sub(entry.title, std::move(sub)));
            }
            if (tail) {
                for (prism::MenuItem& extra : tail()) {
                    items.push_back(std::move(extra));
                }
            }
            const prism::Rect anchor = weakBtn.lock() ? weakBtn.lock()->rect() : prism::Rect{0.0F, kCompactTitleRowUVE, 0.0F, 0.0F};
            prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
        };
        row->add(menuBtn, prism::Slot{0.0F, prism::Margin(2.0F, 0.0F, 2.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});

        if (desc.documentTab) {
            desc.documentTab->setCompact(true);
            row->add(desc.documentTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
        }
        row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
        AddWindowButtonsUVE(*row, std::move(desc.onClose), desc.mainWindow);
        if (desc.outTitleRow != nullptr) {
            *desc.outTitleRow = row;
        }

        auto sized = prism::make<prism::SizeBox>();
        sized->setHeightOverride(kCompactTitleRowUVE);
        sized->setContent(row);

        auto bar = prism::make<prism::Border>();
        bar->setBackground(prism::ThemeColor(&prism::Theme::header));
        bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
        bar->setChild(sized);
        return bar;
    }

    // Desktop: slim classic 2-row header with MenuBar on Row 1 and DocumentTab on Row 2 beside LogoBox.
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(10.0F);
    for (EditorHeaderMenuEntryUVE& entry : desc.menus) {
        menus->addMenu(std::move(entry.title), std::move(entry.factory));
    }
    row->add(menus, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*row, std::move(desc.onClose), desc.mainWindow);

    auto tabRow = prism::hbox();
    if (desc.documentTab) {
        desc.documentTab->setCompact(false);
        tabRow->add(desc.documentTab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    }
    tabRow->add(prism::make<DragStripUVE>(true), prism::Slot{1.0F});

    auto titleRow = prism::make<prism::SizeBox>();
    titleRow->setHeightOverride(kTitleRowUVE);
    titleRow->setContent(row);
    auto tabRowBox = prism::make<prism::SizeBox>();
    tabRowBox->setHeightOverride(kTabRowUVE);
    tabRowBox->setContent(tabRow);

    auto right = prism::vbox();
    right->add(titleRow);
    right->add(tabRowBox);

    auto top = prism::hbox();
    top->add(prism::make<LogoBoxUVE>(/*compact*/ false));
    top->add(right, prism::Slot{1.0F});
    if (desc.outTitleRow != nullptr) {
        *desc.outTitleRow = top;
    }

    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(top);
    return bar;
}

} // namespace UVE::Editor
