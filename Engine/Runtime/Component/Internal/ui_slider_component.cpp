// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_slider_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUISliderComponentValid(const UISliderComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUISliderSizePixels &&
           component.rect.size.x <= kMaximumUISliderSizePixels &&
           component.rect.size.y >= kMinimumUISliderSizePixels &&
           component.rect.size.y <= kMaximumUISliderSizePixels && std::isfinite(component.value) &&
           std::isfinite(component.minValue) && std::isfinite(component.maxValue) &&
           component.minValue <= component.maxValue && std::isfinite(component.step) && component.step >= 0.0F &&
           std::isfinite(component.thumbWidth) && component.thumbWidth >= 0.0F &&
           std::isfinite(component.trackColor.x) && std::isfinite(component.trackColor.y) &&
           std::isfinite(component.trackColor.z) && std::isfinite(component.fillColor.x) &&
           std::isfinite(component.fillColor.y) && std::isfinite(component.fillColor.z) &&
           std::isfinite(component.thumbColor.x) && std::isfinite(component.thumbColor.y) &&
           std::isfinite(component.thumbColor.z);
}

UISliderComponent SanitizeUISliderComponent(const UISliderComponent& component) noexcept {
    UISliderComponent out = component;
    if (!Math::IsFinite(out.rect)) {
        out.rect = Math::Rect{{0.0F, 0.0F}, {200.0F, 24.0F}};
    } else {
        out.rect.size.x = std::clamp(out.rect.size.x, kMinimumUISliderSizePixels,
                                     kMaximumUISliderSizePixels);
        out.rect.size.y = std::clamp(out.rect.size.y, kMinimumUISliderSizePixels,
                                     kMaximumUISliderSizePixels);
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
    out.value = SnapAndClampUISliderValue(out, out.value);
    return out;
}

float SnapAndClampUISliderValue(const UISliderComponent& component,
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

float ResolveUISliderNormalizedRatio(const UISliderComponent& component) noexcept {
    if (!IsUISliderComponentValid(component)) {
        return 0.0F;
    }
    const float span = component.maxValue - component.minValue;
    if (span <= 0.0F) {
        return 0.0F;
    }
    const float clamped = std::clamp(component.value, component.minValue, component.maxValue);
    return std::clamp((clamped - component.minValue) / span, 0.0F, 1.0F);
}

float ResolveUISliderValueFromPointerX(const UISliderComponent& component,
                                          const float pointerXPixels) noexcept {
    if (!IsUISliderComponentValid(component) || !std::isfinite(pointerXPixels)) {
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
    return SnapAndClampUISliderValue(component, raw);
}

void ResetUISliderRuntimeState(UISliderComponent& component) noexcept {
    component.isHovered = false;
    component.isDragging = false;
    component.wasChangedThisFrame = false;
}

} // namespace UVE::Scene
