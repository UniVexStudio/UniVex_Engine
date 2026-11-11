// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_chrome.h"

#include "editor_prism_host.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

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
    return m_desired = constrain(prism::Vec2(kLogoBoxUVE, kTitleRowUVE + kTabRowUVE));
}

void LogoBoxUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.fillRect(r, t.header);
    // The slot hugs the mark: a few pixels of the bar around it, no more.
    const float side = std::floor(std::min(r.w, r.h) - 8.0F);
    const prism::Rect mark{r.x + (r.w - side) * 0.5F, r.y + (r.h - side) * 0.5F, side, side};
    c.fillRoundRect(mark, 5.0F, prism::Color(32, 112, 228));
    c.strokeRoundRect(mark, 5.0F, prism::Color(82, 160, 255, 220), 1);
    // The U, drawn: two stems joined by a rounded bowl.
    const float uw = side * 0.46F;
    const float uh = side * 0.52F;
    const float th = std::max(2.0F, std::round(side * 0.12F));
    const float ux = mark.x + (mark.w - uw) * 0.5F;
    const float uy = mark.y + (mark.h - uh) * 0.5F;
    const prism::Color white(255, 255, 255);
    c.fillRect(prism::Rect{ux, uy, th, uh - uw * 0.5F}, white);
    c.fillRect(prism::Rect{ux + uw - th, uy, th, uh - uw * 0.5F}, white);
    const prism::Vec2 bowl{ux + uw * 0.5F, uy + uh - uw * 0.5F};
    c.fillCircle(bowl, uw * 0.5F, white);
    c.fillCircle(bowl, uw * 0.5F - th, prism::Color(32, 112, 228));
    c.fillRect(prism::Rect{ux + th, uy, uw - 2.0F * th, uh - uw * 0.5F}, prism::Color(32, 112, 228));
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

bool DocumentTabUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && onClick) {
        onClick();
        return true;
    }
    return false;
}

prism::Vec2 DocumentTabUVE::measure(prism::Vec2) {
    const float w = std::max(170.0F, std::ceil(prism::theme().ui->width(m_name)) + 60.0F);
    return m_desired = constrain(prism::Vec2(w, kTabRowUVE));
}

void DocumentTabUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    const prism::Rect tab{r.x + 6.0F, r.y + 2.0F, r.w - 6.0F, r.h - 2.0F};
    c.fillRoundRect(tab, 5.0F, t.panel);
    c.fillRect(prism::Rect{tab.x, tab.bottom() - 5.0F, tab.w, 5.0F}, t.panel);
    c.fillRect(prism::Rect{tab.x, tab.y, tab.w, 2.0F}, t.accent);
    const prism::Rect iconBox{tab.x + 10.0F, std::floor(tab.y + (tab.h - 16.0F) * 0.5F), 16.0F, 16.0F};
    if (m_icon != nullptr && m_iconSize > 0) {
        c.blitBlend(m_icon, m_iconSize, m_iconSize, m_iconSize, iconBox);
    } else {
        prism::drawIcon(c, m_fallback, iconBox, t.accentHover);
    }
    c.textIn(*t.ui, prism::Rect{tab.x + 32.0F, tab.y, tab.w - 52.0F, tab.h}, m_name, t.textBright, prism::HAlign::Left,
             true);
    if (m_dirty) {
        // Unsaved changes: the dot at the tab's end, as editors mark a modified document.
        c.fillCircle(prism::Vec2{tab.right() - 12.0F, tab.y + tab.h * 0.5F}, 3.5F, t.textBright);
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

// -------------------------------------------------------------------------------- window buttons

void AddWindowButtonsUVE(prism::Box& row, std::function<void()> onClose) {
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
    row.add(maximize, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row.add(close, prism::Slot{0.0F, prism::Margin(0.0F, 0.0F, 4.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
}

} // namespace UVE::Editor
