// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>

#include "entity.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct VehicleGroundSampleUVE final {
    bool hit = false;
    float hitDistance = 0.0F;
    Math::Vector3UVE hitNormal{0.0F, 1.0F, 0.0F};
    float surfaceFriction = 1.0F;
};

using VehicleRaycastCallbackUVE = std::function<VehicleGroundSampleUVE(
    const Math::Vector3UVE& origin, const Math::Vector3UVE& direction, float maxDistance)>;

/// Raycast-vehicle chassis component with 6-DOF linear/angular dynamics, Ackermann steering,
/// lateral tire grip, anti-roll stiffness, and reverse-gear handling.
struct VehicleBody3DComponentUVE final {
    float mass = 1200.0F;
    float engineForce = 0.0F;
    float brakeForce = 0.0F;
    float steeringAngleRadians = 0.0F;
    Math::Vector3UVE linearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE angularVelocity{0.0F, 0.0F, 0.0F};
    float wheelbaseMetres = 2.6F;
    float trackWidthMetres = 1.6F;
    float lateralGrip = 6.0F;
    float rollingResistance = 0.015F;
    float antiRollStiffness = 1500.0F;
    bool reverseGear = false;
    bool enabled = true;

    // Runtime state, never persisted:
    float currentSpeedMetresPerSecond = 0.0F;
    std::uint32_t groundedWheelCount = 0U;

    [[nodiscard]] bool operator==(const VehicleBody3DComponentUVE& other) const noexcept {
        return mass == other.mass && engineForce == other.engineForce &&
               brakeForce == other.brakeForce &&
               steeringAngleRadians == other.steeringAngleRadians &&
               linearVelocity == other.linearVelocity &&
               angularVelocity == other.angularVelocity &&
               wheelbaseMetres == other.wheelbaseMetres &&
               trackWidthMetres == other.trackWidthMetres &&
               lateralGrip == other.lateralGrip &&
               rollingResistance == other.rollingResistance &&
               antiRollStiffness == other.antiRollStiffness &&
               reverseGear == other.reverseGear && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsVehicleBody3DComponentValidUVE(
    const VehicleBody3DComponentUVE& value) noexcept;

[[nodiscard]] VehicleBody3DComponentUVE SanitizeVehicleBody3DComponentUVE(
    const VehicleBody3DComponentUVE& value) noexcept;

struct VehicleBody3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "VehicleBody3D";
    VehicleBody3DComponentUVE vehicle{};
};

[[nodiscard]] bool IsVehicleBody3DObjectDefinitionValidUVE(
    const VehicleBody3DObjectDefinitionUVE& value) noexcept;

void ApplyVehicleBody3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const VehicleBody3DObjectDefinitionUVE& value);

/// Raycast-vehicle wheel component (`wheelRadius`, `suspensionRestLength`, `suspensionStiffness`,
/// `suspensionDamping`, `isSteerable`, `isPowered`, `isBraking`) linked to `vehicleBodyEntity`.
struct VehicleWheel3DComponentUVE final {
    EntityUVE vehicleBodyEntity = kInvalidEntityUVE;
    Math::Vector3UVE localMountOffset{0.0F, 0.0F, 0.0F};
    float wheelRadius = 0.35F;
    float suspensionRestLength = 0.3F;
    float suspensionStiffness = 35.0F;
    float suspensionDamping = 4.5F;
    float maxSteerAngleRadians = 0.65F;
    bool isSteerable = false;
    bool isPowered = true;
    bool isBraking = true;
    bool enabled = true;

    // Runtime state, never persisted:
    bool inContact = true;
    float currentSuspensionLength = 0.3F;
    float previousSuspensionLength = 0.3F;
    float suspensionForce = 0.0F;
    float rotationAngleRadians = 0.0F;
    float currentSteerAngleRadians = 0.0F;
    float slipRatio = 0.0F;

    [[nodiscard]] bool operator==(const VehicleWheel3DComponentUVE& other) const noexcept {
        return vehicleBodyEntity == other.vehicleBodyEntity &&
               localMountOffset == other.localMountOffset && wheelRadius == other.wheelRadius &&
               suspensionRestLength == other.suspensionRestLength &&
               suspensionStiffness == other.suspensionStiffness &&
               suspensionDamping == other.suspensionDamping &&
               maxSteerAngleRadians == other.maxSteerAngleRadians &&
               isSteerable == other.isSteerable && isPowered == other.isPowered &&
               isBraking == other.isBraking && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsVehicleWheel3DComponentValidUVE(
    const VehicleWheel3DComponentUVE& value) noexcept;

[[nodiscard]] VehicleWheel3DComponentUVE SanitizeVehicleWheel3DComponentUVE(
    const VehicleWheel3DComponentUVE& value) noexcept;

struct VehicleWheel3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "VehicleWheel3D";
    VehicleWheel3DComponentUVE wheel{};
};

[[nodiscard]] bool IsVehicleWheel3DObjectDefinitionValidUVE(
    const VehicleWheel3DObjectDefinitionUVE& value) noexcept;

void ApplyVehicleWheel3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const VehicleWheel3DObjectDefinitionUVE& value);

/// Computes left/right front wheel steering angles using Ackermann geometry.
[[nodiscard]] std::pair<float, float> EvaluateAckermannSteeringUVE(
    float steerAngleRadians, float wheelbaseMetres, float trackWidthMetres) noexcept;

/// Computes spring-damper suspension force for a wheel given `compressionMetres` and
/// `compressionVelocityMetresPerSecond`.
[[nodiscard]] float EvaluateVehicleWheelSuspensionForceUVE(
    const VehicleWheel3DComponentUVE& wheel, float compressionMetres,
    float compressionVelocityMetresPerSecond) noexcept;

/// Steps all enabled `VehicleBody3DComponentUVE` and `VehicleWheel3DComponentUVE` entities by
/// `deltaTimeSeconds` using optional per-wheel ground raycasting. Returns vehicles stepped.
std::size_t UpdateVehicle3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds,
                                      const VehicleRaycastCallbackUVE& groundRaycaster = {});

} // namespace UVE::Scene
