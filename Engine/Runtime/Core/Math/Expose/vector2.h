// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <string>

namespace UVE::Math {

/// A 2-component single-precision vector. InputSystem (Part 7.7, Increment 17) — mouse
/// position/delta — was the first real consumer; the scalar-utilities increment completed it with
/// the same dot/length/normalize/finite surface Vector3 carries.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector2 {
    float x = 0.0F;
    float y = 0.0F;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector2 operator+(const Vector2& lhs, const Vector2& rhs) noexcept {
    return Vector2{lhs.x + rhs.x, lhs.y + rhs.y};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector2 operator-(const Vector2& lhs, const Vector2& rhs) noexcept {
    return Vector2{lhs.x - rhs.x, lhs.y - rhs.y};
}

/// Both components scaled by `scale`.
[[nodiscard]] constexpr Vector2 operator*(const Vector2& lhs, const float scale) noexcept {
    return Vector2{lhs.x * scale, lhs.y * scale};
}

[[nodiscard]] constexpr bool operator==(const Vector2& lhs, const Vector2& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] constexpr bool operator!=(const Vector2& lhs, const Vector2& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product. The double-precision fallback mirrors Vector3: an overflowing float sum is
/// recomputed in double rather than returned as infinity.
[[nodiscard]] constexpr float Dot(const Vector2& lhs, const Vector2& rhs) noexcept {
    const float floatResult = lhs.x * rhs.x + lhs.y * rhs.y;
    const auto IsFiniteFloat = [](const float value) constexpr {
        return value == value && value <= std::numeric_limits<float>::max() &&
               value >= -std::numeric_limits<float>::max();
    };
    if (IsFiniteFloat(floatResult)) {
        return floatResult;
    }
    return static_cast<float>(static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
                              static_cast<double>(lhs.y) * static_cast<double>(rhs.y));
}

/// Squared length — cheaper than Length() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr float LengthSquared(const Vector2& v) noexcept {
    return Dot(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot.
[[nodiscard]] float Length(const Vector2& v) noexcept;

/// Returns `v` scaled to unit length, under Vector3's contract: `v` must not be the zero
/// vector — callers that cannot guarantee this must check `LengthSquared(v)` first.
[[nodiscard]] Vector2 Normalize(const Vector2& v) noexcept;

/// Returns whether every vector component is finite.
[[nodiscard]] bool IsFinite(const Vector2& value) noexcept;

/// Formats `vector` as `"(x, y)"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Vector2& vector);

} // namespace UVE::Math
