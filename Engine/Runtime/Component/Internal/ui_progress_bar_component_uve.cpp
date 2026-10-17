// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_progress_bar_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIProgressBarComponentValidUVE(const UIProgressBarComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIProgressBarSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIProgressBarSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIProgressBarSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIProgressBarSizePixelsUVE && std::isfinite(component.value) &&
           std::isfinite(component.minValue) && std::isfinite(component.maxValue) &&
           component.minValue <= component.maxValue && std::isfinite(component.backgroundColor.x) &&
           std::isfinite(component.backgroundColor.y) && std::isfinite(component.backgroundColor.z) &&
           std::isfinite(component.fillColor.x) && std::isfinite(component.fillColor.y) &&
           std::isfinite(component.fillColor.z);
}

UIProgressBarComponentUVE SanitizeUIProgressBarComponentUVE(
    const UIProgressBarComponentUVE& component) noexcept {
    UIProgressBarComponentUVE out = component;
    if (!Math::IsFiniteUVE(out.rect)) {
        out.rect = Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}};
    } else {
        out.rect.size.x = std::clamp(out.rect.size.x, kMinimumUIProgressBarSizePixelsUVE,
                                     kMaximumUIProgressBarSizePixelsUVE);
        out.rect.size.y = std::clamp(out.rect.size.y, kMinimumUIProgressBarSizePixelsUVE,
                                     kMaximumUIProgressBarSizePixelsUVE);
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

float ResolveUIProgressRatioUVE(const UIProgressBarComponentUVE& component) noexcept {
    if (!IsUIProgressBarComponentValidUVE(component)) {
        return 0.0F;
    }
    const float span = component.maxValue - component.minValue;
    if (span <= 0.0F) {
        return component.value >= component.maxValue ? 1.0F : 0.0F;
    }
    return std::clamp((component.value - component.minValue) / span, 0.0F, 1.0F);
}

Math::RectUVE ResolveUIProgressFillRectUVE(const UIProgressBarComponentUVE& component) noexcept {
    if (!IsUIProgressBarComponentValidUVE(component)) {
        return Math::RectUVE{};
    }
    const float ratio = ResolveUIProgressRatioUVE(component);
    return Math::RectUVE{
        .position = component.rect.position,
        .size = Math::Vector2UVE{component.rect.size.x * ratio, component.rect.size.y},
    };
}

} // namespace UVE::Scene
