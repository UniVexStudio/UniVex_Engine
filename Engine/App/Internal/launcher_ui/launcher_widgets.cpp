// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_widgets.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Icons.h"
#include "prism/Theme.h"

namespace UVE::LauncherUI {
namespace {

inline constexpr float kNavItemHeightUVE = 40.0F;
inline constexpr float kNavItemIndentUVE = 16.0F;
inline constexpr float kNavIconSizeUVE = 16.0F;
inline constexpr float kNavIconGapUVE = 12.0F;
inline constexpr float kNavAccentWidthUVE = 3.0F;
inline constexpr float kNavRadiusUVE = 6.0F;
inline constexpr float kNavMinimumWidthUVE = 120.0F;
inline constexpr float kCompactNavHeightUVE = 38.0F;
inline constexpr float kCompactNavMinWidthUVE = 64.0F;
inline constexpr float kCompactNavIconSizeUVE = 14.0F;
inline constexpr float kCompactNavIconGapUVE = 5.0F;
inline constexpr float kCompactNavAccentHeightUVE = 2.5F;


} // namespace

NavItemUVE::NavItemUVE(std::string label, const prism::Icon icon) : m_label(std::move(label)), m_icon(icon) {}

void NavItemUVE::SetActiveUVE(const bool active) {
    if (m_active != active) {
        m_active = active;
        invalidate();
    }
}

void NavItemUVE::SetCompactUVE(const bool compact) {
    if (m_compact != compact) {
        m_compact = compact;
        relayout();
    }
}

prism::Vec2 NavItemUVE::measure(const prism::Vec2 /*available*/) {
    if (m_compact) {
        return m_desired = constrain({kCompactNavMinWidthUVE, kCompactNavHeightUVE});
    }
    return m_desired = constrain({kNavMinimumWidthUVE, kNavItemHeightUVE});
}

void NavItemUVE::paint(prism::Canvas& canvas) {
    const prism::Theme& theme = prism::theme();
    prism::Color background{0, 0, 0, 0};
    if (m_active) {
        background = theme.rowSelected;
    } else if (m_pressed) {
        background = theme.controlPress;
    } else if (hovered()) {
        background = theme.rowHover;
    }
    if (background.a != 0) {
        canvas.fillRoundRect(m_rect, kNavRadiusUVE, background);
    }
    const prism::Color foreground = m_active ? theme.textBright : (hovered() ? theme.text : theme.textDim);
    if (m_compact) {
        if (m_active) {
            canvas.fillRect({m_rect.x + kNavRadiusUVE, m_rect.y + m_rect.h - kCompactNavAccentHeightUVE,
                             std::max(0.0F, m_rect.w - (2.0F * kNavRadiusUVE)), kCompactNavAccentHeightUVE},
                            theme.highlight);
        }
        const prism::Font& font = *theme.small;
        const float textWidth = font.width(m_label);
        const float totalWidth = kCompactNavIconSizeUVE + kCompactNavIconGapUVE + textWidth;
        const float startX = std::floor(m_rect.x + std::max(4.0F, (m_rect.w - totalWidth) * 0.5F));
        const float iconY = std::floor(m_rect.y + ((m_rect.h - kCompactNavIconSizeUVE) * 0.5F));
        prism::drawIcon(canvas, m_icon, {startX, iconY, kCompactNavIconSizeUVE, kCompactNavIconSizeUVE},
                        m_active ? theme.textBright : foreground);
        const float textY = std::floor(m_rect.y + ((m_rect.h - font.lineHeight()) * 0.5F));
        canvas.text(font, {startX + kCompactNavIconSizeUVE + kCompactNavIconGapUVE, textY}, m_label, foreground);
    } else {
        if (m_active) {
            canvas.fillRect({m_rect.x, m_rect.y + kNavRadiusUVE, kNavAccentWidthUVE, m_rect.h - (2.0F * kNavRadiusUVE)},
                            theme.highlight);
        }
        const float iconX = m_rect.x + kNavItemIndentUVE;
        const float iconY = std::floor(m_rect.y + ((m_rect.h - kNavIconSizeUVE) * 0.5F));
        prism::drawIcon(canvas, m_icon, {iconX, iconY, kNavIconSizeUVE, kNavIconSizeUVE},
                        m_active ? theme.textBright : foreground);
        const prism::Font& font = *theme.ui;
        const float textY = std::floor(m_rect.y + ((m_rect.h - font.lineHeight()) * 0.5F));
        canvas.text(font, {iconX + kNavIconSizeUVE + kNavIconGapUVE, textY}, m_label, foreground);
    }
    if (focused()) {
        canvas.strokeRoundRect(m_rect, kNavRadiusUVE, theme.highlight);
    }
}

bool NavItemUVE::onMouseDown(const prism::MouseEvent& event) {
    if (event.button != prism::MouseButton::Left) {
        return false;
    }
    m_pressed = true;
    invalidate();
    return true;
}

bool NavItemUVE::onMouseUp(const prism::MouseEvent& event) {
    const bool activate = m_pressed && m_rect.contains(event.pos);
    m_pressed = false;
    invalidate();
    if (activate && onActivate) {
        onActivate();
    }
    return true;
}

bool NavItemUVE::onKeyDown(const prism::KeyEvent& event) {
    if (event.key == prism::Key::Enter || event.key == prism::Key::Space) {
        return accessActivate();
    }
    return false;
}

bool NavItemUVE::accessActivate() {
    if (onActivate) {
        onActivate();
    }
    return true;
}

prism::Vec2 BrandMarkUVE::measure(const prism::Vec2 /*available*/) {
    return m_desired = constrain({m_size, m_size});
}

void BrandMarkUVE::paint(prism::Canvas& canvas) {
    const prism::Rect box{m_rect.x, m_rect.y, m_size, m_size};
    if (prism::hasBrandMark()) {
        prism::drawBrandMark(canvas, box);
    } else {
        canvas.fillRoundRect(box, m_size * 0.5F, prism::theme().accent);
    }
}

} // namespace UVE::LauncherUI
