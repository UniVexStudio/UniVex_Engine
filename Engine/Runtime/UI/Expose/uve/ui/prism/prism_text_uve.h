// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include "uve/math/color_uve.h"
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Text alignment within the widget layout bounds.
enum class PrismTextAlignUVE : std::uint8_t {
    Left,
    Center,
    Right,
};

/// High-performance typography and label widget with wrapping and alignment.
class PrismTextUVE : public PrismWidgetUVE {
public:
    explicit PrismTextUVE(std::string text = "", float fontSize = 14.0F);
    PrismTextUVE(std::string text, float fontSize, PrismColorUVE color);

    void SetText(std::string text);
    [[nodiscard]] const std::string& GetText() const noexcept { return m_text; }

    void SetFontSize(float size) noexcept;
    [[nodiscard]] float GetFontSize() const noexcept { return m_fontSize; }

    void SetTextColor(PrismColorUVE color) noexcept { m_textColor = color; }
    [[nodiscard]] PrismColorUVE GetTextColor() const noexcept { return m_textColor; }

    void SetAlignment(PrismTextAlignUVE align) noexcept { m_alignment = align; }
    [[nodiscard]] PrismTextAlignUVE GetAlignment() const noexcept { return m_alignment; }

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

    std::string       m_text;
    float             m_fontSize = 14.0F;
    PrismColorUVE    m_textColor{0.95F, 0.96F, 0.98F, 1.0F};
    PrismTextAlignUVE m_alignment = PrismTextAlignUVE::Left;
};

} // namespace UVE::UI::Prism
