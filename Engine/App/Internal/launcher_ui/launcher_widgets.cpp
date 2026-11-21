// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "launcher_widgets.h"

#include <cmath>
#include <utility>

#include "prism/Canvas.h"
#include "prism/Font.h"
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

// The mark's proportions, as fractions of its size (matching Engine/Distribution/univex-hub.svg).
inline constexpr float kLogoCornerUVE = 0.21F;
inline constexpr float kLogoInsetXUVE = 0.30F;
inline constexpr float kLogoTopUVE = 0.24F;
inline constexpr float kLogoBottomUVE = 0.80F;
inline constexpr float kLogoStrokeUVE = 0.12F;

} // namespace

NavItemUVE::NavItemUVE(std::string label, const prism::Icon icon) : m_label(std::move(label)), m_icon(icon) {}

void NavItemUVE::SetActiveUVE(const bool active) {
    if (m_active != active) {
        m_active = active;
        invalidate();
    }
}

prism::Vec2 NavItemUVE::measure(const prism::Vec2 /*available*/) {
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
    if (m_active) {
        canvas.fillRect({m_rect.x, m_rect.y + kNavRadiusUVE, kNavAccentWidthUVE, m_rect.h - (2.0F * kNavRadiusUVE)},
                        theme.accent);
    }
    const prism::Color foreground = m_active ? theme.textBright : (hovered() ? theme.text : theme.textDim);
    const float iconX = m_rect.x + kNavItemIndentUVE;
    const float iconY = std::floor(m_rect.y + ((m_rect.h - kNavIconSizeUVE) * 0.5F));
    prism::drawIcon(canvas, m_icon, {iconX, iconY, kNavIconSizeUVE, kNavIconSizeUVE},
                    m_active ? theme.accent : foreground);
    const prism::Font& font = *theme.ui;
    const float textY = std::floor(m_rect.y + ((m_rect.h - font.lineHeight()) * 0.5F));
    canvas.text(font, {iconX + kNavIconSizeUVE + kNavIconGapUVE, textY}, m_label, foreground);
    if (focused()) {
        canvas.strokeRoundRect(m_rect, kNavRadiusUVE, theme.accent);
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

prism::Vec2 LogoMarkUVE::measure(const prism::Vec2 /*available*/) {
    return m_desired = constrain({m_size, m_size});
}

void LogoMarkUVE::paint(prism::Canvas& canvas) {
    const prism::Theme& theme = prism::theme();
    const prism::Rect box{m_rect.x, m_rect.y, m_size, m_size};
    canvas.fillRoundRect(box, m_size * kLogoCornerUVE, theme.accent);
    // The U: a white rounded slab, its inside cut back out in the accent colour, open at the top.
    const float left = box.x + (m_size * kLogoInsetXUVE);
    const float width = m_size * (1.0F - (2.0F * kLogoInsetXUVE));
    const float top = box.y + (m_size * kLogoTopUVE);
    const float height = m_size * (kLogoBottomUVE - kLogoTopUVE);
    const float stroke = m_size * kLogoStrokeUVE;
    const prism::Color white = theme.accentText;
    canvas.fillRoundRect({left, top, width, height}, width * 0.5F, white);
    canvas.fillRoundRect({left + stroke, top, width - (2.0F * stroke), height - stroke}, (width * 0.5F) - stroke,
                         theme.accent);
    canvas.fillRect({left, top, width, height * 0.5F}, theme.accent);
    canvas.fillRect({left, top, stroke, height * 0.5F}, white);
    canvas.fillRect({left + width - stroke, top, stroke, height * 0.5F}, white);
}

} // namespace UVE::LauncherUI
