// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <string>

namespace UVE::Math {

/// A 4-component single-precision vector, used for shader-facing four-float quantities
/// (homogeneous positions, tangents with handedness, and — once Tier 1.3 lands — colors).
/// Mirrors Vector3's shape and conventions exactly (component-wise arithmetic, the same
/// overflow-safe Dot(), Length/Normalize/ToString/IsFinite in the .cpp); the one deliberate
/// absence is Cross(), which has no 4D meaning.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector4 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector4 operator+(const Vector4& lhs, const Vector4& rhs) noexcept {
    return Vector4{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

/// Component-wise subtraction.
[[nodiscard]] constexpr Vector4 operator-(const Vector4& lhs, const Vector4& rhs) noexcept {
    return Vector4{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w};
}

/// Unary negation.
[[nodiscard]] constexpr Vector4 operator-(const Vector4& v) noexcept {
    return Vector4{-v.x, -v.y, -v.z, -v.w};
}

/// Component-wise multiplication.
[[nodiscard]] constexpr Vector4 operator*(const Vector4& lhs, const Vector4& rhs) noexcept {
    return Vector4{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w};
}

/// Scalar multiplication.
[[nodiscard]] constexpr Vector4 operator*(const Vector4& v, float scalar) noexcept {
    return Vector4{v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar};
}

constexpr Vector4& operator+=(Vector4& lhs, const Vector4& rhs) noexcept {
    lhs = lhs + rhs;
    return lhs;
}

constexpr Vector4& operator-=(Vector4& lhs, const Vector4& rhs) noexcept {
    lhs = lhs - rhs;
    return lhs;
}

constexpr Vector4& operator*=(Vector4& v, float scalar) noexcept {
    v = v * scalar;
    return v;
}

[[nodiscard]] constexpr bool operator==(const Vector4& lhs, const Vector4& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] constexpr bool operator!=(const Vector4& lhs, const Vector4& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product. Same overflow-safe shape as Vector3's Dot(): the float sum is returned
/// when finite, otherwise the sum is recomputed in double precision.
[[nodiscard]] constexpr float Dot(const Vector4& lhs, const Vector4& rhs) noexcept {
    const float floatResult = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    const auto IsFiniteFloat = [](const float value) constexpr {
        return value == value && value <= std::numeric_limits<float>::max() &&
               value >= -std::numeric_limits<float>::max();
    };
    if (IsFiniteFloat(floatResult)) {
        return floatResult;
    }
    return static_cast<float>(static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
                              static_cast<double>(lhs.y) * static_cast<double>(rhs.y) +
                              static_cast<double>(lhs.z) * static_cast<double>(rhs.z) +
                              static_cast<double>(lhs.w) * static_cast<double>(rhs.w));
}

/// Squared length — cheaper than Length() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr float LengthSquared(const Vector4& v) noexcept {
    return Dot(v, v);
}

/// Euclidean length. Non-constexpr: uses std::hypot.
[[nodiscard]] float Length(const Vector4& v) noexcept;

/// Returns `v` scaled to unit length. Same contract as Vector3's Normalize(): `v` must
/// not be the zero vector (or within floating-point epsilon of it) — see that function's doc
/// comment for the rationale.
[[nodiscard]] Vector4 Normalize(const Vector4& v) noexcept;

/// Formats `vector` as `"(x, y, z, w)"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Vector4& vector);

/// Returns whether every vector component is finite.
[[nodiscard]] bool IsFinite(const Vector4& value) noexcept;

} // namespace UVE::Math
