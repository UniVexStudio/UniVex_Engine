// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_text_input_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

PrismTextInputUVE::PrismTextInputUVE(std::string placeholder)
    : PrismWidgetUVE("PrismTextInput"), m_placeholder(std::move(placeholder)) {
    m_isFocusable = true;

    const auto& theme = PrismThemeUVE::Dark();
    m_style.backgroundColor = theme.bgApp;
    m_style.borderColor = theme.borderSubtle;
    m_style.borderWidth = 1.0F;
    m_style.cornerRadii = theme.radiusSmall;
    m_style.padding = PrismEdgesUVE{8.0F, 4.0F};
    m_style.height = PrismLengthUVE::Px(28.0F);
}

void PrismTextInputUVE::SetText(std::string text) {
    m_text = std::move(text);
    m_cursorIndex = m_text.length();
}

void PrismTextInputUVE::SetPlaceholder(std::string placeholder) {
    m_placeholder = std::move(placeholder);
}

void PrismTextInputUVE::OnFocusGained() {
    PrismWidgetUVE::OnFocusGained();
    const auto& theme = PrismThemeUVE::Dark();
    m_style.borderColor = theme.accentPrimary;
    m_cursorIndex = m_text.length();
}

void PrismTextInputUVE::OnFocusLost() {
    PrismWidgetUVE::OnFocusLost();
    const auto& theme = PrismThemeUVE::Dark();
    m_style.borderColor = theme.borderSubtle;
    if (m_onCommit) {
        m_onCommit(m_text);
    }
}

PrismEventResultUVE PrismTextInputUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);
    if (event.button == Input::MouseButtonUVE::Left) {
        // Approximate cursor position from click X
        const float charWidth = 8.0F;
        const float relativeX = std::max(0.0F, event.position.x - (m_layoutRect.position.x + m_style.padding.left));
        const auto clickedIndex = static_cast<std::size_t>(relativeX / charWidth);
        m_cursorIndex = std::min(clickedIndex, m_text.length());
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismTextInputUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (event.keyCode == Input::KeyCodeUVE::Backspace) {
        if (m_cursorIndex > 0 && !m_text.empty()) {
            m_text.erase(m_cursorIndex - 1, 1);
            m_cursorIndex--;
            if (m_onTextChanged) {
                m_onTextChanged(m_text);
            }
        }
        return PrismEventResultUVE::Handled;
    } else if (event.keyCode == Input::KeyCodeUVE::Left) {
        if (m_cursorIndex > 0) {
            m_cursorIndex--;
        }
        return PrismEventResultUVE::Handled;
    } else if (event.keyCode == Input::KeyCodeUVE::Right) {
        if (m_cursorIndex < m_text.length()) {
            m_cursorIndex++;
        }
        return PrismEventResultUVE::Handled;
    } else if (event.keyCode == Input::KeyCodeUVE::Enter) {
        if (m_onCommit) {
            m_onCommit(m_text);
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismTextInputUVE::OnCharInput(char32_t codepoint) {
    // Only accept printable ASCII characters
    if (codepoint >= 32 && codepoint <= 126) {
        m_text.insert(m_cursorIndex, 1, static_cast<char>(codepoint));
        m_cursorIndex++;
        if (m_onTextChanged) {
            m_onTextChanged(m_text);
        }
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

Math::Vector2UVE PrismTextInputUVE::OnMeasureContentUVE(Math::Vector2UVE /*availableSize*/) {
    const float textW = static_cast<float>(std::max(m_text.length(), m_placeholder.length())) * 8.0F;
    const float totalW = textW + m_style.padding.Horizontal() + 16.0F;
    return Math::Vector2UVE{std::max(120.0F, totalW), 28.0F};
}

void PrismTextInputUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();
    const float contentX = m_layoutRect.position.x + m_style.padding.left;
    const float contentY = m_layoutRect.position.y + (m_layoutRect.size.y - 14.0F) * 0.5F;
    const float charWidth = 8.0F;

    if (m_text.empty() && !m_placeholder.empty()) {
        drawList.AddText(Math::Vector2UVE{contentX, contentY}, m_placeholder, theme.textMuted, 13.0F);
    } else if (!m_text.empty()) {
        drawList.AddText(Math::Vector2UVE{contentX, contentY}, m_text, theme.textPrimary, 13.0F);
    }

    if (m_isFocused) {
        const float cursorX = contentX + static_cast<float>(m_cursorIndex) * charWidth;
        const Math::RectUVE cursorRect{{cursorX, contentY - 1.0F}, {1.5F, 16.0F}};
        drawList.AddRectFilled(cursorRect, theme.accentPrimary);
    }
}

} // namespace UVE::UI::Prism
