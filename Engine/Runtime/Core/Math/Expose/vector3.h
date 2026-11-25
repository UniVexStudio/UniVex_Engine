// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <limits>
#include <string>

namespace UVE::Math {

/// A 3-component single-precision vector, used engine-wide for position, scale, and any other
/// three-float quantity. Deliberately minimal: only the operations Scene/ECS's transform
/// composition actually needs today (addition, component-wise multiplication for scale
/// composition, equality). Vector2/Vector4, cross/dot products, and Length/Normalize
/// now exist as their own headers; SIMD optimization and swizzles remain a real design problem
/// for whichever future increment first needs them — not invented here.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct Vector3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

/// Component-wise addition.
[[nodiscard]] constexpr Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept {
    return Vector3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

/// Component-wise subtraction. Added for Physics (Part 7.5, Increment 15) — see the class doc
/// comment; not needed by anything before it.
[[nodiscard]] constexpr Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept {
    return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

/// Unary negation.
[[nodiscard]] constexpr Vector3 operator-(const Vector3& v) noexcept {
    return Vector3{-v.x, -v.y, -v.z};
}

/// Component-wise multiplication (used for scale composition, not a dot/cross product).
[[nodiscard]] constexpr Vector3 operator*(const Vector3& lhs, const Vector3& rhs) noexcept {
    return Vector3{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

/// Scalar multiplication.
[[nodiscard]] constexpr Vector3 operator*(const Vector3& v, float scalar) noexcept {
    return Vector3{v.x * scalar, v.y * scalar, v.z * scalar};
}

constexpr Vector3& operator+=(Vector3& lhs, const Vector3& rhs) noexcept {
    lhs = lhs + rhs;
    return lhs;
}

constexpr Vector3& operator-=(Vector3& lhs, const Vector3& rhs) noexcept {
    lhs = lhs - rhs;
    return lhs;
}

constexpr Vector3& operator*=(Vector3& v, float scalar) noexcept {
    v = v * scalar;
    return v;
}

[[nodiscard]] constexpr bool operator==(const Vector3& lhs, const Vector3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] constexpr bool operator!=(const Vector3& lhs, const Vector3& rhs) noexcept {
    return !(lhs == rhs);
}

/// Dot product.
[[nodiscard]] constexpr float Dot(const Vector3& lhs, const Vector3& rhs) noexcept {
    const float floatResult = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    const auto IsFiniteFloat = [](const float value) constexpr {
        return value == value && value <= std::numeric_limits<float>::max() &&
               value >= -std::numeric_limits<float>::max();
    };
    if (IsFiniteFloat(floatResult)) {
        return floatResult;
    }
    return static_cast<float>(static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
                              static_cast<double>(lhs.y) * static_cast<double>(rhs.y) +
                              static_cast<double>(lhs.z) * static_cast<double>(rhs.z));
}

/// Cross product.
[[nodiscard]] constexpr Vector3 Cross(const Vector3& lhs, const Vector3& rhs) noexcept {
    return Vector3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

/// Squared length — cheaper than Length() when only comparing magnitudes (no sqrt).
[[nodiscard]] constexpr float LengthSquared(const Vector3& v) noexcept {
    return Dot(v, v);
}

/// Euclidean length. Non-constexpr: uses std::sqrt.
[[nodiscard]] float Length(const Vector3& v) noexcept;

/// Returns `v` scaled to unit length. Contract: `v` must not be the zero vector (or within
/// floating-point epsilon of it) — callers that cannot guarantee this must check
/// `LengthSquared(v)` first; Normalize() does not itself guard against zero and scales
/// finite inputs before measuring length so large nonzero vectors do not overflow that intermediate
/// (matching this codebase's "don't validate what the caller must already ensure" convention for
/// value-type math, e.g. Aabb::Transform assumes an affine matrix without checking).
[[nodiscard]] Vector3 Normalize(const Vector3& v) noexcept;

/// Formats `vector` as `"(x, y, z)"`, for logging/debugging.
[[nodiscard]] std::string ToString(const Vector3& vector);

/// Returns whether every vector component is finite.
[[nodiscard]] bool IsFinite(const Vector3& value) noexcept;

} // namespace UVE::Math
