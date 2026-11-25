// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "object_3d_common.h"

namespace UVE::Scene {

bool IsBounded3DObjectString(const std::string& value, const bool allowEmpty) noexcept {
    return (allowEmpty || !value.empty()) && value.size() <= kMaximum3DObjectStringLength &&
           value.find('\0') == std::string::npos;
}

bool IsFinite3DObjectVector(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsFinite3DObjectQuaternion(const Math::Quaternion& value) noexcept {
    return Math::IsFinite(value) && std::isfinite(Math::LengthSquared(value));
}

bool IsPositive3DObjectVector(const Math::Vector3& value, const float minimumExclusive) noexcept {
    return IsFinite3DObjectVector(value) && std::isfinite(minimumExclusive) &&
           value.x > minimumExclusive && value.y > minimumExclusive && value.z > minimumExclusive;
}

bool IsNormalizable3DObjectQuaternion(const Math::Quaternion& value) noexcept {
    Math::Quaternion normalized{};
    return IsFinite3DObjectQuaternion(value) && Math::TryNormalize(value, normalized);
}

bool Is3DObjectLayerMaskNonZero(const std::uint32_t mask) noexcept {
    return mask != 0U;
}

std::optional<Math::Vector3> Compose3DObjectWorldPoint(
    const Math::Vector3& worldPosition, const Math::Quaternion& worldRotation,
    const Math::Vector3& localOffset) noexcept {
    if (!IsFinite3DObjectVector(worldPosition) || !IsFinite3DObjectVector(localOffset) ||
        !IsFinite3DObjectQuaternion(worldRotation)) {
        return std::nullopt;
    }
    Math::Quaternion normalizedRotation{};
    if (!Math::TryNormalize(worldRotation, normalizedRotation)) {
        return std::nullopt;
    }
    return worldPosition + Math::RotateVector(normalizedRotation, localOffset);
}

std::optional<Math::Quaternion> Compose3DObjectWorldRotation(
    const Math::Quaternion& worldRotation, const Math::Quaternion& localRotation) noexcept {
    if (!IsFinite3DObjectQuaternion(worldRotation) || !IsFinite3DObjectQuaternion(localRotation)) {
        return std::nullopt;
    }
    Math::Quaternion normalizedWorld{};
    if (!Math::TryNormalize(worldRotation, normalizedWorld)) {
        return std::nullopt;
    }
    const Math::Quaternion product = Math::Multiply(normalizedWorld, localRotation);
    Math::Quaternion normalizedProduct{};
    if (!Math::TryNormalize(product, normalizedProduct)) {
        return std::nullopt;
    }
    return normalizedProduct;
}

} // namespace UVE::Scene
