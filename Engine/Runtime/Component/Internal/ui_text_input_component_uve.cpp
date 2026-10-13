// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_text_input_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsUITextInputComponentValidUVE(const UITextInputComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) &&
           component.rect.size.x >= kMinimumUITextInputSizePixelsUVE &&
           component.rect.size.x <= kMaximumUITextInputSizePixelsUVE &&
           component.rect.size.y >= kMinimumUITextInputSizePixelsUVE &&
           component.rect.size.y <= kMaximumUITextInputSizePixelsUVE &&
           component.text.size() <= kMaximumUITextBytesUVE &&
           component.maxLength >= kMinimumUITextInputLengthUVE &&
           static_cast<std::size_t>(component.maxLength) <= kMaximumUITextBytesUVE &&
           component.placeholder.size() <= kMaximumUITextBytesUVE &&
           component.fontSize >= kMinimumUIFontSizeUVE && component.fontSize <= kMaximumUIFontSizeUVE &&
           std::isfinite(component.textPadding) && component.textPadding >= 0.0F &&
           std::isfinite(component.boxColor.x) && std::isfinite(component.boxColor.y) &&
           std::isfinite(component.boxColor.z) && std::isfinite(component.focusColor.x) &&
           std::isfinite(component.focusColor.y) && std::isfinite(component.focusColor.z) &&
           std::isfinite(component.textColor.x) && std::isfinite(component.textColor.y) &&
           std::isfinite(component.textColor.z) && std::isfinite(component.caretColor.x) &&
           std::isfinite(component.caretColor.y) && std::isfinite(component.caretColor.z) &&
           std::isfinite(component.placeholderColor.x) && std::isfinite(component.placeholderColor.y) &&
           std::isfinite(component.placeholderColor.z) && component.caretIndex >= 0 &&
           std::isfinite(component.blinkTime) && component.blinkTime >= 0.0F &&
           std::isfinite(component.scrollOffset) && component.scrollOffset >= 0.0F;
}

} // namespace UVE::Scene
