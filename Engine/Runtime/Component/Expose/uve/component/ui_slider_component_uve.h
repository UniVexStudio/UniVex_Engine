// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUISliderSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUISliderSizePixelsUVE = 8192.0F;

/// A horizontal pointer-dragged value widget: `rect` is the full track, `value` ranges from
/// `minValue` to `maxValue` (`step` snaps it to increments, 0 means continuous), and the thumb -
/// `thumbWidth` pixels wide, never leaving the track - sits at the value's fraction of the
/// thumb travel (`rect` width minus `thumbWidth`). Pressing inside grabs the thumb and jumps the
/// value to the pointer; the drag then tracks the pointer even past the track ends (clamped)
/// until release. Horizontal only: vertical sliders are a follow-up.
///
/// Mixes authored config with tick-recomputed runtime state (`isHovered`, `isDragging`,
/// `wasChangedThisFrame`) - scripts and gameplay code read `wasChangedThisFrame` the same tick
/// it's set. Unlike the button's legacy hand-written persistence, these three are declared
/// RuntimeState and never round-trip: a save file recording a mid-drag pointer would be stale on
/// arrival, so the next tick recomputes them from live input instead. An out-of-range `value` is
/// valid and clamps at use; only an incoherent range (`minValue` above `maxValue`) or a
/// non-finite field fails validation, and an invalid slider is skipped entirely - no quads, state
/// reset. Draws three solid quads (track, fill-to-thumb-center, thumb) on the button layer.
/// Thread-safety: value type; trivially safe to copy/move.
struct UISliderComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{200.0F, 24.0F}};
    float value = 0.0F;
    float minValue = 0.0F;
    float maxValue = 1.0F;
    float step = 0.0F;
    Math::Vector3UVE trackColor{0.16F, 0.16F, 0.18F};
    Math::Vector3UVE fillColor{0.25F, 0.50F, 0.90F};
    Math::Vector3UVE thumbColor{0.85F, 0.85F, 0.90F};
    float thumbWidth = 12.0F;
    /// Runtime state, never persisted: true while the mouse cursor is over this slider's track rect.
    bool isHovered = false;
    /// Runtime state, never persisted: true from the press that grabbed the thumb until release.
    bool isDragging = false;
    /// Runtime state, never persisted: true for exactly the ticks the drag moved `value` -
    /// callers must read it the same tick it's set.
    bool wasChangedThisFrame = false;
};

[[nodiscard]] bool IsUISliderComponentValidUVE(const UISliderComponentUVE& component) noexcept;

} // namespace UVE::Scene
