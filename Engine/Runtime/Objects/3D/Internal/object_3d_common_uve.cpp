// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

bool IsBounded3DObjectStringUVE(const std::string& value, const bool allowEmpty) noexcept {
    return (allowEmpty || !value.empty()) && value.size() <= kMaximum3DObjectStringLengthUVE &&
           value.find('\0') == std::string::npos;
}

bool IsFinite3DObjectVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsFinite3DObjectQuaternionUVE(const Math::QuaternionUVE& value) noexcept {
    return Math::IsFiniteUVE(value) && std::isfinite(Math::LengthSquaredUVE(value));
}

bool IsPositive3DObjectVectorUVE(const Math::Vector3UVE& value, const float minimumExclusive) noexcept {
    return IsFinite3DObjectVectorUVE(value) && std::isfinite(minimumExclusive) &&
           value.x > minimumExclusive && value.y > minimumExclusive && value.z > minimumExclusive;
}

bool IsNormalizable3DObjectQuaternionUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE normalized{};
    return IsFinite3DObjectQuaternionUVE(value) && Math::TryNormalizeUVE(value, normalized);
}

bool Is3DObjectLayerMaskNonZeroUVE(const std::uint32_t mask) noexcept {
    return mask != 0U;
}

std::optional<Math::Vector3UVE> Compose3DObjectWorldPointUVE(
    const Math::Vector3UVE& worldPosition, const Math::QuaternionUVE& worldRotation,
    const Math::Vector3UVE& localOffset) noexcept {
    if (!IsFinite3DObjectVectorUVE(worldPosition) || !IsFinite3DObjectVectorUVE(localOffset) ||
        !IsFinite3DObjectQuaternionUVE(worldRotation)) {
        return std::nullopt;
    }
    Math::QuaternionUVE normalizedRotation{};
    if (!Math::TryNormalizeUVE(worldRotation, normalizedRotation)) {
        return std::nullopt;
    }
    return worldPosition + Math::RotateVectorUVE(normalizedRotation, localOffset);
}

std::optional<Math::QuaternionUVE> Compose3DObjectWorldRotationUVE(
    const Math::QuaternionUVE& worldRotation, const Math::QuaternionUVE& localRotation) noexcept {
    if (!IsFinite3DObjectQuaternionUVE(worldRotation) || !IsFinite3DObjectQuaternionUVE(localRotation)) {
        return std::nullopt;
    }
    Math::QuaternionUVE normalizedWorld{};
    if (!Math::TryNormalizeUVE(worldRotation, normalizedWorld)) {
        return std::nullopt;
    }
    const Math::QuaternionUVE product = Math::MultiplyUVE(normalizedWorld, localRotation);
    Math::QuaternionUVE normalizedProduct{};
    if (!Math::TryNormalizeUVE(product, normalizedProduct)) {
        return std::nullopt;
    }
    return normalizedProduct;
}

} // namespace UVE::Scene
