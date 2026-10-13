// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_button_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIButtonComponentValidUVE(const UIButtonComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIButtonSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIButtonSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIButtonSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIButtonSizePixelsUVE && std::isfinite(component.normalColor.x) &&
           std::isfinite(component.normalColor.y) && std::isfinite(component.normalColor.z) &&
           std::isfinite(component.hoverColor.x) && std::isfinite(component.hoverColor.y) &&
           std::isfinite(component.hoverColor.z) && std::isfinite(component.pressedColor.x) &&
           std::isfinite(component.pressedColor.y) && std::isfinite(component.pressedColor.z);
}

} // namespace UVE::Scene
