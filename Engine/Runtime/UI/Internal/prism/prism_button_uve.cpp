// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_button_uve.h"

namespace UVE::UI::Prism {

PrismButtonUVE::PrismButtonUVE(std::string text)
    : PrismWidgetUVE("PrismButton"), m_text(std::move(text)) {
    m_isFocusable = true;

    // Apply default theme styles
    const auto& theme = PrismThemeUVE::Dark();
    m_style.backgroundColor = theme.bgWidget;
    m_style.borderColor = theme.borderSubtle;
    m_style.borderWidth = 1.0F;
    m_style.cornerRadii = theme.radiusMedium;
    m_style.padding = PrismEdgesUVE{12.0F, 6.0F};
    m_style.height = PrismLengthUVE::Px(28.0F);
}

void PrismButtonUVE::SetText(std::string text) {
    m_text = std::move(text);
}

void PrismButtonUVE::OnPointerEnter(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerEnter(event);
    const auto& theme = PrismThemeUVE::Dark();
    m_style.backgroundColor = theme.bgWidgetHover;
}

void PrismButtonUVE::OnPointerLeave(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerLeave(event);
    const auto& theme = PrismThemeUVE::Dark();
    m_style.backgroundColor = theme.bgWidget;
}

PrismEventResultUVE PrismButtonUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);
    if (event.button == Input::MouseButtonUVE::Left) {
        const auto& theme = PrismThemeUVE::Dark();
        m_style.backgroundColor = theme.bgWidgetActive;
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismButtonUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    const bool wasPressed = m_isPressed;
    PrismWidgetUVE::OnPointerUp(event);

    const auto& theme = PrismThemeUVE::Dark();
    m_style.backgroundColor = m_isHovered ? theme.bgWidgetHover : theme.bgWidget;

    if (wasPressed && m_isHovered && event.button == Input::MouseButtonUVE::Left) {
        if (m_onClick) {
            m_onClick();
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

Math::Vector2UVE PrismButtonUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    // Content width approximates text extent (approx 7px per character + padding)
    const float textW = static_cast<float>(m_text.length()) * 7.5F;
    const float totalW = textW + m_style.padding.Horizontal();
    const float totalH = 28.0F;
    return Math::Vector2UVE{totalW, totalH};
}

void PrismButtonUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    // If focused, render glow/focus ring
    if (m_isFocused) {
        const auto& theme = PrismThemeUVE::Dark();
        drawList.AddRectStroke(m_layoutRect, theme.accentFocusRing, 2.0F, m_style.cornerRadii);
    }
}

} // namespace UVE::UI::Prism
