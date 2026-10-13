// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_slider_component_uve.h"

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

} // namespace UVE::Scene
