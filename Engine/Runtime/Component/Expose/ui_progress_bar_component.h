// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIProgressBarSizePixels = 1.0F;
inline constexpr float kMaximumUIProgressBarSizePixels = 8192.0F;

/// A read-only value widget: fills `rect` from the left up to `value`'s fraction of
/// [`minValue`, `maxValue`]. Gameplay code writes `value`; the runtime only draws - no
/// hit-testing, no interaction state. An out-of-range `value` is valid and clamps at draw time; a
/// degenerate range (`minValue` equals `maxValue`) draws full when `value` reaches it, empty
/// otherwise. Draws two solid quads (background, fill) on the image layer.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIProgressBarComponent final {
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{200.0F, 20.0F}};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 1.0F;
    Math::Vector3 backgroundColor{0.12F, 0.12F, 0.14F};
    Math::Vector3 fillColor{0.30F, 0.75F, 0.35F};
};

[[nodiscard]] bool IsUIProgressBarComponentValid(const UIProgressBarComponent& component) noexcept;

/// Sanitizes `component` so `rect`, `minValue <= maxValue`, `value`, and colors are valid.
[[nodiscard]] UIProgressBarComponent SanitizeUIProgressBarComponent(
    const UIProgressBarComponent& component) noexcept;

/// Computes the normalized progress ratio in `[0, 1]` (degenerate `minValue == maxValue` returns `1` when `value >= maxValue`).
[[nodiscard]] float ResolveUIProgressRatio(
    const UIProgressBarComponent& component) noexcept;

/// Computes the left-to-right fill rectangle inside `component.rect`.
[[nodiscard]] Math::Rect ResolveUIProgressFillRect(
    const UIProgressBarComponent& component) noexcept;

} // namespace UVE::Scene
