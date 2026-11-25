// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "rect.h"
#include "vector2i.h"

namespace UVE::Math {

/// An integer rectangle as a top-left `position` plus a `size` extent: viewport regions, scissor
/// rects, pixel-space layout. The integer twin of Rect with identical inclusive-Contains and
/// strict-Intersects semantics; corner arithmetic widens to int64 internally so rects near
/// INT32_MAX compare correctly instead of overflowing. `Render::ViewportRect` is this type —
/// the RHI's old four-uint32 struct is retired, not wrapped.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct RectInt {
    Vector2i position{};
    Vector2i size{};
};

[[nodiscard]] constexpr bool operator==(const RectInt& lhs, const RectInt& rhs) noexcept {
    return lhs.position == rhs.position && lhs.size == rhs.size;
}

[[nodiscard]] constexpr bool operator!=(const RectInt& lhs, const RectInt& rhs) noexcept {
    return !(lhs == rhs);
}

/// The rect's maximum corner (`position + size`), widened to int64 so callers comparing against
/// it cannot overflow.
[[nodiscard]] constexpr Vector2i Max(const RectInt& rect) noexcept {
    return rect.position + rect.size;
}

/// Whether `point` lies inside `rect`, edges included.
[[nodiscard]] constexpr bool Contains(const RectInt& rect, const Vector2i& point) noexcept {
    return static_cast<std::int64_t>(point.x) >= static_cast<std::int64_t>(rect.position.x) &&
           static_cast<std::int64_t>(point.x) <=
               static_cast<std::int64_t>(rect.position.x) + static_cast<std::int64_t>(rect.size.x) &&
           static_cast<std::int64_t>(point.y) >= static_cast<std::int64_t>(rect.position.y) &&
           static_cast<std::int64_t>(point.y) <=
               static_cast<std::int64_t>(rect.position.y) + static_cast<std::int64_t>(rect.size.y);
}

/// Whether `inner` lies entirely inside `outer`, edges included. This is the renderer pass
/// fit-check: a viewport override must be contained in its target's bounds rect.
[[nodiscard]] constexpr bool Contains(const RectInt& outer, const RectInt& inner) noexcept {
    return static_cast<std::int64_t>(inner.position.x) >= static_cast<std::int64_t>(outer.position.x) &&
           static_cast<std::int64_t>(inner.position.y) >= static_cast<std::int64_t>(outer.position.y) &&
           static_cast<std::int64_t>(inner.position.x) + static_cast<std::int64_t>(inner.size.x) <=
               static_cast<std::int64_t>(outer.position.x) + static_cast<std::int64_t>(outer.size.x) &&
           static_cast<std::int64_t>(inner.position.y) + static_cast<std::int64_t>(inner.size.y) <=
               static_cast<std::int64_t>(outer.position.y) + static_cast<std::int64_t>(outer.size.y);
}

/// Whether the two rects overlap with nonzero area. Edge-touching rects do not intersect.
[[nodiscard]] constexpr bool Intersects(const RectInt& lhs, const RectInt& rhs) noexcept {
    return static_cast<std::int64_t>(lhs.position.x) <
               static_cast<std::int64_t>(rhs.position.x) + static_cast<std::int64_t>(rhs.size.x) &&
           static_cast<std::int64_t>(rhs.position.x) <
               static_cast<std::int64_t>(lhs.position.x) + static_cast<std::int64_t>(lhs.size.x) &&
           static_cast<std::int64_t>(lhs.position.y) <
               static_cast<std::int64_t>(rhs.position.y) + static_cast<std::int64_t>(rhs.size.y) &&
           static_cast<std::int64_t>(rhs.position.y) <
               static_cast<std::int64_t>(lhs.position.y) + static_cast<std::int64_t>(lhs.size.y);
}

/// The overlap of the two rects. Disjoint (or edge-touching) rects yield a zero-size rect at the
/// clamped corner — always a valid empty rect, never a negative extent.
[[nodiscard]] constexpr RectInt Intersection(const RectInt& lhs, const RectInt& rhs) noexcept {
    const std::int64_t x0 = static_cast<std::int64_t>(lhs.position.x) > static_cast<std::int64_t>(rhs.position.x)
                                ? static_cast<std::int64_t>(lhs.position.x)
                                : static_cast<std::int64_t>(rhs.position.x);
    const std::int64_t y0 = static_cast<std::int64_t>(lhs.position.y) > static_cast<std::int64_t>(rhs.position.y)
                                ? static_cast<std::int64_t>(lhs.position.y)
                                : static_cast<std::int64_t>(rhs.position.y);
    const std::int64_t lhsX1 =
        static_cast<std::int64_t>(lhs.position.x) + static_cast<std::int64_t>(lhs.size.x);
    const std::int64_t rhsX1 =
        static_cast<std::int64_t>(rhs.position.x) + static_cast<std::int64_t>(rhs.size.x);
    const std::int64_t lhsY1 =
        static_cast<std::int64_t>(lhs.position.y) + static_cast<std::int64_t>(lhs.size.y);
    const std::int64_t rhsY1 =
        static_cast<std::int64_t>(rhs.position.y) + static_cast<std::int64_t>(rhs.size.y);
    const std::int64_t x1 = lhsX1 < rhsX1 ? lhsX1 : rhsX1;
    const std::int64_t y1 = lhsY1 < rhsY1 ? lhsY1 : rhsY1;
    // x0/y0 are each one input's own position, so the int32 narrowing is value-preserving.
    // The width/height are bounded by one input's own extent whenever they are positive
    // (min-of-maxes minus max-of-mins never exceeds either input's width), so they narrow
    // exactly too; disjoint rects take the zero branch instead.
    const std::int32_t width = x1 > x0 ? static_cast<std::int32_t>(x1 - x0) : 0;
    const std::int32_t height = y1 > y0 ? static_cast<std::int32_t>(y1 - y0) : 0;
    return RectInt{Vector2i{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
                      Vector2i{width, height}};
}

/// The smallest rect containing both rects. Callers own the range: a union spanning past INT32_MAX
/// narrows (viewport-sized rects never do).
[[nodiscard]] constexpr RectInt Union(const RectInt& lhs, const RectInt& rhs) noexcept {
    const std::int32_t x0 = lhs.position.x < rhs.position.x ? lhs.position.x : rhs.position.x;
    const std::int32_t y0 = lhs.position.y < rhs.position.y ? lhs.position.y : rhs.position.y;
    const std::int64_t x1 = (static_cast<std::int64_t>(lhs.position.x) + static_cast<std::int64_t>(lhs.size.x) >
                             static_cast<std::int64_t>(rhs.position.x) + static_cast<std::int64_t>(rhs.size.x))
                                ? static_cast<std::int64_t>(lhs.position.x) + static_cast<std::int64_t>(lhs.size.x)
                                : static_cast<std::int64_t>(rhs.position.x) + static_cast<std::int64_t>(rhs.size.x);
    const std::int64_t y1 = (static_cast<std::int64_t>(lhs.position.y) + static_cast<std::int64_t>(lhs.size.y) >
                             static_cast<std::int64_t>(rhs.position.y) + static_cast<std::int64_t>(rhs.size.y))
                                ? static_cast<std::int64_t>(lhs.position.y) + static_cast<std::int64_t>(lhs.size.y)
                                : static_cast<std::int64_t>(rhs.position.y) + static_cast<std::int64_t>(rhs.size.y);
    return RectInt{Vector2i{x0, y0},
                      Vector2i{static_cast<std::int32_t>(x1 - static_cast<std::int64_t>(x0)),
                                  static_cast<std::int32_t>(y1 - static_cast<std::int64_t>(y0))}};
}

/// Converts to float space (magnitudes past 2^24 round, per ToVector2).
[[nodiscard]] constexpr Rect ToRect(const RectInt& rect) noexcept {
    return Rect{ToVector2(rect.position), ToVector2(rect.size)};
}

/// Formats `rect` as `"((x, y), (w, h))"`, for logging/debugging.
[[nodiscard]] std::string ToString(const RectInt& rect);

} // namespace UVE::Math
