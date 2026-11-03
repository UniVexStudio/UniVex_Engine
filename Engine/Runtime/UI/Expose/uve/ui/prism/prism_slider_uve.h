// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Smooth interactive range slider with track, fill, and thumb.
class PrismSliderUVE : public PrismWidgetUVE {
public:
    explicit PrismSliderUVE(
        float minVal = 0.0F,
        float maxVal = 1.0F,
        float initialVal = 0.0F,
        std::string label = ""
    );

    void SetValue(float value) noexcept;
    [[nodiscard]] float GetValue() const noexcept { return m_value; }

    void SetRange(float minVal, float maxVal) noexcept;
    [[nodiscard]] float GetMin() const noexcept { return m_min; }
    [[nodiscard]] float GetMax() const noexcept { return m_max; }

    void SetStep(float step) noexcept { m_step = step; }
    [[nodiscard]] float GetStep() const noexcept { return m_step; }

    void SetLabel(std::string label) { m_label = std::move(label); }
    [[nodiscard]] const std::string& GetLabel() const noexcept { return m_label; }

    void SetOnValueChanged(std::function<void(float)> callback) {
        m_onValueChanged = std::move(callback);
    }

    // --- State overrides ---
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerUp(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    [[nodiscard]] Math::RectUVE GetTrackRect() const noexcept;
    void UpdateValueFromPointerX(float pointerX);

    float m_min = 0.0F;
    float m_max = 1.0F;
    float m_value = 0.0F;
    float m_step = 0.01F;

    bool  m_isDragging = false;
    std::string m_label;
    std::function<void(float)> m_onValueChanged;
};

} // namespace UVE::UI::Prism
