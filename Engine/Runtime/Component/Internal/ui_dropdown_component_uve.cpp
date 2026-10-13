// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_dropdown_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIDropdownComponentValidUVE(const UIDropdownComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUIDropdownSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIDropdownSizePixelsUVE &&
           component.rect.size.y >= kMinimumUIDropdownSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIDropdownSizePixelsUVE &&
           component.options.size() <= kMaximumUIDropdownOptionsBytesUVE && component.selectedIndex >= 0 &&
           component.placeholder.size() <= kMaximumUITextBytesUVE &&
           component.fontSize >= kMinimumUIFontSizeUVE && component.fontSize <= kMaximumUIFontSizeUVE &&
           std::isfinite(component.optionHeight) &&
           component.optionHeight >= kMinimumUIDropdownOptionHeightUVE &&
           component.optionHeight <= kMaximumUIDropdownOptionHeightUVE &&
           std::isfinite(component.textPadding) && component.textPadding >= 0.0F &&
           std::isfinite(component.boxColor.x) && std::isfinite(component.boxColor.y) &&
           std::isfinite(component.boxColor.z) && std::isfinite(component.boxHoverColor.x) &&
           std::isfinite(component.boxHoverColor.y) && std::isfinite(component.boxHoverColor.z) &&
           std::isfinite(component.popupColor.x) && std::isfinite(component.popupColor.y) &&
           std::isfinite(component.popupColor.z) && std::isfinite(component.optionHoverColor.x) &&
           std::isfinite(component.optionHoverColor.y) && std::isfinite(component.optionHoverColor.z) &&
           std::isfinite(component.selectedColor.x) && std::isfinite(component.selectedColor.y) &&
           std::isfinite(component.selectedColor.z) && std::isfinite(component.textColor.x) &&
           std::isfinite(component.textColor.y) && std::isfinite(component.textColor.z) &&
           component.hoveredIndex >= -1;
}

} // namespace UVE::Scene
