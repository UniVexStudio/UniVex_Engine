// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_numeric_drag_uve.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace UVE::UI::Prism {

namespace {
constexpr float kBadgeWidthUVE = 18.0F;
} // namespace

PrismNumericDragUVE::PrismNumericDragUVE(
    std::string badge,
    float initialValue,
    float speed,
    PrismColorUVE badgeColor
)
    : PrismWidgetUVE("PrismNumericDrag"),
      m_value(initialValue),
      m_speed(speed),
      m_badge(std::move(badge)),
      m_badgeColor(badgeColor) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{2.0F, 2.0F};
    m_style.height = PrismLengthUVE::Px(24.0F);
}

void PrismNumericDragUVE::SetValue(float value) noexcept {
    const float clamped = std::clamp(value, m_min, m_max);
    if (std::abs(m_value - clamped) > 0.00001F) {
        m_value = clamped;
        if (m_onValueChanged) {
            m_onValueChanged(m_value);
        }
    }
}

void PrismNumericDragUVE::SetRange(float minVal, float maxVal) noexcept {
    m_min = minVal;
    m_max = std::max(minVal, maxVal);
    SetValue(m_value);
}

std::string PrismNumericDragUVE::FormatValue() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", m_precision, m_value);
    return std::string(buf);
}

void PrismNumericDragUVE::CommitTextEdit() {
    if (!m_isEditingText) return;
    m_isEditingText = false;

    if (!m_textBuffer.empty()) {
        char* endPtr = nullptr;
        const float parsed = std::strtof(m_textBuffer.c_str(), &endPtr);
        if (endPtr != m_textBuffer.c_str()) {
            SetValue(parsed);
        }
    }
    m_textBuffer.clear();
}

void PrismNumericDragUVE::CancelTextEdit() {
    m_isEditingText = false;
    m_textBuffer.clear();
}

void PrismNumericDragUVE::OnFocusGained() {
    PrismWidgetUVE::OnFocusGained();
}

void PrismNumericDragUVE::OnFocusLost() {
    CommitTextEdit();
    PrismWidgetUVE::OnFocusLost();
}

PrismEventResultUVE PrismNumericDragUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);

    if (event.button == Input::MouseButtonUVE::Left) {
        if (!m_isEditingText) {
            m_isDragging = true;
            m_dragStartX = event.position.x;
            m_dragStartValue = m_value;
            return PrismEventResultUVE::Handled;
        }
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismNumericDragUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    if (m_isDragging) {
        const float deltaX = event.position.x - m_dragStartX;
        if (std::abs(deltaX) > 2.0F) {
            SetValue(m_dragStartValue + deltaX * m_speed);
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismNumericDragUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left && m_isDragging) {
        const float deltaX = std::abs(event.position.x - m_dragStartX);
        m_isDragging = false;

        // If the click did not drag more than 2 pixels, enter direct text typing mode
        if (deltaX <= 2.0F) {
            m_isEditingText = true;
            m_textBuffer = FormatValue();
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerUp(event);
}

PrismEventResultUVE PrismNumericDragUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (m_isEditingText) {
        if (event.keyCode == Input::KeyCodeUVE::Enter) {
            CommitTextEdit();
            return PrismEventResultUVE::Handled;
        }
        if (event.keyCode == Input::KeyCodeUVE::Escape) {
            CancelTextEdit();
            return PrismEventResultUVE::Handled;
        }
        if (event.keyCode == Input::KeyCodeUVE::Backspace) {
            if (!m_textBuffer.empty()) {
                m_textBuffer.pop_back();
            }
            return PrismEventResultUVE::Handled;
        }
        return PrismEventResultUVE::Handled;
    }

    if (event.keyCode == Input::KeyCodeUVE::Enter) {
        m_isEditingText = true;
        m_textBuffer = FormatValue();
        return PrismEventResultUVE::Handled;
    }

    return PrismWidgetUVE::OnKeyDown(event);
}

PrismEventResultUVE PrismNumericDragUVE::OnCharInput(char32_t codepoint) {
    if (m_isEditingText) {
        const char32_t c = codepoint;
        if ((c >= U'0' && c <= U'9') || c == U'.' || c == U'-' || c == U'+') {
            m_textBuffer.push_back(static_cast<char>(c));
            return PrismEventResultUVE::Handled;
        }
    }
    return PrismWidgetUVE::OnCharInput(codepoint);
}

Math::Vector2UVE PrismNumericDragUVE::OnMeasureContentUVE(Math::Vector2UVE /*availableSize*/) {
    return Math::Vector2UVE{85.0F, 24.0F};
}

void PrismNumericDragUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();

    // 1. Container background card
    const PrismColorUVE bg = m_isDragging ? theme.bgWidgetActive : (m_isHovered ? theme.bgWidgetHover : theme.bgWidget);
    drawList.AddRectFilled(m_layoutRect, bg, theme.radiusSmall);

    // Border or focus ring
    const PrismColorUVE border = (m_isFocused || m_isEditingText) ? theme.accentFocusRing : theme.borderSubtle;
    drawList.AddRectStroke(m_layoutRect, border, 1.0F, theme.radiusSmall);

    // 2. Left colored badge
    const float badgeH = m_layoutRect.size.y - 4.0F;
    const Math::RectUVE badgeRect{
        {m_layoutRect.position.x + 2.0F, m_layoutRect.position.y + 2.0F},
        {kBadgeWidthUVE, badgeH}
    };
    drawList.AddRectFilled(badgeRect, m_badgeColor, PrismCornerRadiiUVE{3.0F});

    // Badge text letter
    if (!m_badge.empty()) {
        const float badgeCharX = badgeRect.position.x + (badgeRect.size.x - 7.5F) * 0.5F;
        const float badgeCharY = badgeRect.position.y + (badgeRect.size.y - 12.0F) * 0.5F;
        drawList.AddRectFilled(
            Math::RectUVE{{badgeCharX, badgeCharY}, {7.5F, 12.0F}},
            PrismColorUVE{1.0F, 1.0F, 1.0F, 1.0F}
        );
    }

    // 3. Numeric value text / edit buffer
    const std::string textToShow = m_isEditingText ? m_textBuffer : FormatValue();
    const float textStartX = badgeRect.position.x + badgeRect.size.x + 6.0F;
    const float textY = m_layoutRect.position.y + (m_layoutRect.size.y - 12.0F) * 0.5F;
    const float charW = 7.5F;

    for (std::size_t i = 0; i < textToShow.length(); ++i) {
        const float x = textStartX + static_cast<float>(i) * charW;
        drawList.AddRectFilled(Math::RectUVE{{x, textY}, {charW, 12.0F}}, theme.textPrimary);
    }

    // Caret when editing text
    if (m_isEditingText) {
        const float caretX = textStartX + static_cast<float>(textToShow.length()) * charW + 1.0F;
        drawList.AddRectFilled(
            Math::RectUVE{{caretX, textY}, {1.5F, 12.0F}},
            theme.accentPrimary
        );
    }
}

} // namespace UVE::UI::Prism
