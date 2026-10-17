// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_checkbox_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUICheckboxComponentValidUVE(const UICheckboxComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUICheckboxSizePixelsUVE &&
           component.rect.size.x <= kMaximumUICheckboxSizePixelsUVE &&
           component.rect.size.y >= kMinimumUICheckboxSizePixelsUVE &&
           component.rect.size.y <= kMaximumUICheckboxSizePixelsUVE && std::isfinite(component.boxColor.x) &&
           std::isfinite(component.boxColor.y) && std::isfinite(component.boxColor.z) &&
           std::isfinite(component.hoverColor.x) && std::isfinite(component.hoverColor.y) &&
           std::isfinite(component.hoverColor.z) && std::isfinite(component.checkColor.x) &&
           std::isfinite(component.checkColor.y) && std::isfinite(component.checkColor.z);
}

Math::Vector3UVE ResolveUICheckboxBoxColorUVE(const UICheckboxComponentUVE& component) noexcept {
    return component.isHovered ? component.hoverColor : component.boxColor;
}

Math::RectUVE ResolveUICheckboxMarkRectUVE(const UICheckboxComponentUVE& component,
                                           const float insetRatio) noexcept {
    if (!IsUICheckboxComponentValidUVE(component)) {
        return Math::RectUVE{};
    }
    const float clampedInset =
        std::isfinite(insetRatio) ? std::clamp(insetRatio, 0.0F, 0.45F) : 0.25F;
    const Math::Vector2UVE insetPixels{
        component.rect.size.x * clampedInset,
        component.rect.size.y * clampedInset,
    };
    return Math::RectUVE{
        .position = component.rect.position + insetPixels,
        .size = Math::Vector2UVE{
            std::max(0.0F, component.rect.size.x - insetPixels.x * 2.0F),
            std::max(0.0F, component.rect.size.y - insetPixels.y * 2.0F),
        },
    };
}

bool ToggleUICheckboxUVE(UICheckboxComponentUVE& component) noexcept {
    component.checked = !component.checked;
    component.wasToggledThisFrame = true;
    return component.checked;
}

void ResetUICheckboxRuntimeStateUVE(UICheckboxComponentUVE& component) noexcept {
    component.isHovered = false;
    component.wasToggledThisFrame = false;
}

} // namespace UVE::Scene
