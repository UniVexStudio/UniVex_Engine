// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vector3.h"
#include "angular_dynamics.h"

#include <cmath>

namespace UVE::Physics {
namespace {

[[nodiscard]] Math::Vector3 MultiplyComponents(
    const Math::Vector3& lhs, const Math::Vector3& rhs) noexcept {
    return Math::Vector3{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

} // namespace

std::optional<Math::Vector3> ComputeBoxInverseInertia(
    const float mass, const Math::Vector3 halfExtents) noexcept {
    if (!std::isfinite(mass) || mass < 0.0F || !Math::IsFinite(halfExtents) ||
        halfExtents.x <= 0.0F || halfExtents.y <= 0.0F || halfExtents.z <= 0.0F) {
        return std::nullopt;
    }
    if (mass == 0.0F) {
        return Math::Vector3{};
    }

    const double doubleMass = static_cast<double>(mass);
    const double xSquared = static_cast<double>(halfExtents.x) * static_cast<double>(halfExtents.x);
    const double ySquared = static_cast<double>(halfExtents.y) * static_cast<double>(halfExtents.y);
    const double zSquared = static_cast<double>(halfExtents.z) * static_cast<double>(halfExtents.z);
    const double inverseMass = 1.0 / doubleMass;
    if (!std::isfinite(xSquared) || !std::isfinite(ySquared) || !std::isfinite(zSquared) ||
        !std::isfinite(inverseMass)) {
        return std::nullopt;
    }
    const double inertiaX = (doubleMass / 3.0) * (ySquared + zSquared);
    const double inertiaY = (doubleMass / 3.0) * (xSquared + zSquared);
    const double inertiaZ = (doubleMass / 3.0) * (xSquared + ySquared);
    const double inverseInertiaX = 1.0 / inertiaX;
    const double inverseInertiaY = 1.0 / inertiaY;
    const double inverseInertiaZ = 1.0 / inertiaZ;
    const Math::Vector3 result{
        static_cast<float>(inverseInertiaX),
        static_cast<float>(inverseInertiaY),
        static_cast<float>(inverseInertiaZ),
    };
    if (!std::isfinite(inertiaX) || !std::isfinite(inertiaY) || !std::isfinite(inertiaZ) ||
        inertiaX <= 0.0 || inertiaY <= 0.0 || inertiaZ <= 0.0 ||
        !std::isfinite(inverseInertiaX) || !std::isfinite(inverseInertiaY) ||
        !std::isfinite(inverseInertiaZ) || !Math::IsFinite(result) ||
        result.x <= 0.0F || result.y <= 0.0F || result.z <= 0.0F) {
        return std::nullopt;
    }
    return result;
}

std::optional<Math::Vector3> IntegrateAngularVelocity(
    const Math::Vector3 angularVelocity, const Math::Vector3 torque,
    const Math::Vector3 inverseInertia, const float deltaTimeSeconds) noexcept {
    if (!Math::IsFinite(angularVelocity) || !Math::IsFinite(torque) ||
        !Math::IsFinite(inverseInertia) || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0F ||
        inverseInertia.x < 0.0F || inverseInertia.y < 0.0F || inverseInertia.z < 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 angularAcceleration = MultiplyComponents(torque, inverseInertia);
    const Math::Vector3 result = angularVelocity + angularAcceleration * deltaTimeSeconds;
    return Math::IsFinite(result) ? std::optional<Math::Vector3>{result} : std::nullopt;
}

std::optional<Math::Vector3> ApplyAngularImpulse(
    const Math::Vector3 angularVelocity, const Math::Vector3 angularImpulse,
    const Math::Vector3 inverseInertia) noexcept {
    if (!Math::IsFinite(angularVelocity) || !Math::IsFinite(angularImpulse) ||
        !Math::IsFinite(inverseInertia) || inverseInertia.x < 0.0F || inverseInertia.y < 0.0F ||
        inverseInertia.z < 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 result = angularVelocity + MultiplyComponents(angularImpulse, inverseInertia);
    return Math::IsFinite(result) ? std::optional<Math::Vector3>{result} : std::nullopt;
}

std::optional<Math::Vector3> EvaluateGyroscopicTorque(
    const Math::Vector3 angularVelocity, const Math::Vector3 inverseInertia) noexcept {
    if (!Math::IsFinite(angularVelocity) || !Math::IsFinite(inverseInertia) ||
        inverseInertia.x < 0.0F || inverseInertia.y < 0.0F || inverseInertia.z < 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 angularMomentum{
        inverseInertia.x > 0.0F ? angularVelocity.x / inverseInertia.x : 0.0F,
        inverseInertia.y > 0.0F ? angularVelocity.y / inverseInertia.y : 0.0F,
        inverseInertia.z > 0.0F ? angularVelocity.z / inverseInertia.z : 0.0F};
    const Math::Vector3 result{
        angularVelocity.y * angularMomentum.z - angularVelocity.z * angularMomentum.y,
        angularVelocity.z * angularMomentum.x - angularVelocity.x * angularMomentum.z,
        angularVelocity.x * angularMomentum.y - angularVelocity.y * angularMomentum.x};
    return Math::IsFinite(result) ? std::optional<Math::Vector3>{result} : std::nullopt;
}

} // namespace UVE::Physics
