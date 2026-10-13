// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_progress_bar_component_uve.h"

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

} // namespace UVE::Scene
