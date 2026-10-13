// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "uve/math/vector2_uve.h"

namespace UVE::Math {

/// A 2-component 32-bit integer vector: pixel coordinates, grid indices, viewport extents.
/// Component-wise arithmetic is plain int32 (callers own the range, as with every ivec2); only
/// the reductions widen — DotUVE/LengthSquaredUVE return int64 computed without overflow, and
/// LengthUVE returns a float. There is no NormalizeUVE (a unit vector is not an integer vector)
/// and no IsFiniteUVE (every int32 is finite); convert with ToVector2UVE for float-space math.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector2iUVE {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector2iUVE operator+(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return Vector2iUVE{lhs.x + rhs.x, lhs.y + rhs.y};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector2iUVE operator-(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return Vector2iUVE{lhs.x - rhs.x, lhs.y - rhs.y};
}

/// Component-wise negation.
[[nodiscard]] constexpr Vector2iUVE operator-(const Vector2iUVE& v) noexcept {
    return Vector2iUVE{-v.x, -v.y};
}

/// Both components scaled by `scale` (truncation-free: int32 times int32 is exact until it
/// overflows, which is the caller's range to own).
[[nodiscard]] constexpr Vector2iUVE operator*(const Vector2iUVE& lhs, const std::int32_t scale) noexcept {
    return Vector2iUVE{lhs.x * scale, lhs.y * scale};
}

[[nodiscard]] constexpr bool operator==(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] constexpr bool operator!=(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product, widened to int64 before multiplying so e.g. {50000, 50000} dot itself is the
/// exact 5e9 rather than int32 overflow. Integer math needs no Vector2UVE-style float fallback:
/// the widened computation cannot lose precision.
[[nodiscard]] constexpr std::int64_t DotUVE(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return static_cast<std::int64_t>(lhs.x) * static_cast<std::int64_t>(rhs.x) +
           static_cast<std::int64_t>(lhs.y) * static_cast<std::int64_t>(rhs.y);
}

/// Squared length — cheaper than LengthUVE() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr std::int64_t LengthSquaredUVE(const Vector2iUVE& v) noexcept {
    return DotUVE(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot over doubles.
[[nodiscard]] float LengthUVE(const Vector2iUVE& v) noexcept;

/// Component-wise minimum.
[[nodiscard]] constexpr Vector2iUVE MinUVE(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return Vector2iUVE{lhs.x < rhs.x ? lhs.x : rhs.x, lhs.y < rhs.y ? lhs.y : rhs.y};
}

/// Component-wise maximum.
[[nodiscard]] constexpr Vector2iUVE MaxUVE(const Vector2iUVE& lhs, const Vector2iUVE& rhs) noexcept {
    return Vector2iUVE{lhs.x > rhs.x ? lhs.x : rhs.x, lhs.y > rhs.y ? lhs.y : rhs.y};
}

/// Component-wise clamp of `value` into [`lo`, `hi`]. `lo` must not exceed `hi` per component.
[[nodiscard]] constexpr Vector2iUVE ClampUVE(const Vector2iUVE& value, const Vector2iUVE& lo,
                                            const Vector2iUVE& hi) noexcept {
    return MaxUVE(lo, MinUVE(value, hi));
}

/// Converts to float space. Magnitudes past 2^24 round to the nearest representable float.
[[nodiscard]] constexpr Vector2UVE ToVector2UVE(const Vector2iUVE& v) noexcept {
    return Vector2UVE{static_cast<float>(v.x), static_cast<float>(v.y)};
}

/// Formats `vector` as `"(x, y)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const Vector2iUVE& vector);

} // namespace UVE::Math
