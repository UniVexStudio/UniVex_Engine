// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <string>

namespace UVE::Math {

/// A 2-component single-precision vector. InputSystemUVE (Part 7.7, Increment 17) — mouse
/// position/delta — was the first real consumer; the scalar-utilities increment completed it with
/// the same dot/length/normalize/finite surface Vector3UVE carries.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector2UVE {
    float x = 0.0F;
    float y = 0.0F;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector2UVE operator+(const Vector2UVE& lhs, const Vector2UVE& rhs) noexcept {
    return Vector2UVE{lhs.x + rhs.x, lhs.y + rhs.y};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector2UVE operator-(const Vector2UVE& lhs, const Vector2UVE& rhs) noexcept {
    return Vector2UVE{lhs.x - rhs.x, lhs.y - rhs.y};
}

/// Both components scaled by `scale`.
[[nodiscard]] constexpr Vector2UVE operator*(const Vector2UVE& lhs, const float scale) noexcept {
    return Vector2UVE{lhs.x * scale, lhs.y * scale};
}

[[nodiscard]] constexpr bool operator==(const Vector2UVE& lhs, const Vector2UVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] constexpr bool operator!=(const Vector2UVE& lhs, const Vector2UVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product. The double-precision fallback mirrors Vector3UVE: an overflowing float sum is
/// recomputed in double rather than returned as infinity.
[[nodiscard]] constexpr float DotUVE(const Vector2UVE& lhs, const Vector2UVE& rhs) noexcept {
    const float floatResult = lhs.x * rhs.x + lhs.y * rhs.y;
    const auto IsFiniteFloatUVE = [](const float value) constexpr {
        return value == value && value <= std::numeric_limits<float>::max() &&
               value >= -std::numeric_limits<float>::max();
    };
    if (IsFiniteFloatUVE(floatResult)) {
        return floatResult;
    }
    return static_cast<float>(static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
                              static_cast<double>(lhs.y) * static_cast<double>(rhs.y));
}

/// Squared length — cheaper than LengthUVE() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr float LengthSquaredUVE(const Vector2UVE& v) noexcept {
    return DotUVE(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot.
[[nodiscard]] float LengthUVE(const Vector2UVE& v) noexcept;

/// Returns `v` scaled to unit length, under Vector3UVE's contract: `v` must not be the zero
/// vector — callers that cannot guarantee this must check `LengthSquaredUVE(v)` first.
[[nodiscard]] Vector2UVE NormalizeUVE(const Vector2UVE& v) noexcept;

/// Returns whether every vector component is finite.
[[nodiscard]] bool IsFiniteUVE(const Vector2UVE& value) noexcept;

/// Formats `vector` as `"(x, y)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const Vector2UVE& vector);

} // namespace UVE::Math
