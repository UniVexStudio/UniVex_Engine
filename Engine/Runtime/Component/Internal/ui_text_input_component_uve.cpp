// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/ui_text_input_component_uve.h"

#include <algorithm>
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

std::int32_t ClampUITextInputCaretIndexUVE(const UITextInputComponentUVE& component) noexcept {
    const auto textLen = static_cast<std::int32_t>(component.text.size());
    return std::clamp<std::int32_t>(component.caretIndex, 0, textLen);
}

bool ShouldDisplayUITextInputPlaceholderUVE(const UITextInputComponentUVE& component) noexcept {
    return component.text.empty() && !component.placeholder.empty();
}

bool InsertUITextInputCharactersUVE(UITextInputComponentUVE& component,
                                    const std::string& typedAscii) {
    component.caretIndex = ClampUITextInputCaretIndexUVE(component);
    const std::size_t effectiveCap =
        component.maxLength > 0
            ? std::min(static_cast<std::size_t>(component.maxLength), kMaximumUITextBytesUVE)
            : 0U;
    bool mutated = false;
    for (const char ch : typedAscii) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U || byte > 0x7EU) {
            continue;
        }
        if (component.text.size() >= effectiveCap) {
            break;
        }
        component.text.insert(component.text.begin() + component.caretIndex, ch);
        ++component.caretIndex;
        mutated = true;
    }
    if (mutated) {
        component.blinkTime = 0.0F;
    }
    return mutated;
}

bool ApplyUITextInputBackspaceUVE(UITextInputComponentUVE& component) noexcept {
    component.caretIndex = ClampUITextInputCaretIndexUVE(component);
    if (component.caretIndex <= 0 || component.text.empty()) {
        return false;
    }
    component.text.erase(component.text.begin() + (component.caretIndex - 1));
    --component.caretIndex;
    component.blinkTime = 0.0F;
    return true;
}

void ResetUITextInputRuntimeStateUVE(UITextInputComponentUVE& component) noexcept {
    component.focused = false;
    component.caretIndex = ClampUITextInputCaretIndexUVE(component);
    component.wasSubmittedThisFrame = false;
    component.blinkTime = 0.0F;
    component.scrollOffset = 0.0F;
}

} // namespace UVE::Scene
