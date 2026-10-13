// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIScrollbarSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIScrollbarSizePixelsUVE = 8192.0F;

inline constexpr float kDefaultUIScrollbarMinThumbHeightUVE = 16.0F;

/// A vertical scrollbar bound to the scroll container on the SAME entity: `rect` is the track
/// (authored absolute, usually a strip on the box's right edge), the thumb's height is the
/// viewport/content fraction of the track (floored at `minThumbHeight`), and dragging the thumb
/// writes the container's `scrollOffset.y`. Pressing the track grabs the thumb and the offset
/// jumps to the pointer - the same grab-then-track contract as the slider, except the grab takes
/// effect on the next tick: the drag applies before the scroll layout pass stacks the children,
/// so dragged content never lags the thumb by a frame. With no overflow the thumb fills the
/// track and drags are inert. Vertical only: horizontal scrollbars are a follow-up, mirroring
/// the slider's horizontal-only rule.
///
/// Mixes authored config with tick-recomputed runtime state (`isHovered`, `isDragging`,
/// `wasChangedThisFrame`) - scripts and gameplay code read `wasChangedThisFrame` the same tick
/// it's set. Like the slider's, these three are declared RuntimeState and never round-trip: a
/// save file recording a mid-drag pointer would be stale on arrival, so the next tick
/// recomputes them from live input instead. Without a scroll container on the entity, or when
/// invalid, the scrollbar is skipped entirely - no quads, state reset. Draws two solid quads
/// (track, thumb) on layer 2, above scroll content.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIScrollbarComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{12.0F, 200.0F}};
    Math::Vector3UVE trackColor{0.10F, 0.10F, 0.12F};
    Math::Vector3UVE thumbColor{0.45F, 0.45F, 0.50F};
    float minThumbHeight = kDefaultUIScrollbarMinThumbHeightUVE;
    /// Runtime state, never persisted: true while the mouse cursor is over this scrollbar's track rect.
    bool isHovered = false;
    /// Runtime state, never persisted: true from the press that grabbed the thumb until release.
    bool isDragging = false;
    /// Runtime state, never persisted: true for exactly the ticks the drag moved `scrollOffset.y` -
    /// callers must read it the same tick it's set.
    bool wasChangedThisFrame = false;
};

[[nodiscard]] bool IsUIScrollbarComponentValidUVE(const UIScrollbarComponentUVE& component) noexcept;

} // namespace UVE::Scene
