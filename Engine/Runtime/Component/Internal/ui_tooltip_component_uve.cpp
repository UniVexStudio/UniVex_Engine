// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_tooltip_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUITooltipComponentValidUVE(const UITooltipComponentUVE& component) noexcept {
    return component.text.size() <= kMaximumUITextBytesUVE && std::isfinite(component.delay) &&
           component.delay >= 0.0F && std::isfinite(component.offset.x) && std::isfinite(component.offset.y) &&
           std::isfinite(component.padding) && component.padding >= 0.0F &&
           component.fontSize >= kMinimumUIFontSizeUVE && component.fontSize <= kMaximumUIFontSizeUVE &&
           std::isfinite(component.backgroundColor.x) && std::isfinite(component.backgroundColor.y) &&
           std::isfinite(component.backgroundColor.z) && std::isfinite(component.textColor.x) &&
           std::isfinite(component.textColor.y) && std::isfinite(component.textColor.z) &&
           std::isfinite(component.hoverTime) && component.hoverTime >= 0.0F;
}

} // namespace UVE::Scene
