// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_button.h"

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsUIButtonObjectDefinitionValid(const UIButtonObjectDefinition& value) noexcept {
    return IsUIButtonComponentValid(value.button);
}

void ApplyUIButtonObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const UIButtonObjectDefinition& value) {
    entityManager.AddComponent<UIButtonComponent>(entity, value.button);
}

bool UIButton::ContainsPoint(
    const UIButtonComponent& button, const Math::Vector2& point) noexcept {
    return HitTestUIButton(button, point);
}

void UIButton::ClearRuntimeState(UIButtonComponent& button) noexcept {
    ResetUIButtonRuntimeState(button);
}

void UIButton::UpdateInteractionState(
    UIButtonComponent& button, const bool pointerHovered, const bool mousePressedThisFrame,
    const bool keyboardFocused, const bool activatePressedThisFrame) noexcept {
    button.isHovered = pointerHovered;
    button.wasClickedThisFrame =
        (pointerHovered && mousePressedThisFrame) || (keyboardFocused && activatePressedThisFrame);
}

Math::Vector3 UIButton::ResolveVisualColor(
    const UIButtonComponent& button, const bool keyboardFocused,
    const bool pressActive) noexcept {
    if (button.isHovered || keyboardFocused) {
        return pressActive ? button.pressedColor : button.hoverColor;
    }
    return button.normalColor;
}

} // namespace UVE::Scene
