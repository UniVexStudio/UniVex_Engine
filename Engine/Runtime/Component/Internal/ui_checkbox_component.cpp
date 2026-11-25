// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_checkbox_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUICheckboxComponentValid(const UICheckboxComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUICheckboxSizePixels &&
           component.rect.size.x <= kMaximumUICheckboxSizePixels &&
           component.rect.size.y >= kMinimumUICheckboxSizePixels &&
           component.rect.size.y <= kMaximumUICheckboxSizePixels && std::isfinite(component.boxColor.x) &&
           std::isfinite(component.boxColor.y) && std::isfinite(component.boxColor.z) &&
           std::isfinite(component.hoverColor.x) && std::isfinite(component.hoverColor.y) &&
           std::isfinite(component.hoverColor.z) && std::isfinite(component.checkColor.x) &&
           std::isfinite(component.checkColor.y) && std::isfinite(component.checkColor.z);
}

Math::Vector3 ResolveUICheckboxBoxColor(const UICheckboxComponent& component) noexcept {
    return component.isHovered ? component.hoverColor : component.boxColor;
}

Math::Rect ResolveUICheckboxMarkRect(const UICheckboxComponent& component,
                                           const float insetRatio) noexcept {
    if (!IsUICheckboxComponentValid(component)) {
        return Math::Rect{};
    }
    const float clampedInset =
        std::isfinite(insetRatio) ? std::clamp(insetRatio, 0.0F, 0.45F) : 0.25F;
    const Math::Vector2 insetPixels{
        component.rect.size.x * clampedInset,
        component.rect.size.y * clampedInset,
    };
    return Math::Rect{
        .position = component.rect.position + insetPixels,
        .size = Math::Vector2{
            std::max(0.0F, component.rect.size.x - insetPixels.x * 2.0F),
            std::max(0.0F, component.rect.size.y - insetPixels.y * 2.0F),
        },
    };
}

bool ToggleUICheckbox(UICheckboxComponent& component) noexcept {
    component.checked = !component.checked;
    component.wasToggledThisFrame = true;
    return component.checked;
}

void ResetUICheckboxRuntimeState(UICheckboxComponent& component) noexcept {
    component.isHovered = false;
    component.wasToggledThisFrame = false;
}

} // namespace UVE::Scene
