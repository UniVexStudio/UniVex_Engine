// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_checkbox_component_uve.h"

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

} // namespace UVE::Scene
