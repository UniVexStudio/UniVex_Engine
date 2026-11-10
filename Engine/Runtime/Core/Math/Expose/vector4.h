// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <string>

namespace UVE::Math {

/// A 4-component single-precision vector, used for shader-facing four-float quantities
/// (homogeneous positions, tangents with handedness, and — once Tier 1.3 lands — colors).
/// Mirrors Vector3UVE's shape and conventions exactly (component-wise arithmetic, the same
/// overflow-safe DotUVE(), Length/Normalize/ToString/IsFinite in the .cpp); the one deliberate
/// absence is CrossUVE(), which has no 4D meaning.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector4UVE {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector4UVE operator+(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    return Vector4UVE{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector4UVE operator-(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    return Vector4UVE{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w};
}

/// Unary negation.
[[nodiscard]] constexpr Vector4UVE operator-(const Vector4UVE& v) noexcept {
    return Vector4UVE{-v.x, -v.y, -v.z, -v.w};
}

/// Component-wise multiplication.
[[nodiscard]] constexpr Vector4UVE operator*(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    return Vector4UVE{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w};
}

/// Scalar multiplication.
[[nodiscard]] constexpr Vector4UVE operator*(const Vector4UVE& v, float scalar) noexcept {
    return Vector4UVE{v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar};
}

constexpr Vector4UVE& operator+=(Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    lhs = lhs + rhs;
    return lhs;
}

constexpr Vector4UVE& operator-=(Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    lhs = lhs - rhs;
    return lhs;
}

constexpr Vector4UVE& operator*=(Vector4UVE& v, float scalar) noexcept {
    v = v * scalar;
    return v;
}

[[nodiscard]] constexpr bool operator==(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] constexpr bool operator!=(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product. Same overflow-safe shape as Vector3UVE's DotUVE(): the float sum is returned
/// when finite, otherwise the sum is recomputed in double precision.
[[nodiscard]] constexpr float DotUVE(const Vector4UVE& lhs, const Vector4UVE& rhs) noexcept {
    const float floatResult = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    const auto IsFiniteFloatUVE = [](const float value) constexpr {
        return value == value && value <= std::numeric_limits<float>::max() &&
               value >= -std::numeric_limits<float>::max();
    };
    if (IsFiniteFloatUVE(floatResult)) {
        return floatResult;
    }
    return static_cast<float>(static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
                              static_cast<double>(lhs.y) * static_cast<double>(rhs.y) +
                              static_cast<double>(lhs.z) * static_cast<double>(rhs.z) +
                              static_cast<double>(lhs.w) * static_cast<double>(rhs.w));
}

/// Squared length — cheaper than LengthUVE() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr float LengthSquaredUVE(const Vector4UVE& v) noexcept {
    return DotUVE(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot.
[[nodiscard]] float LengthUVE(const Vector4UVE& v) noexcept;

/// Returns `v` scaled to unit length. Same contract as Vector3UVE's NormalizeUVE(): `v` must
/// not be the zero vector (or within floating-point epsilon of it) — see that function's doc
/// comment for the rationale.
[[nodiscard]] Vector4UVE NormalizeUVE(const Vector4UVE& v) noexcept;

/// Formats `vector` as `"(x, y, z, w)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const Vector4UVE& vector);

/// Returns whether every vector component is finite.
[[nodiscard]] bool IsFiniteUVE(const Vector4UVE& value) noexcept;

} // namespace UVE::Math
