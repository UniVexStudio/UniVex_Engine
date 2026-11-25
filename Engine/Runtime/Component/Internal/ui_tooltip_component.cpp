// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_tooltip_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUITooltipComponentValid(const UITooltipComponent& component) noexcept {
    return component.text.size() <= kMaximumUITextBytes && std::isfinite(component.delay) &&
           component.delay >= 0.0F && std::isfinite(component.offset.x) && std::isfinite(component.offset.y) &&
           std::isfinite(component.padding) && component.padding >= 0.0F &&
           component.fontSize >= kMinimumUIFontSize && component.fontSize <= kMaximumUIFontSize &&
           std::isfinite(component.backgroundColor.x) && std::isfinite(component.backgroundColor.y) &&
           std::isfinite(component.backgroundColor.z) && std::isfinite(component.textColor.x) &&
           std::isfinite(component.textColor.y) && std::isfinite(component.textColor.z) &&
           std::isfinite(component.hoverTime) && component.hoverTime >= 0.0F;
}

bool ShouldDisplayUITooltip(const UITooltipComponent& component) noexcept {
    return !component.text.empty() && IsUITooltipComponentValid(component) &&
           component.hoverTime >= component.delay;
}

bool StepUITooltipHoverState(UITooltipComponent& component, const bool isPointerInside,
                                const float deltaSeconds) noexcept {
    if (!isPointerInside || component.text.empty() || !IsUITooltipComponentValid(component)) {
        ResetUITooltipRuntimeState(component);
        return false;
    }
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) {
        component.hoverTime += deltaSeconds;
    }
    component.visibleThisFrame = component.hoverTime >= component.delay;
    return component.visibleThisFrame;
}

void ResetUITooltipRuntimeState(UITooltipComponent& component) noexcept {
    component.hoverTime = 0.0F;
    component.visibleThisFrame = false;
}

Math::Rect ResolveUITooltipPopupRect(
    const UITooltipComponent& component, const Math::Vector2& pointerPosition,
    const float measuredTextWidthPixels,
    const Math::Vector2& viewportSizePixels) noexcept {
    if (!IsUITooltipComponentValid(component) || !Math::IsFinite(pointerPosition)) {
        return Math::Rect{};
    }
    const float safeTextWidth =
        (std::isfinite(measuredTextWidthPixels) && measuredTextWidthPixels > 0.0F)
            ? measuredTextWidthPixels
            : 0.0F;
    const Math::Vector2 boxSize{
        safeTextWidth + component.padding * 2.0F,
        component.fontSize + component.padding * 2.0F,
    };
    Math::Vector2 boxPos = pointerPosition + component.offset;
    if (Math::IsFinite(viewportSizePixels) && viewportSizePixels.x > 0.0F &&
        viewportSizePixels.y > 0.0F) {
        const float maxX = std::max(0.0F, viewportSizePixels.x - boxSize.x);
        const float maxY = std::max(0.0F, viewportSizePixels.y - boxSize.y);
        boxPos.x = std::clamp(boxPos.x, 0.0F, maxX);
        boxPos.y = std::clamp(boxPos.y, 0.0F, maxY);
    }
    return Math::Rect{
        .position = boxPos,
        .size = boxSize,
    };
}

} // namespace UVE::Scene
