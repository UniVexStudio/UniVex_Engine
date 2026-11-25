// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_button_component.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIButtonComponentValid(const UIButtonComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUIButtonSizePixels &&
           component.rect.size.x <= kMaximumUIButtonSizePixels &&
           component.rect.size.y >= kMinimumUIButtonSizePixels &&
           component.rect.size.y <= kMaximumUIButtonSizePixels && std::isfinite(component.normalColor.x) &&
           std::isfinite(component.normalColor.y) && std::isfinite(component.normalColor.z) &&
           std::isfinite(component.hoverColor.x) && std::isfinite(component.hoverColor.y) &&
           std::isfinite(component.hoverColor.z) && std::isfinite(component.pressedColor.x) &&
           std::isfinite(component.pressedColor.y) && std::isfinite(component.pressedColor.z);
}

Math::Vector3 ResolveUIButtonActiveColor(const UIButtonComponent& component,
                                               const bool isMouseButtonDown) noexcept {
    if (!component.isHovered) {
        return component.normalColor;
    }
    return isMouseButtonDown ? component.pressedColor : component.hoverColor;
}

bool HitTestUIButton(const UIButtonComponent& component,
                        const Math::Vector2& pointPixels) noexcept {
    return IsUIButtonComponentValid(component) && Math::IsFinite(pointPixels) &&
           Math::Contains(component.rect, pointPixels);
}

void ResetUIButtonRuntimeState(UIButtonComponent& component) noexcept {
    component.isHovered = false;
    component.wasClickedThisFrame = false;
}

bool StepUIButtonInteraction(UIButtonComponent& component,
                                const Math::Vector2& pointerPixels,
                                const bool mouseJustPressed) noexcept {
    component.isHovered = HitTestUIButton(component, pointerPixels);
    component.wasClickedThisFrame = component.isHovered && mouseJustPressed;
    return component.wasClickedThisFrame;
}

} // namespace UVE::Scene
