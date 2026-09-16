// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIButtonSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIButtonSizePixelsUVE = 8192.0F;

/// A screen-space, rectangular, hit-testable UI button. Mixes authored config
/// (position/size/normal/hover/pressed colors) with persisted runtime state (`isHovered`,
/// `wasClickedThisFrame`) in one component, mirroring CharacterControllerComponentUVE's own
/// established convention. UIRuntimeUVE writes the runtime fields every tick by hit-testing the
/// button's screen rect against IInputSystemUVE's real mouse position/button state - scripts and
/// gameplay code read `wasClickedThisFrame` the same way they'd read any other runtime-state field.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIButtonComponentUVE final {
    Math::Vector2UVE positionPixels{};
    Math::Vector2UVE sizePixels{120.0F, 32.0F};
    Math::Vector3UVE normalColor{0.25F, 0.25F, 0.28F};
    Math::Vector3UVE hoverColor{0.35F, 0.35F, 0.40F};
    Math::Vector3UVE pressedColor{0.18F, 0.18F, 0.20F};
    /// Persisted runtime state: true while the mouse cursor is over this button's screen rect.
    bool isHovered = false;
    /// Persisted runtime state: true for exactly the one tick the button was clicked (mouse
    /// pressed this frame while hovered) - callers must read it the same tick it's set.
    bool wasClickedThisFrame = false;
};

[[nodiscard]] inline bool IsUIButtonComponentValidUVE(const UIButtonComponentUVE& component) noexcept {
    return std::isfinite(component.positionPixels.x) && std::isfinite(component.positionPixels.y) &&
           std::isfinite(component.sizePixels.x) && std::isfinite(component.sizePixels.y) &&
           component.sizePixels.x >= kMinimumUIButtonSizePixelsUVE &&
           component.sizePixels.x <= kMaximumUIButtonSizePixelsUVE &&
           component.sizePixels.y >= kMinimumUIButtonSizePixelsUVE &&
           component.sizePixels.y <= kMaximumUIButtonSizePixelsUVE && std::isfinite(component.normalColor.x) &&
           std::isfinite(component.normalColor.y) && std::isfinite(component.normalColor.z) &&
           std::isfinite(component.hoverColor.x) && std::isfinite(component.hoverColor.y) &&
           std::isfinite(component.hoverColor.z) && std::isfinite(component.pressedColor.x) &&
           std::isfinite(component.pressedColor.y) && std::isfinite(component.pressedColor.z);
}

} // namespace UVE::Scene
