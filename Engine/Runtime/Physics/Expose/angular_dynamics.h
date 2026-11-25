// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include "vector3.h"

namespace UVE::Physics {

/// Returns the diagonal inverse inertia tensor for a box of `halfExtents` and `mass`.
/// Finite box arithmetic is widened before each result is narrowed to a finite positive float;
/// constructible large boxes remain representable when their public inverse-inertia components do
/// so. The result is represented by its diagonal vector because this bounded slice has no rotation
/// integration or gyroscopic coupling; zero mass returns zero inverse inertia.
[[nodiscard]] std::optional<Math::Vector3> ComputeBoxInverseInertia(
    float mass, Math::Vector3 halfExtents) noexcept;

/// Integrates one copied angular-velocity value under a caller-supplied torque for one finite
/// timestep. The diagonal inverse inertia is caller-owned state; no ECS or body persistence is
/// accessed or mutated by this helper.
[[nodiscard]] std::optional<Math::Vector3> IntegrateAngularVelocity(
    Math::Vector3 angularVelocity, Math::Vector3 torque,
    Math::Vector3 inverseInertia, float deltaTimeSeconds) noexcept;

/// Applies one copied angular impulse through a diagonal inverse inertia tensor.
[[nodiscard]] std::optional<Math::Vector3> ApplyAngularImpulse(
    Math::Vector3 angularVelocity, Math::Vector3 angularImpulse,
    Math::Vector3 inverseInertia) noexcept;

/// Returns the diagonal-inertia gyroscopic torque term `angularVelocity x (inertia * angularVelocity)`.
/// The caller decides whether and how to subtract this term during integration.
[[nodiscard]] std::optional<Math::Vector3> EvaluateGyroscopicTorque(
    Math::Vector3 angularVelocity, Math::Vector3 inverseInertia) noexcept;

} // namespace UVE::Physics
