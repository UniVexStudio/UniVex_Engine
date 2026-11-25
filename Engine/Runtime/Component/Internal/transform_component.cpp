// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "transform_component.h"

#include <cmath>

namespace UVE::Scene {

bool TrySyncRotationFromEuler(TransformComponent& transform) noexcept {
    if (transform.rotationEditMode != RotationEditMode::Euler) {
        // Quaternion mode: the quaternion is the authored value. Rebuilding it from a derived
        // Euler view is exactly the round-trip this design exists to avoid, so this reports
        // success and changes nothing.
        return true;
    }
    Math::Quaternion rotation{};
    if (!Math::TryMakeEulerOrdered(transform.localEulerRadians, transform.eulerOrder, rotation)) {
        return false;
    }
    transform.localRotation = rotation;
    return true;
}

bool TryGetDisplayEuler(const TransformComponent& transform, Math::Vector3& outRadians) noexcept {
    if (transform.rotationEditMode == RotationEditMode::Euler) {
        // Handed straight back. No extraction means no gimbal collapse, no wrap to +/-180, and no
        // drift - the author sees the number they typed, including 370 degrees if that is what
        // they meant.
        outRadians = transform.localEulerRadians;
        return true;
    }
    return Math::TryToEulerOrdered(transform.localRotation, transform.eulerOrder, outRadians);
}

[[nodiscard]] bool IsTransformComponentValid(const TransformComponent& transform) noexcept {
    const auto isFiniteVector = [](const Math::Vector3& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!isFiniteVector(transform.localPosition) || !isFiniteVector(transform.localScale) ||
        !Math::IsFinite(transform.localRotation)) {
        return false;
    }
    const float rotationLengthSquared = Math::LengthSquared(transform.localRotation);
    return std::isfinite(rotationLengthSquared) &&
           std::abs(rotationLengthSquared - 1.0F) <= 1.0e-3F;
}

} // namespace UVE::Scene
