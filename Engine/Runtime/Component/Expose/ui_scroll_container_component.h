// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "rect.h"
#include "vector2.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIScrollSizePixels = 1.0F;
inline constexpr float kMaximumUIScrollSizePixels = 8192.0F;

/// A clipped vertical stack: children lay out top-down inside `rect` (padding around, gap between)
/// and content past the box scrolls under the mouse wheel (`wheelStep` pixels per wheel unit,
/// Shift+wheel for horizontal) instead of spilling out. `scrollOffset` is authored and persists -
/// a reopened panel sits where it was left - while `contentSize` is recomputed every frame and
/// never round-trips. The box draws nothing itself; compose an image behind it for a background.
///
/// Clipping is software: every emitted quad is intersected against its scroll ancestors (nested
/// clips intersect) with texture coordinates remapped, fully-outside quads dropped, and
/// hit-testing gated on the same clips, so no renderer changes were needed. Partially visible
/// widgets still interact - only the pixels (and hover outside the clip) are cut. A scroll
/// container stacks its children itself, so a layout container on the same entity is overridden;
/// scroll containers do not nest (an inner one stacks without ever scrolling). An optional
/// `UIScrollbarComponent` on the same entity adds a draggable thumb over the wheel input.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIScrollContainerComponent final {
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{200.0F, 200.0F}};
    float padding = 8.0F;
    float gap = 4.0F;
    float wheelStep = 24.0F;
    Math::Vector2 scrollOffset{0.0F, 0.0F};
    /// Runtime state, never persisted: last frame's full content box including padding.
    Math::Vector2 contentSize{0.0F, 0.0F};
};

[[nodiscard]] bool IsUIScrollContainerComponentValid(const UIScrollContainerComponent& component) noexcept;

/// Computes the maximum valid scroll offset (`max(0, contentSize - rect.size)` per axis).
[[nodiscard]] Math::Vector2 ResolveUIScrollMaxOffset(
    const UIScrollContainerComponent& component) noexcept;

/// Clamps `desiredScrollOffset` into `[0, ResolveUIScrollMaxOffset(component)]` per axis.
[[nodiscard]] Math::Vector2 ClampUIScrollOffset(
    const UIScrollContainerComponent& component,
    const Math::Vector2& desiredScrollOffset) noexcept;

/// True when `contentSize.y > rect.size.y` (vertical content overflows the viewport).
[[nodiscard]] bool HasUIScrollVerticalOverflow(
    const UIScrollContainerComponent& component) noexcept;

/// Applies a mouse-wheel delta (`wheelDeltaUnits`) along Y (or X when `horizontalWheel` is true) and clamps `scrollOffset`.
Math::Vector2 ApplyUIScrollWheelDelta(UIScrollContainerComponent& component,
                                            float wheelDeltaUnits,
                                            bool horizontalWheel = false) noexcept;

} // namespace UVE::Scene
