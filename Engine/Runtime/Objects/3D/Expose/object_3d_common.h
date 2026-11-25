// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximum3DObjectStringLength = 256U;

[[nodiscard]] bool IsBounded3DObjectString(const std::string& value, bool allowEmpty = true) noexcept;
[[nodiscard]] bool IsFinite3DObjectVector(const Math::Vector3& value) noexcept;
[[nodiscard]] bool IsFinite3DObjectQuaternion(const Math::Quaternion& value) noexcept;

/// True when every component of `value` is finite and strictly greater than `minimumExclusive`.
[[nodiscard]] bool IsPositive3DObjectVector(
    const Math::Vector3& value, float minimumExclusive = 0.0F) noexcept;

/// True when `value` is finite and its squared length is positive and normalizable.
[[nodiscard]] bool IsNormalizable3DObjectQuaternion(const Math::Quaternion& value) noexcept;

/// True when `mask` has at least one bit set.
[[nodiscard]] bool Is3DObjectLayerMaskNonZero(std::uint32_t mask) noexcept;

/// Transforms `localOffset` by `worldPosition` and `worldRotation` (ignoring scale, as markers,
/// spawn points, and attachment offsets require). Returns `std::nullopt` if any input is
/// non-finite or `worldRotation` cannot be normalized.
[[nodiscard]] std::optional<Math::Vector3> Compose3DObjectWorldPoint(
    const Math::Vector3& worldPosition, const Math::Quaternion& worldRotation,
    const Math::Vector3& localOffset) noexcept;

/// Composes `worldRotation * localRotation` and normalizes the result. Returns `std::nullopt`
/// if either quaternion is non-finite or degenerate.
[[nodiscard]] std::optional<Math::Quaternion> Compose3DObjectWorldRotation(
    const Math::Quaternion& worldRotation, const Math::Quaternion& localRotation) noexcept;

} // namespace UVE::Scene
