// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <algorithm>
#include <concepts>
#include <numbers>

namespace UVE::Math {

/// Single-precision pi: the engine's working precision is float, so this is the constant most
/// call sites want. Sourced from `std::numbers` rather than a literal, so it never drifts.
inline constexpr float kPiUVE = std::numbers::pi_v<float>;
/// Double-precision pi, for the rare double-precision paths (config doubles, asset import math).
inline constexpr double kPiDoubleUVE = std::numbers::pi_v<double>;

/// Degrees to radians. `T` is floating-point only: an integer conversion would silently truncate.
template <std::floating_point T>
[[nodiscard]] constexpr T DegToRadUVE(const T degrees) noexcept {
    return degrees * (std::numbers::pi_v<T> / static_cast<T>(180));
}

/// Radians to degrees. `T` is floating-point only, as above.
template <std::floating_point T>
[[nodiscard]] constexpr T RadToDegUVE(const T radians) noexcept {
    return radians * (static_cast<T>(180) / std::numbers::pi_v<T>);
}

/// Linear interpolation: `a` at `t == 0`, `b` at `t == 1`. `t` outside [0, 1] extrapolates.
template <std::floating_point T>
[[nodiscard]] constexpr T LerpUVE(const T a, const T b, const T t) noexcept {
    return a + (b - a) * t;
}

/// `value` pinned into [`low`, `high`]. Any ordered type, mirroring `std::clamp` (which this calls).
template <typename T>
[[nodiscard]] constexpr const T& ClampUVE(const T& value, const T& low, const T& high) {
    return std::clamp(value, low, high);
}

/// Smoothstep: 0 at or below `edge0`, 1 at or above `edge1`, eased between. Contract: `edge0`
/// must be below `edge1` — the easing divides by their span, so an inverted or empty span is a
/// caller bug, matching this module's "don't validate what the caller must ensure" convention
/// (see `Vector3UVE::NormalizeUVE`).
template <std::floating_point T>
[[nodiscard]] constexpr T SmoothStepUVE(const T edge0, const T edge1, const T x) noexcept {
    const T t = ClampUVE((x - edge0) / (edge1 - edge0), static_cast<T>(0), static_cast<T>(1));
    return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
}

/// True when `a` and `b` differ by no more than `epsilon`. The tolerance is always explicit —
/// there is no default, because the right epsilon depends on the quantities compared.
template <std::floating_point T>
[[nodiscard]] constexpr bool ApproximatelyEqualUVE(const T a, const T b, const T epsilon) noexcept {
    return (a > b ? a - b : b - a) <= epsilon;
}

} // namespace UVE::Math
