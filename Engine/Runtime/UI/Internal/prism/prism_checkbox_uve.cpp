// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_checkbox_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

namespace {
constexpr float kBoxSizeUVE = 16.0F;
} // namespace

PrismCheckboxUVE::PrismCheckboxUVE(std::string label, bool initialChecked)
    : PrismWidgetUVE("PrismCheckbox"), m_checked(initialChecked), m_label(std::move(label)) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{4.0F, 2.0F};
    m_style.height = PrismLengthUVE::Px(22.0F);
}

void PrismCheckboxUVE::SetChecked(bool checked) noexcept {
    if (m_checked != checked) {
        m_checked = checked;
        if (m_onToggled) {
            m_onToggled(m_checked);
        }
    }
}

void PrismCheckboxUVE::SetLabel(std::string label) {
    m_label = std::move(label);
}

PrismEventResultUVE PrismCheckboxUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);
    if (event.button == Input::MouseButtonUVE::Left) {
        SetChecked(!m_checked);
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismCheckboxUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (event.keyCode == Input::KeyCodeUVE::Space || event.keyCode == Input::KeyCodeUVE::Enter) {
        SetChecked(!m_checked);
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnKeyDown(event);
}

Math::Vector2UVE PrismCheckboxUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    const float labelWidth = static_cast<float>(m_label.length()) * 7.5F;
    const float totalW = kBoxSizeUVE + 8.0F + labelWidth + m_style.padding.Horizontal();
    return Math::Vector2UVE{totalW, 22.0F};
}

void PrismCheckboxUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();
    const float boxX = m_layoutRect.position.x + m_style.padding.left;
    const float boxY = m_layoutRect.position.y + (m_layoutRect.size.y - kBoxSizeUVE) * 0.5F;
    const Math::RectUVE boxRect{{boxX, boxY}, {kBoxSizeUVE, kBoxSizeUVE}};

    // Checkbox square background & border
    const PrismColorUVE boxBg = m_checked ? theme.accentPrimary : theme.bgApp;
    const PrismColorUVE boxBorder = m_isFocused ? theme.accentFocusRing : theme.borderProminent;
    drawList.AddRectFilled(boxRect, boxBg, theme.radiusSmall);
    drawList.AddRectStroke(boxRect, boxBorder, 1.0F, theme.radiusSmall);

    // Inner checkmark glyph
    if (m_checked) {
        const Math::RectUVE markRect{{boxX + 4.0F, boxY + 4.0F}, {kBoxSizeUVE - 8.0F, kBoxSizeUVE - 8.0F}};
        drawList.AddRectFilled(markRect, PrismColorUVE{1.0F, 1.0F, 1.0F, 1.0F}, PrismCornerRadiiUVE{2.0F});
    }

    // Label text
    const float labelX = boxX + kBoxSizeUVE + 8.0F;
    const float labelY = m_layoutRect.position.y + (m_layoutRect.size.y - 12.0F) * 0.5F;
    const float charW = 7.5F;
    for (std::size_t i = 0; i < m_label.length(); ++i) {
        const float x = labelX + static_cast<float>(i) * charW;
        drawList.AddRectFilled(Math::RectUVE{{x, labelY}, {charW, 12.0F}}, theme.textPrimary);
    }
}

} // namespace UVE::UI::Prism
