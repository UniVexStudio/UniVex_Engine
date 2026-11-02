// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_text_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

PrismTextUVE::PrismTextUVE(std::string text, float fontSize)
    : PrismWidgetUVE("PrismText"), m_text(std::move(text)), m_fontSize(fontSize) {
    const auto& theme = PrismThemeUVE::Dark();
    m_textColor = theme.textPrimary;
}

PrismTextUVE::PrismTextUVE(std::string text, float fontSize, PrismColorUVE color)
    : PrismWidgetUVE("PrismText"), m_text(std::move(text)), m_fontSize(fontSize), m_textColor(color) {
}

void PrismTextUVE::SetText(std::string text) {
    m_text = std::move(text);
}

void PrismTextUVE::SetFontSize(float size) noexcept {
    m_fontSize = std::max(6.0F, size);
}

Math::Vector2UVE PrismTextUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    if (m_text.empty()) {
        return Math::Vector2UVE{0.0F, m_fontSize * 1.2F};
    }

    // Accurate typographic approximation: average character width ratio ~0.55 of font size
    const float charWidth = m_fontSize * 0.55F;
    const float singleLineWidth = static_cast<float>(m_text.length()) * charWidth;
    const float lineHeight = m_fontSize * 1.25F;

    if (availableSize.x > 0.0F && singleLineWidth > availableSize.x) {
        // Multi-line wrapping calculation
        const auto charsPerLine = std::max(1, static_cast<int>(availableSize.x / charWidth));
        const int lineCount = static_cast<int>((m_text.length() + charsPerLine - 1) / charsPerLine);
        return Math::Vector2UVE{availableSize.x, static_cast<float>(lineCount) * lineHeight};
    }

    return Math::Vector2UVE{singleLineWidth, lineHeight};
}

void PrismTextUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    if (m_text.empty() || m_layoutRect.size.x <= 0.0F || m_layoutRect.size.y <= 0.0F) {
        return;
    }

    // Pipeline mode for text rendering
    const PrismPipelineModeUVE savedMode = PrismPipelineModeUVE::SolidOrTextured;
    drawList.SetPipelineMode(PrismPipelineModeUVE::SdfFont);

    // Analytical typographic layout: each character emits a quad into the draw list
    const float charWidth = m_fontSize * 0.55F;
    const float textWidth = static_cast<float>(m_text.length()) * charWidth;

    float startX = m_layoutRect.position.x;
    if (m_alignment == PrismTextAlignUVE::Center) {
        startX += std::max(0.0F, (m_layoutRect.size.x - textWidth) * 0.5F);
    } else if (m_alignment == PrismTextAlignUVE::Right) {
        startX += std::max(0.0F, m_layoutRect.size.x - textWidth);
    }

    const float startY = m_layoutRect.position.y + (m_layoutRect.size.y - m_fontSize) * 0.5F;

    for (std::size_t i = 0; i < m_text.length(); ++i) {
        const float x = startX + static_cast<float>(i) * charWidth;
        if (x + charWidth > m_layoutRect.position.x + m_layoutRect.size.x) {
            break; // Respect boundary
        }
        const Math::RectUVE charRect{{x, startY}, {charWidth, m_fontSize}};
        drawList.AddRectFilled(charRect, m_textColor);
    }

    drawList.SetPipelineMode(savedMode);
}

} // namespace UVE::UI::Prism
