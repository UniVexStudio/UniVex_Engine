// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_button_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Button scene object (Canvas family): the component set and
/// defaults a freshly created UI Button entity attaches — one hit-testable 120x32 button with
/// the established normal/hover/pressed palette.
struct UIButtonObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "UIButton";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "UI Button" component label exactly.
    static constexpr std::string_view defaultName = "UI Button";

    /// Button authored defaults (120x32 px, standard palette); the entity's transform is
    /// attached by the creation shell.
    UIButtonComponentUVE button{};

    [[nodiscard]] bool operator==(const UIButtonObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsUIButtonObjectDefinitionValidUVE(const UIButtonObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUIButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const UIButtonObjectDefinitionUVE& value);

/// Node policy for a `UIButton` widget object.
///
/// Owns transient hover/click state updates, point containment, and visual palette selection
/// (`normalColor`, `hoverColor`, `pressedColor`) shared by `UIRuntimeUVE`.
class UIButtonUVE final {
public:
    UIButtonUVE() = delete;

    /// True when `button` is valid and `point` lies inside `button.rect`.
    [[nodiscard]] static bool ContainsPointUVE(
        const UIButtonComponentUVE& button, const Math::Vector2UVE& point) noexcept;

    /// Clears `button.isHovered` and `button.wasClickedThisFrame` (used when a button is hidden
    /// or disabled).
    static void ClearRuntimeStateUVE(UIButtonComponentUVE& button) noexcept;

    /// Updates `button.isHovered` and `button.wasClickedThisFrame` for the current frame from
    /// pointer hover/press and keyboard focus/activate state.
    static void UpdateInteractionStateUVE(
        UIButtonComponentUVE& button, bool pointerHovered, bool mousePressedThisFrame,
        bool keyboardFocused, bool activatePressedThisFrame) noexcept;

    /// Selects `pressedColor`, `hoverColor`, or `normalColor` based on hover/focus and press-down
    /// state.
    [[nodiscard]] static Math::Vector3UVE ResolveVisualColorUVE(
        const UIButtonComponentUVE& button, bool keyboardFocused, bool pressActive) noexcept;
};

} // namespace UVE::Scene
