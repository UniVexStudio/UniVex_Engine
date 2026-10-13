// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

enum class UITweenTargetUVE : std::uint8_t {
    Rect = 0,
    Alpha = 1,
};

enum class UITweenLoopUVE : std::uint8_t {
    Once = 0,
    Loop = 1,
    PingPong = 2,
};

enum class UITweenEaseUVE : std::uint8_t {
    Linear = 0,
    SineInOut = 1,
    QuadIn = 2,
    QuadOut = 3,
    QuadInOut = 4,
    CubicIn = 5,
    CubicOut = 6,
    CubicInOut = 7,
    OutBack = 8,
};

/// Animates one property of the widget on the same entity over `duration` seconds after `delay`:
/// `Rect` lerps every positioned component the entity carries (buttons, images, sliders, progress
/// bars, containers take the full rect; text takes the position, size ignored) from `fromRect` to
/// `toRect`, and `Alpha` fades `currentAlpha` from `fromAlpha` to `toAlpha`, multiplied with the
/// widget's authored alpha at draw time so a half-transparent image stays half of whatever the
/// fade says. `OutBack` overshoots rects past `toRect` by design; alpha clamps to [0, 1] because
/// overshooting opacity is meaningless.
///
/// The tween pass runs after anchors and layout, so an active tween overrides the resting
/// arrangement - a slide-in moves even an anchored dialog, and hit-testing sees the tweened rect
/// within the same tick. The price is one frame of lag for a tweened container's children, which
/// were positioned from the pre-tween rect and settle the following tick.
///
/// Only the authored half persists (`target`, from/to values, `duration`, `delay`, `ease`,
/// `loop`); the runtime half (`elapsed`, `playing`, `completedThisFrame`, `currentAlpha`) is
/// declared RuntimeState and reseeds on load, so a saved mid-flight tween restarts from its
/// from-values. `completedThisFrame` fires on cycle boundaries - once at the end for `Once`, at
/// every wrap for `Loop` and `PingPong`. A non-positive `duration` fails validation and the tween
/// never advances; an invalid alpha tween parks `currentAlpha` at the neutral 1 so it dims
/// nothing. Thread-safety: value type; trivially safe to copy/move.
struct UITweenComponentUVE final {
    UITweenTargetUVE target = UITweenTargetUVE::Rect;
    Math::RectUVE fromRect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{120.0F, 32.0F}};
    Math::RectUVE toRect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{120.0F, 32.0F}};
    float fromAlpha = 1.0F;
    float toAlpha = 1.0F;
    float duration = 1.0F;
    float delay = 0.0F;
    UITweenEaseUVE ease = UITweenEaseUVE::Linear;
    UITweenLoopUVE loop = UITweenLoopUVE::Once;
    /// Runtime state, never persisted: seconds advanced so far, including the delay phase.
    float elapsed = 0.0F;
    /// Runtime state, never persisted: false once a `Once` tween reaches its end.
    bool playing = true;
    /// Runtime state, never persisted: true for exactly the ticks a cycle boundary is crossed.
    bool completedThisFrame = false;
    /// Runtime state, never persisted: the eased alpha multiplier quads are drawn with.
    float currentAlpha = 1.0F;
};

/// Finite from/to values and state, positive finite duration, non-negative finite delay, and
/// known target/ease/loop values.
[[nodiscard]] bool IsUITweenComponentValidUVE(const UITweenComponentUVE& value) noexcept;

} // namespace UVE::Scene
