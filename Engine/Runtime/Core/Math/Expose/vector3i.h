// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "vector3.h"

namespace UVE::Math {

/// A 3-component 32-bit integer vector: voxel/grid coordinates, the integer twin of Vector3.
/// Same contract as Vector2i — plain int32 arithmetic, widened int64 reductions, no normalize
/// (a unit vector is not an integer vector), ToVector3 for float-space math. No in-tree user
/// yet; it completes the integer family so the first voxel/grid feature does not invent its own.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector3i {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector3i operator+(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return Vector3i{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector3i operator-(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return Vector3i{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

/// Component-wise negation.
[[nodiscard]] constexpr Vector3i operator-(const Vector3i& v) noexcept {
    return Vector3i{-v.x, -v.y, -v.z};
}

/// All components scaled by `scale`.
[[nodiscard]] constexpr Vector3i operator*(const Vector3i& lhs, const std::int32_t scale) noexcept {
    return Vector3i{lhs.x * scale, lhs.y * scale, lhs.z * scale};
}

[[nodiscard]] constexpr bool operator==(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] constexpr bool operator!=(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product, widened to int64 before multiplying so it cannot overflow where int32 would.
[[nodiscard]] constexpr std::int64_t Dot(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return static_cast<std::int64_t>(lhs.x) * static_cast<std::int64_t>(rhs.x) +
           static_cast<std::int64_t>(lhs.y) * static_cast<std::int64_t>(rhs.y) +
           static_cast<std::int64_t>(lhs.z) * static_cast<std::int64_t>(rhs.z);
}

/// Squared length — cheaper than Length() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr std::int64_t LengthSquared(const Vector3i& v) noexcept {
    return Dot(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot over doubles.
[[nodiscard]] float Length(const Vector3i& v) noexcept;

/// Component-wise minimum.
[[nodiscard]] constexpr Vector3i Min(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return Vector3i{lhs.x < rhs.x ? lhs.x : rhs.x, lhs.y < rhs.y ? lhs.y : rhs.y,
                       lhs.z < rhs.z ? lhs.z : rhs.z};
}

/// Component-wise maximum.
[[nodiscard]] constexpr Vector3i Max(const Vector3i& lhs, const Vector3i& rhs) noexcept {
    return Vector3i{lhs.x > rhs.x ? lhs.x : rhs.x, lhs.y > rhs.y ? lhs.y : rhs.y,
                       lhs.z > rhs.z ? lhs.z : rhs.z};
}

/// Component-wise clamp of `value` into [`lo`, `hi`]. `lo` must not exceed `hi` per component.
[[nodiscard]] constexpr Vector3i Clamp(const Vector3i& value, const Vector3i& lo,
                                            const Vector3i& hi) noexcept {
    return Max(lo, Min(value, hi));
}

/// Converts to float space. Magnitudes past 2^24 round to the nearest representable float.
[[nodiscard]] constexpr Vector3 ToVector3(const Vector3i& v) noexcept {
    return Vector3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// Formats `vector` as `"(x, y, z)"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Vector3i& vector);

} // namespace UVE::Math
