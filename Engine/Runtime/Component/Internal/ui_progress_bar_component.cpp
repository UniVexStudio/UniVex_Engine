// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_progress_bar_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIProgressBarComponentValid(const UIProgressBarComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUIProgressBarSizePixels &&
           component.rect.size.x <= kMaximumUIProgressBarSizePixels &&
           component.rect.size.y >= kMinimumUIProgressBarSizePixels &&
           component.rect.size.y <= kMaximumUIProgressBarSizePixels && std::isfinite(component.value) &&
           std::isfinite(component.minValue) && std::isfinite(component.maxValue) &&
           component.minValue <= component.maxValue && std::isfinite(component.backgroundColor.x) &&
           std::isfinite(component.backgroundColor.y) && std::isfinite(component.backgroundColor.z) &&
           std::isfinite(component.fillColor.x) && std::isfinite(component.fillColor.y) &&
           std::isfinite(component.fillColor.z);
}

UIProgressBarComponent SanitizeUIProgressBarComponent(
    const UIProgressBarComponent& component) noexcept {
    UIProgressBarComponent out = component;
    if (!Math::IsFinite(out.rect)) {
        out.rect = Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}};
    } else {
        out.rect.size.x = std::clamp(out.rect.size.x, kMinimumUIProgressBarSizePixels,
                                     kMaximumUIProgressBarSizePixels);
        out.rect.size.y = std::clamp(out.rect.size.y, kMinimumUIProgressBarSizePixels,
                                     kMaximumUIProgressBarSizePixels);
    }
    if (!std::isfinite(out.minValue)) {
        out.minValue = 0.0F;
    }
    if (!std::isfinite(out.maxValue) || out.maxValue < out.minValue) {
        out.maxValue = out.minValue + 1.0F;
    }
    out.value =
        std::isfinite(out.value) ? std::clamp(out.value, out.minValue, out.maxValue) : out.minValue;
    return out;
}

float ResolveUIProgressRatio(const UIProgressBarComponent& component) noexcept {
    if (!IsUIProgressBarComponentValid(component)) {
        return 0.0F;
    }
    const float span = component.maxValue - component.minValue;
    if (span <= 0.0F) {
        return component.value >= component.maxValue ? 1.0F : 0.0F;
    }
    return std::clamp((component.value - component.minValue) / span, 0.0F, 1.0F);
}

Math::Rect ResolveUIProgressFillRect(const UIProgressBarComponent& component) noexcept {
    if (!IsUIProgressBarComponentValid(component)) {
        return Math::Rect{};
    }
    const float ratio = ResolveUIProgressRatio(component);
    return Math::Rect{
        .position = component.rect.position,
        .size = Math::Vector2{component.rect.size.x * ratio, component.rect.size.y},
    };
}

} // namespace UVE::Scene
