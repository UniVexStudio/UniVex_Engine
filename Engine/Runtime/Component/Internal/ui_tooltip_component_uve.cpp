// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_tooltip_component_uve.h"

#include <algorithm>
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

bool ShouldDisplayUITooltipUVE(const UITooltipComponentUVE& component) noexcept {
    return !component.text.empty() && IsUITooltipComponentValidUVE(component) &&
           component.hoverTime >= component.delay;
}

bool StepUITooltipHoverStateUVE(UITooltipComponentUVE& component, const bool isPointerInside,
                                const float deltaSeconds) noexcept {
    if (!isPointerInside || component.text.empty() || !IsUITooltipComponentValidUVE(component)) {
        ResetUITooltipRuntimeStateUVE(component);
        return false;
    }
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) {
        component.hoverTime += deltaSeconds;
    }
    component.visibleThisFrame = component.hoverTime >= component.delay;
    return component.visibleThisFrame;
}

void ResetUITooltipRuntimeStateUVE(UITooltipComponentUVE& component) noexcept {
    component.hoverTime = 0.0F;
    component.visibleThisFrame = false;
}

Math::RectUVE ResolveUITooltipPopupRectUVE(
    const UITooltipComponentUVE& component, const Math::Vector2UVE& pointerPosition,
    const float measuredTextWidthPixels,
    const Math::Vector2UVE& viewportSizePixels) noexcept {
    if (!IsUITooltipComponentValidUVE(component) || !Math::IsFiniteUVE(pointerPosition)) {
        return Math::RectUVE{};
    }
    const float safeTextWidth =
        (std::isfinite(measuredTextWidthPixels) && measuredTextWidthPixels > 0.0F)
            ? measuredTextWidthPixels
            : 0.0F;
    const Math::Vector2UVE boxSize{
        safeTextWidth + component.padding * 2.0F,
        component.fontSize + component.padding * 2.0F,
    };
    Math::Vector2UVE boxPos = pointerPosition + component.offset;
    if (Math::IsFiniteUVE(viewportSizePixels) && viewportSizePixels.x > 0.0F &&
        viewportSizePixels.y > 0.0F) {
        const float maxX = std::max(0.0F, viewportSizePixels.x - boxSize.x);
        const float maxY = std::max(0.0F, viewportSizePixels.y - boxSize.y);
        boxPos.x = std::clamp(boxPos.x, 0.0F, maxX);
        boxPos.y = std::clamp(boxPos.y, 0.0F, maxY);
    }
    return Math::RectUVE{
        .position = boxPos,
        .size = boxSize,
    };
}

} // namespace UVE::Scene
