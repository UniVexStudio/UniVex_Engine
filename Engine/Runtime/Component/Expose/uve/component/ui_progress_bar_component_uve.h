// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIProgressBarSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIProgressBarSizePixelsUVE = 8192.0F;

/// A read-only value widget: fills `rect` from the left up to `value`'s fraction of
/// [`minValue`, `maxValue`]. Gameplay code writes `value`; the runtime only draws - no
/// hit-testing, no interaction state. An out-of-range `value` is valid and clamps at draw time; a
/// degenerate range (`minValue` equals `maxValue`) draws full when `value` reaches it, empty
/// otherwise. Draws two solid quads (background, fill) on the image layer.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIProgressBarComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{200.0F, 20.0F}};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 1.0F;
    Math::Vector3UVE backgroundColor{0.12F, 0.12F, 0.14F};
    Math::Vector3UVE fillColor{0.30F, 0.75F, 0.35F};
};

[[nodiscard]] bool IsUIProgressBarComponentValidUVE(const UIProgressBarComponentUVE& component) noexcept;

} // namespace UVE::Scene
