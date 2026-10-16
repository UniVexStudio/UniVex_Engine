// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumCamera2DZoomUVE = 0.01F;
inline constexpr float kDefaultCamera2DZoomUVE = 1.0F;

/// A 2D scrolling camera: the view a 2D scene is authored through. `zoom` scales each axis
/// (1 = one world unit per pixel at a 1:1 view; larger zooms in), `offset` shifts the view
/// center from the node's origin in world units, and `current` marks the camera the 2D view
/// follows when several exist - persisted now, consumed by the 2D renderer when it lands.
/// Thread-safety: value type; trivially safe to copy/move.
struct Camera2DComponentUVE final {
    Math::Vector2UVE zoom = {1.0F, 1.0F};
    Math::Vector2UVE offset{};
    bool current = false;
};

[[nodiscard]] bool IsCamera2DComponentValidUVE(const Camera2DComponentUVE& camera) noexcept;

/// Clamps each axis of `zoom` to at least `kMinimumCamera2DZoomUVE` (or `1.0F` when non-finite).
[[nodiscard]] Math::Vector2UVE ClampCamera2DZoomUVE(const Math::Vector2UVE& zoom) noexcept;

/// Sanitizes `camera` so `zoom` and `offset` pass `IsCamera2DComponentValidUVE`.
[[nodiscard]] Camera2DComponentUVE SanitizeCamera2DComponentUVE(
    const Camera2DComponentUVE& camera) noexcept;

/// Computes the world-space view center (`worldPosition + camera.offset`).
[[nodiscard]] Math::Vector2UVE ResolveCamera2DViewCenterUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& worldPosition) noexcept;

/// Computes the visible world-space half-extents for `viewportSizePixels` at `camera.zoom`.
[[nodiscard]] Math::Vector2UVE ResolveCamera2DVisibleHalfExtentsUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSizePixels) noexcept;

/// Computes the visible axis-aligned world-space rectangle for `camera` at `worldPosition`.
[[nodiscard]] Math::RectUVE ResolveCamera2DVisibleWorldRectUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& worldPosition,
    const Math::Vector2UVE& viewportSizePixels) noexcept;

} // namespace UVE::Scene
