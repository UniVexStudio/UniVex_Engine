// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIScrollSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIScrollSizePixelsUVE = 8192.0F;

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
/// `UIScrollbarComponentUVE` on the same entity adds a draggable thumb over the wheel input.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIScrollContainerComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{200.0F, 200.0F}};
    float padding = 8.0F;
    float gap = 4.0F;
    float wheelStep = 24.0F;
    Math::Vector2UVE scrollOffset{0.0F, 0.0F};
    /// Runtime state, never persisted: last frame's full content box including padding.
    Math::Vector2UVE contentSize{0.0F, 0.0F};
};

[[nodiscard]] bool IsUIScrollContainerComponentValidUVE(const UIScrollContainerComponentUVE& component) noexcept;

} // namespace UVE::Scene
