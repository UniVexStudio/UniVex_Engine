// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_dropdown_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUIDropdownComponentValid(const UIDropdownComponent& component) noexcept {
    return Math::IsFinite(component.rect) &&
           component.rect.size.x >= kMinimumUIDropdownSizePixels &&
           component.rect.size.x <= kMaximumUIDropdownSizePixels &&
           component.rect.size.y >= kMinimumUIDropdownSizePixels &&
           component.rect.size.y <= kMaximumUIDropdownSizePixels &&
           component.options.size() <= kMaximumUIDropdownOptionsBytes && component.selectedIndex >= 0 &&
           component.placeholder.size() <= kMaximumUITextBytes &&
           component.fontSize >= kMinimumUIFontSize && component.fontSize <= kMaximumUIFontSize &&
           std::isfinite(component.optionHeight) &&
           component.optionHeight >= kMinimumUIDropdownOptionHeight &&
           component.optionHeight <= kMaximumUIDropdownOptionHeight &&
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

std::vector<std::string> SplitUIDropdownOptions(const UIDropdownComponent& component) {
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

std::string ResolveUIDropdownDisplayLabel(const UIDropdownComponent& component) {
    const std::vector<std::string> parsed = SplitUIDropdownOptions(component);
    if (parsed.empty()) {
        return component.placeholder;
    }
    const std::int32_t clampedIdx =
        std::clamp<std::int32_t>(component.selectedIndex, 0,
                                 static_cast<std::int32_t>(parsed.size() - 1U));
    return parsed[static_cast<std::size_t>(clampedIdx)];
}

bool SelectUIDropdownOption(UIDropdownComponent& component,
                               const std::int32_t optionIndex) noexcept {
    const std::vector<std::string> parsed = SplitUIDropdownOptions(component);
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

void ResetUIDropdownRuntimeState(UIDropdownComponent& component) noexcept {
    component.open = false;
    component.isHovered = false;
    component.hoveredIndex = -1;
    component.wasSelectionChangedThisFrame = false;
}

} // namespace UVE::Scene
