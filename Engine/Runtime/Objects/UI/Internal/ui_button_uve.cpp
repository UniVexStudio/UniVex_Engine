// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_button_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIButtonObjectDefinitionValidUVE(const UIButtonObjectDefinitionUVE& value) noexcept {
    return IsUIButtonComponentValidUVE(value.button);
}

void ApplyUIButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const UIButtonObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIButtonComponentUVE>(entity, value.button);
}

bool UIButtonUVE::ContainsPointUVE(
    const UIButtonComponentUVE& button, const Math::Vector2UVE& point) noexcept {
    return HitTestUIButtonUVE(button, point);
}

void UIButtonUVE::ClearRuntimeStateUVE(UIButtonComponentUVE& button) noexcept {
    ResetUIButtonRuntimeStateUVE(button);
}

void UIButtonUVE::UpdateInteractionStateUVE(
    UIButtonComponentUVE& button, const bool pointerHovered, const bool mousePressedThisFrame,
    const bool keyboardFocused, const bool activatePressedThisFrame) noexcept {
    button.isHovered = pointerHovered;
    button.wasClickedThisFrame =
        (pointerHovered && mousePressedThisFrame) || (keyboardFocused && activatePressedThisFrame);
}

Math::Vector3UVE UIButtonUVE::ResolveVisualColorUVE(
    const UIButtonComponentUVE& button, const bool keyboardFocused,
    const bool pressActive) noexcept {
    if (button.isHovered || keyboardFocused) {
        return pressActive ? button.pressedColor : button.hoverColor;
    }
    return button.normalColor;
}

} // namespace UVE::Scene
