// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIButtonSizePixels = 1.0F;
inline constexpr float kMaximumUIButtonSizePixels = 8192.0F;

/// A screen-space, rectangular, hit-testable UI button. Mixes authored config
/// (position/size/normal/hover/pressed colors) with persisted runtime state (`isHovered`,
/// `wasClickedThisFrame`) in one component, mirroring CharacterControllerComponent's own
/// established convention. UIRuntime writes the runtime fields every tick by hit-testing the
/// button's screen rect against IInputSystem's real mouse position/button state - scripts and
/// gameplay code read `wasClickedThisFrame` the same way they'd read any other runtime-state field.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIButtonComponent final {
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{120.0F, 32.0F}};
    Math::Vector3 normalColor{0.25F, 0.25F, 0.28F};
    Math::Vector3 hoverColor{0.35F, 0.35F, 0.40F};
    Math::Vector3 pressedColor{0.18F, 0.18F, 0.20F};
    /// Persisted runtime state: true while the mouse cursor is over this button's screen rect.
    bool isHovered = false;
    /// Persisted runtime state: true for exactly the one tick the button was clicked (mouse
    /// pressed this frame while hovered) - callers must read it the same tick it's set.
    bool wasClickedThisFrame = false;
    [[nodiscard]] bool operator==(const UIButtonComponent&) const = default;
};

[[nodiscard]] bool IsUIButtonComponentValid(const UIButtonComponent& component) noexcept;

/// Returns the active display color (`pressedColor` when hovered & mouseDown, `hoverColor` when hovered, else `normalColor`).
[[nodiscard]] Math::Vector3 ResolveUIButtonActiveColor(const UIButtonComponent& component,
                                                             bool isMouseButtonDown = false) noexcept;

/// True when `pointPixels` lies inside `component.rect`.
[[nodiscard]] bool HitTestUIButton(const UIButtonComponent& component,
                                      const Math::Vector2& pointPixels) noexcept;

/// Resets runtime state (`isHovered = false`, `wasClickedThisFrame = false`).
void ResetUIButtonRuntimeState(UIButtonComponent& component) noexcept;

/// Steps `isHovered` and `wasClickedThisFrame` from `pointerPixels` and `mouseJustPressed`.
bool StepUIButtonInteraction(UIButtonComponent& component,
                                const Math::Vector2& pointerPixels,
                                bool mouseJustPressed) noexcept;

} // namespace UVE::Scene
