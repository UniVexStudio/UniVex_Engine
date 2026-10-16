// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximum3DObjectStringLengthUVE = 256U;

[[nodiscard]] bool IsBounded3DObjectStringUVE(const std::string& value, bool allowEmpty = true) noexcept;
[[nodiscard]] bool IsFinite3DObjectVectorUVE(const Math::Vector3UVE& value) noexcept;
[[nodiscard]] bool IsFinite3DObjectQuaternionUVE(const Math::QuaternionUVE& value) noexcept;

/// True when every component of `value` is finite and strictly greater than `minimumExclusive`.
[[nodiscard]] bool IsPositive3DObjectVectorUVE(
    const Math::Vector3UVE& value, float minimumExclusive = 0.0F) noexcept;

/// True when `value` is finite and its squared length is positive and normalizable.
[[nodiscard]] bool IsNormalizable3DObjectQuaternionUVE(const Math::QuaternionUVE& value) noexcept;

/// True when `mask` has at least one bit set.
[[nodiscard]] bool Is3DObjectLayerMaskNonZeroUVE(std::uint32_t mask) noexcept;

/// Transforms `localOffset` by `worldPosition` and `worldRotation` (ignoring scale, as markers,
/// spawn points, and attachment offsets require). Returns `std::nullopt` if any input is
/// non-finite or `worldRotation` cannot be normalized.
[[nodiscard]] std::optional<Math::Vector3UVE> Compose3DObjectWorldPointUVE(
    const Math::Vector3UVE& worldPosition, const Math::QuaternionUVE& worldRotation,
    const Math::Vector3UVE& localOffset) noexcept;

/// Composes `worldRotation * localRotation` and normalizes the result. Returns `std::nullopt`
/// if either quaternion is non-finite or degenerate.
[[nodiscard]] std::optional<Math::QuaternionUVE> Compose3DObjectWorldRotationUVE(
    const Math::QuaternionUVE& worldRotation, const Math::QuaternionUVE& localRotation) noexcept;

} // namespace UVE::Scene
