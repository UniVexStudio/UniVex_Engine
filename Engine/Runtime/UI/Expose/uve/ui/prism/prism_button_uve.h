// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// High-performance modern button widget with hover, active, and focus transitions.
class PrismButtonUVE : public PrismWidgetUVE {
public:
    explicit PrismButtonUVE(std::string text = "Button");

    void SetText(std::string text);
    [[nodiscard]] const std::string& GetText() const noexcept { return m_text; }

    void SetOnClick(std::function<void()> onClick) { m_onClick = std::move(onClick); }

    // --- State overrides ---
    void OnPointerEnter(const PrismPointerEventUVE& event) override;
    void OnPointerLeave(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerUp(const PrismPointerEventUVE& event) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

    std::string           m_text;
    std::function<void()> m_onClick;
};

} // namespace UVE::UI::Prism
