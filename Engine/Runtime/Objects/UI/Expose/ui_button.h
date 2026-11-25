// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "entity.h"
#include "ui_button_component.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the UI Button scene object (Canvas family): the component set and
/// defaults a freshly created UI Button entity attaches — one hit-testable 120x32 button with
/// the established normal/hover/pressed palette.
struct UIButtonObjectDefinition final {
    static constexpr std::string_view typeName = "UIButton";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "UI Button" component label exactly.
    static constexpr std::string_view defaultName = "UI Button";

    /// Button authored defaults (120x32 px, standard palette); the entity's transform is
    /// attached by the creation shell.
    UIButtonComponent button{};

    [[nodiscard]] bool operator==(const UIButtonObjectDefinition&) const = default;
};

[[nodiscard]] bool IsUIButtonObjectDefinitionValid(const UIButtonObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUIButtonObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const UIButtonObjectDefinition& value);

/// Node policy for a `UIButton` widget object.
///
/// Owns transient hover/click state updates, point containment, and visual palette selection
/// (`normalColor`, `hoverColor`, `pressedColor`) shared by `UIRuntime`.
class UIButton final {
public:
    UIButton() = delete;

    /// True when `button` is valid and `point` lies inside `button.rect`.
    [[nodiscard]] static bool ContainsPoint(
        const UIButtonComponent& button, const Math::Vector2& point) noexcept;

    /// Clears `button.isHovered` and `button.wasClickedThisFrame` (used when a button is hidden
    /// or disabled).
    static void ClearRuntimeState(UIButtonComponent& button) noexcept;

    /// Updates `button.isHovered` and `button.wasClickedThisFrame` for the current frame from
    /// pointer hover/press and keyboard focus/activate state.
    static void UpdateInteractionState(
        UIButtonComponent& button, bool pointerHovered, bool mousePressedThisFrame,
        bool keyboardFocused, bool activatePressedThisFrame) noexcept;

    /// Selects `pressedColor`, `hoverColor`, or `normalColor` based on hover/focus and press-down
    /// state.
    [[nodiscard]] static Math::Vector3 ResolveVisualColor(
        const UIButtonComponent& button, bool keyboardFocused, bool pressActive) noexcept;
};

} // namespace UVE::Scene
