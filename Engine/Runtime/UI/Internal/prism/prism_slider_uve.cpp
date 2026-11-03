// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_slider_uve.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UVE::UI::Prism {

namespace {
constexpr float kTrackHeightUVE = 4.0F;
constexpr float kThumbRadiusUVE = 7.0F;
} // namespace

PrismSliderUVE::PrismSliderUVE(float minVal, float maxVal, float initialVal, std::string label)
    : PrismWidgetUVE("PrismSlider"),
      m_min(minVal),
      m_max(maxVal),
      m_value(std::clamp(initialVal, minVal, maxVal)),
      m_label(std::move(label)) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{4.0F, 2.0F};
    m_style.height = PrismLengthUVE::Px(24.0F);
}

void PrismSliderUVE::SetValue(float value) noexcept {
    const float clamped = std::clamp(value, m_min, m_max);
    if (std::abs(m_value - clamped) > 0.00001F) {
        m_value = clamped;
        if (m_onValueChanged) {
            m_onValueChanged(m_value);
        }
    }
}

void PrismSliderUVE::SetRange(float minVal, float maxVal) noexcept {
    m_min = minVal;
    m_max = std::max(minVal, maxVal);
    SetValue(m_value);
}

Math::RectUVE PrismSliderUVE::GetTrackRect() const noexcept {
    const float labelOffset = m_label.empty() ? 0.0F : (static_cast<float>(m_label.length()) * 7.5F + 10.0F);
    const float startX = m_layoutRect.position.x + m_style.padding.left + labelOffset + kThumbRadiusUVE;
    const float trackW = std::max(10.0F, m_layoutRect.size.x - m_style.padding.Horizontal() - labelOffset - kThumbRadiusUVE * 2.0F);
    const float trackY = m_layoutRect.position.y + (m_layoutRect.size.y - kTrackHeightUVE) * 0.5F;
    return Math::RectUVE{{startX, trackY}, {trackW, kTrackHeightUVE}};
}

void PrismSliderUVE::UpdateValueFromPointerX(float pointerX) {
    const Math::RectUVE track = GetTrackRect();
    if (track.size.x <= 0.0F || m_max <= m_min) return;

    const float fraction = std::clamp((pointerX - track.position.x) / track.size.x, 0.0F, 1.0F);
    float rawValue = m_min + fraction * (m_max - m_min);

    if (m_step > 0.0F) {
        rawValue = m_min + std::round((rawValue - m_min) / m_step) * m_step;
    }
    SetValue(rawValue);
}

PrismEventResultUVE PrismSliderUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);
    if (event.button == Input::MouseButtonUVE::Left) {
        m_isDragging = true;
        UpdateValueFromPointerX(event.position.x);
        return PrismEventResultUVE::Handled;
    }
    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismSliderUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    if (m_isDragging) {
        UpdateValueFromPointerX(event.position.x);
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismSliderUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    if (event.button == Input::MouseButtonUVE::Left && m_isDragging) {
        m_isDragging = false;
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerUp(event);
}

PrismEventResultUVE PrismSliderUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    const float step = (m_step > 0.0F) ? m_step : (m_max - m_min) * 0.05F;
    if (event.keyCode == Input::KeyCodeUVE::Left || event.keyCode == Input::KeyCodeUVE::Down) {
        SetValue(m_value - step);
        return PrismEventResultUVE::Handled;
    }
    if (event.keyCode == Input::KeyCodeUVE::Right || event.keyCode == Input::KeyCodeUVE::Up) {
        SetValue(m_value + step);
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnKeyDown(event);
}

Math::Vector2UVE PrismSliderUVE::OnMeasureContentUVE(Math::Vector2UVE /*availableSize*/) {
    const float labelWidth = m_label.empty() ? 0.0F : (static_cast<float>(m_label.length()) * 7.5F + 10.0F);
    return Math::Vector2UVE{labelWidth + 120.0F + m_style.padding.Horizontal(), 24.0F};
}

void PrismSliderUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();
    const Math::RectUVE track = GetTrackRect();

    // 1. Draw Label if provided
    if (!m_label.empty()) {
        const float labelX = m_layoutRect.position.x + m_style.padding.left;
        const float labelY = m_layoutRect.position.y + (m_layoutRect.size.y - 14.0F) * 0.5F;
        drawList.AddText(Math::Vector2UVE{labelX, labelY}, m_label, theme.textSecondary, 13.0F);
    }

    // 2. Track Background
    drawList.AddRectFilled(track, theme.bgWidget, PrismCornerRadiiUVE{kTrackHeightUVE * 0.5F});

    // 3. Track Fill
    const float fraction = (m_max > m_min) ? std::clamp((m_value - m_min) / (m_max - m_min), 0.0F, 1.0F) : 0.0F;
    const Math::RectUVE fillRect{track.position, {track.size.x * fraction, track.size.y}};
    drawList.AddRectFilled(fillRect, theme.accentPrimary, PrismCornerRadiiUVE{kTrackHeightUVE * 0.5F});

    // 4. Thumb Knob
    const float thumbCenterX = track.position.x + track.size.x * fraction;
    const float thumbCenterY = track.position.y + track.size.y * 0.5F;
    const Math::RectUVE thumbRect{
        {thumbCenterX - kThumbRadiusUVE, thumbCenterY - kThumbRadiusUVE},
        {kThumbRadiusUVE * 2.0F, kThumbRadiusUVE * 2.0F}
    };

    PrismColorUVE thumbColor = m_isDragging ? theme.accentHover : (m_isHovered ? theme.textPrimary : theme.accentPrimary);
    drawList.AddRectFilled(thumbRect, thumbColor, PrismCornerRadiiUVE{kThumbRadiusUVE});

    // Focus ring if focused
    if (m_isFocused) {
        drawList.AddRectStroke(thumbRect, theme.accentFocusRing, 1.5F, PrismCornerRadiiUVE{kThumbRadiusUVE});
    }
}

} // namespace UVE::UI::Prism
