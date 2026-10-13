// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// A 3-component 32-bit integer vector: voxel/grid coordinates, the integer twin of Vector3UVE.
/// Same contract as Vector2iUVE — plain int32 arithmetic, widened int64 reductions, no normalize
/// (a unit vector is not an integer vector), ToVector3UVE for float-space math. No in-tree user
/// yet; it completes the integer family so the first voxel/grid feature does not invent its own.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector3iUVE {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector3iUVE operator+(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return Vector3iUVE{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector3iUVE operator-(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return Vector3iUVE{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

/// Component-wise negation.
[[nodiscard]] constexpr Vector3iUVE operator-(const Vector3iUVE& v) noexcept {
    return Vector3iUVE{-v.x, -v.y, -v.z};
}

/// All components scaled by `scale`.
[[nodiscard]] constexpr Vector3iUVE operator*(const Vector3iUVE& lhs, const std::int32_t scale) noexcept {
    return Vector3iUVE{lhs.x * scale, lhs.y * scale, lhs.z * scale};
}

[[nodiscard]] constexpr bool operator==(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] constexpr bool operator!=(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product, widened to int64 before multiplying so it cannot overflow where int32 would.
[[nodiscard]] constexpr std::int64_t DotUVE(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return static_cast<std::int64_t>(lhs.x) * static_cast<std::int64_t>(rhs.x) +
           static_cast<std::int64_t>(lhs.y) * static_cast<std::int64_t>(rhs.y) +
           static_cast<std::int64_t>(lhs.z) * static_cast<std::int64_t>(rhs.z);
}

/// Squared length — cheaper than LengthUVE() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr std::int64_t LengthSquaredUVE(const Vector3iUVE& v) noexcept {
    return DotUVE(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot over doubles.
[[nodiscard]] float LengthUVE(const Vector3iUVE& v) noexcept;

/// Component-wise minimum.
[[nodiscard]] constexpr Vector3iUVE MinUVE(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return Vector3iUVE{lhs.x < rhs.x ? lhs.x : rhs.x, lhs.y < rhs.y ? lhs.y : rhs.y,
                       lhs.z < rhs.z ? lhs.z : rhs.z};
}

/// Component-wise maximum.
[[nodiscard]] constexpr Vector3iUVE MaxUVE(const Vector3iUVE& lhs, const Vector3iUVE& rhs) noexcept {
    return Vector3iUVE{lhs.x > rhs.x ? lhs.x : rhs.x, lhs.y > rhs.y ? lhs.y : rhs.y,
                       lhs.z > rhs.z ? lhs.z : rhs.z};
}

/// Component-wise clamp of `value` into [`lo`, `hi`]. `lo` must not exceed `hi` per component.
[[nodiscard]] constexpr Vector3iUVE ClampUVE(const Vector3iUVE& value, const Vector3iUVE& lo,
                                            const Vector3iUVE& hi) noexcept {
    return MaxUVE(lo, MinUVE(value, hi));
}

/// Converts to float space. Magnitudes past 2^24 round to the nearest representable float.
[[nodiscard]] constexpr Vector3UVE ToVector3UVE(const Vector3iUVE& v) noexcept {
    return Vector3UVE{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// Formats `vector` as `"(x, y, z)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const Vector3iUVE& vector);

} // namespace UVE::Math
