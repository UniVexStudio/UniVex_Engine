// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_slider_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUISliderComponentValidUVE(const UISliderComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUISliderSizePixelsUVE &&
           component.rect.size.x <= kMaximumUISliderSizePixelsUVE &&
           component.rect.size.y >= kMinimumUISliderSizePixelsUVE &&
           component.rect.size.y <= kMaximumUISliderSizePixelsUVE && std::isfinite(component.value) &&
           std::isfinite(component.minValue) && std::isfinite(component.maxValue) &&
           component.minValue <= component.maxValue && std::isfinite(component.step) && component.step >= 0.0F &&
           std::isfinite(component.thumbWidth) && component.thumbWidth >= 0.0F &&
           std::isfinite(component.trackColor.x) && std::isfinite(component.trackColor.y) &&
           std::isfinite(component.trackColor.z) && std::isfinite(component.fillColor.x) &&
           std::isfinite(component.fillColor.y) && std::isfinite(component.fillColor.z) &&
           std::isfinite(component.thumbColor.x) && std::isfinite(component.thumbColor.y) &&
           std::isfinite(component.thumbColor.z);
}

UISliderComponentUVE SanitizeUISliderComponentUVE(const UISliderComponentUVE& component) noexcept {
    UISliderComponentUVE out = component;
    if (!Math::IsFiniteUVE(out.rect)) {
        out.rect = Math::RectUVE{{0.0F, 0.0F}, {200.0F, 24.0F}};
    } else {
        out.rect.size.x = std::clamp(out.rect.size.x, kMinimumUISliderSizePixelsUVE,
                                     kMaximumUISliderSizePixelsUVE);
        out.rect.size.y = std::clamp(out.rect.size.y, kMinimumUISliderSizePixelsUVE,
                                     kMaximumUISliderSizePixelsUVE);
    }
    if (!std::isfinite(out.minValue)) {
        out.minValue = 0.0F;
    }
    if (!std::isfinite(out.maxValue) || out.maxValue < out.minValue) {
        out.maxValue = out.minValue + 1.0F;
    }
    out.step = (std::isfinite(out.step) && out.step >= 0.0F) ? out.step : 0.0F;
    out.thumbWidth =
        (std::isfinite(out.thumbWidth) && out.thumbWidth >= 0.0F) ? out.thumbWidth : 12.0F;
    out.value = SnapAndClampUISliderValueUVE(out, out.value);
    return out;
}

float SnapAndClampUISliderValueUVE(const UISliderComponentUVE& component,
                                   const float rawValue) noexcept {
    if (!std::isfinite(component.minValue) || !std::isfinite(component.maxValue) ||
        component.maxValue < component.minValue) {
        return 0.0F;
    }
    float v =
        std::isfinite(rawValue) ? std::clamp(rawValue, component.minValue, component.maxValue)
                                : component.minValue;
    if (std::isfinite(component.step) && component.step > 0.0F) {
        const float steps = std::round((v - component.minValue) / component.step);
        v = std::clamp(component.minValue + steps * component.step, component.minValue,
                       component.maxValue);
    }
    return v;
}

float ResolveUISliderNormalizedRatioUVE(const UISliderComponentUVE& component) noexcept {
    if (!IsUISliderComponentValidUVE(component)) {
        return 0.0F;
    }
    const float span = component.maxValue - component.minValue;
    if (span <= 0.0F) {
        return 0.0F;
    }
    const float clamped = std::clamp(component.value, component.minValue, component.maxValue);
    return std::clamp((clamped - component.minValue) / span, 0.0F, 1.0F);
}

float ResolveUISliderValueFromPointerXUVE(const UISliderComponentUVE& component,
                                          const float pointerXPixels) noexcept {
    if (!IsUISliderComponentValidUVE(component) || !std::isfinite(pointerXPixels)) {
        return component.minValue;
    }
    const float thumbW = std::min(component.rect.size.x, std::max(0.0F, component.thumbWidth));
    const float travel = component.rect.size.x - thumbW;
    if (travel <= 1e-5F) {
        return component.minValue;
    }
    const float t = std::clamp(
        ((pointerXPixels - component.rect.position.x) - thumbW * 0.5F) / travel, 0.0F, 1.0F);
    const float raw = component.minValue + (component.maxValue - component.minValue) * t;
    return SnapAndClampUISliderValueUVE(component, raw);
}

void ResetUISliderRuntimeStateUVE(UISliderComponentUVE& component) noexcept {
    component.isHovered = false;
    component.isDragging = false;
    component.wasChangedThisFrame = false;
}

} // namespace UVE::Scene
