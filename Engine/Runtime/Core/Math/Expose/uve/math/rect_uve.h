// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "uve/math/vector2_uve.h"

namespace UVE::Math {

/// A float rectangle as a top-left `position` plus a `size` extent — the shape every UI widget
/// already carried as two Vector2s, now one value with shared geometry instead of per-callsite
/// arithmetic. A rect with a zero extent is empty; a negative extent is invalid (validators
/// reject it) and the helpers below treat it arithmetically, not geometrically.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct RectUVE {
    Vector2UVE position{};
    Vector2UVE size{};
};

[[nodiscard]] constexpr bool operator==(const RectUVE& lhs, const RectUVE& rhs) noexcept {
    return lhs.position == rhs.position && lhs.size == rhs.size;
}

[[nodiscard]] constexpr bool operator!=(const RectUVE& lhs, const RectUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// The rect's maximum corner (`position + size`). The minimum corner is `position` itself.
[[nodiscard]] constexpr Vector2UVE MaxUVE(const RectUVE& rect) noexcept {
    return rect.position + rect.size;
}

/// Whether `point` lies inside `rect`, edges included. This is UIRuntimeUVE's old hover test
/// verbatim — a button's edge pixels are hoverable, so `<=` on both far edges is load-bearing,
/// not a fencepost.
[[nodiscard]] constexpr bool ContainsUVE(const RectUVE& rect, const Vector2UVE& point) noexcept {
    return point.x >= rect.position.x && point.x <= rect.position.x + rect.size.x &&
           point.y >= rect.position.y && point.y <= rect.position.y + rect.size.y;
}

/// Whether `inner` lies entirely inside `outer`, edges included.
[[nodiscard]] constexpr bool ContainsUVE(const RectUVE& outer, const RectUVE& inner) noexcept {
    return inner.position.x >= outer.position.x && inner.position.y >= outer.position.y &&
           inner.position.x + inner.size.x <= outer.position.x + outer.size.x &&
           inner.position.y + inner.size.y <= outer.position.y + outer.size.y;
}

/// Whether the two rects overlap with nonzero area. Edge-touching rects do not intersect — a
/// zero-width shared edge is not an overlap (and keeps IntersectionUVE's empty case honest).
[[nodiscard]] constexpr bool IntersectsUVE(const RectUVE& lhs, const RectUVE& rhs) noexcept {
    return lhs.position.x < rhs.position.x + rhs.size.x && rhs.position.x < lhs.position.x + lhs.size.x &&
           lhs.position.y < rhs.position.y + rhs.size.y && rhs.position.y < lhs.position.y + lhs.size.y;
}

/// The overlap of the two rects. Disjoint (or edge-touching) rects yield a zero-size rect at the
/// clamped corner — always a valid empty rect, never a negative extent.
[[nodiscard]] constexpr RectUVE IntersectionUVE(const RectUVE& lhs, const RectUVE& rhs) noexcept {
    const float x0 = lhs.position.x > rhs.position.x ? lhs.position.x : rhs.position.x;
    const float y0 = lhs.position.y > rhs.position.y ? lhs.position.y : rhs.position.y;
    const float x1 = lhs.position.x + lhs.size.x < rhs.position.x + rhs.size.x
                         ? lhs.position.x + lhs.size.x
                         : rhs.position.x + rhs.size.x;
    const float y1 = lhs.position.y + lhs.size.y < rhs.position.y + rhs.size.y
                         ? lhs.position.y + lhs.size.y
                         : rhs.position.y + rhs.size.y;
    return RectUVE{Vector2UVE{x0, y0}, Vector2UVE{x1 > x0 ? x1 - x0 : 0.0F, y1 > y0 ? y1 - y0 : 0.0F}};
}

/// The smallest rect containing both rects.
[[nodiscard]] constexpr RectUVE UnionUVE(const RectUVE& lhs, const RectUVE& rhs) noexcept {
    const float x0 = lhs.position.x < rhs.position.x ? lhs.position.x : rhs.position.x;
    const float y0 = lhs.position.y < rhs.position.y ? lhs.position.y : rhs.position.y;
    const float x1 = lhs.position.x + lhs.size.x > rhs.position.x + rhs.size.x
                         ? lhs.position.x + lhs.size.x
                         : rhs.position.x + rhs.size.x;
    const float y1 = lhs.position.y + lhs.size.y > rhs.position.y + rhs.size.y
                         ? lhs.position.y + lhs.size.y
                         : rhs.position.y + rhs.size.y;
    return RectUVE{Vector2UVE{x0, y0}, Vector2UVE{x1 - x0, y1 - y0}};
}

/// Scales `rect` by `scale`, then translates the scaled position by `offset` — UIRuntimeUVE's
/// authored-to-presentation coordinate transform as one value operation. The extent scales but
/// never translates: an offset moves a rect, it does not resize it.
[[nodiscard]] constexpr RectUVE TransformUVE(const RectUVE& rect, const Vector2UVE& scale,
                                            const Vector2UVE& offset) noexcept {
    return RectUVE{Vector2UVE{rect.position.x * scale.x + offset.x, rect.position.y * scale.y + offset.y},
                   Vector2UVE{rect.size.x * scale.x, rect.size.y * scale.y}};
}

/// Returns whether every rect component (position and size) is finite.
[[nodiscard]] bool IsFiniteUVE(const RectUVE& value) noexcept;

/// Formats `rect` as `"((x, y), (w, h))"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const RectUVE& rect);

} // namespace UVE::Math
