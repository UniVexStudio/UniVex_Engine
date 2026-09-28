// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

namespace UVE::Math {

/// A 2-component single-precision vector. InputSystemUVE (Part 7.7, Increment 17) — mouse
/// position/delta — is the first real consumer, so this stays deliberately minimal: addition,
/// subtraction, scaling (for blend-space smoothing) and equality. Dot/length/normalize are left
/// to whichever increment first needs them.
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

/// Formats `vector` as `"(x, y)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const Vector2UVE& vector);

} // namespace UVE::Math
