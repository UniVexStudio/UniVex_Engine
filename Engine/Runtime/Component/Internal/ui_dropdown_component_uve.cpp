// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_dropdown_component_uve.h"

#include <algorithm>
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

std::vector<std::string> SplitUIDropdownOptionsUVE(const UIDropdownComponentUVE& component) {
    std::vector<std::string> out;
    std::size_t start = 0U;
    while (start <= component.options.size()) {
        const std::size_t end = component.options.find('\n', start);
        const std::size_t len =
            (end == std::string::npos) ? (component.options.size() - start) : (end - start);
        if (len > 0U) {
            out.push_back(component.options.substr(start, len));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return out;
}

std::string ResolveUIDropdownDisplayLabelUVE(const UIDropdownComponentUVE& component) {
    const std::vector<std::string> parsed = SplitUIDropdownOptionsUVE(component);
    if (parsed.empty()) {
        return component.placeholder;
    }
    const std::int32_t clampedIdx =
        std::clamp<std::int32_t>(component.selectedIndex, 0,
                                 static_cast<std::int32_t>(parsed.size() - 1U));
    return parsed[static_cast<std::size_t>(clampedIdx)];
}

bool SelectUIDropdownOptionUVE(UIDropdownComponentUVE& component,
                               const std::int32_t optionIndex) noexcept {
    const std::vector<std::string> parsed = SplitUIDropdownOptionsUVE(component);
    if (parsed.empty() || optionIndex < 0 ||
        static_cast<std::size_t>(optionIndex) >= parsed.size()) {
        component.open = false;
        return false;
    }
    const bool changed = (component.selectedIndex != optionIndex);
    component.selectedIndex = optionIndex;
    component.wasSelectionChangedThisFrame = changed;
    component.open = false;
    return changed;
}

void ResetUIDropdownRuntimeStateUVE(UIDropdownComponentUVE& component) noexcept {
    component.open = false;
    component.isHovered = false;
    component.hoveredIndex = -1;
    component.wasSelectionChangedThisFrame = false;
}

} // namespace UVE::Scene
