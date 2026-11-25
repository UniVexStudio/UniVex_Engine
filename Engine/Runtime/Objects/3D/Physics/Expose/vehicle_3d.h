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

class IEntityManager;

struct VehicleGroundSample final {
    bool hit = false;
    float hitDistance = 0.0F;
    Math::Vector3 hitNormal{0.0F, 1.0F, 0.0F};
    float surfaceFriction = 1.0F;
};

using VehicleRaycastCallback = std::function<VehicleGroundSample(
    const Math::Vector3& origin, const Math::Vector3& direction, float maxDistance)>;

/// Raycast-vehicle chassis component with 6-DOF linear/angular dynamics, Ackermann steering,
/// lateral tire grip, anti-roll stiffness, and reverse-gear handling.
struct VehicleBody3DComponent final {
    float mass = 1200.0F;
    float engineForce = 0.0F;
    float brakeForce = 0.0F;
    float steeringAngleRadians = 0.0F;
    Math::Vector3 linearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3 angularVelocity{0.0F, 0.0F, 0.0F};
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

    [[nodiscard]] bool operator==(const VehicleBody3DComponent& other) const noexcept {
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

[[nodiscard]] bool IsVehicleBody3DComponentValid(
    const VehicleBody3DComponent& value) noexcept;

[[nodiscard]] VehicleBody3DComponent SanitizeVehicleBody3DComponent(
    const VehicleBody3DComponent& value) noexcept;

struct VehicleBody3DObjectDefinition final {
    static constexpr std::string_view defaultName = "VehicleBody3D";
    VehicleBody3DComponent vehicle{};
};

[[nodiscard]] bool IsVehicleBody3DObjectDefinitionValid(
    const VehicleBody3DObjectDefinition& value) noexcept;

void ApplyVehicleBody3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const VehicleBody3DObjectDefinition& value);

/// Raycast-vehicle wheel component (`wheelRadius`, `suspensionRestLength`, `suspensionStiffness`,
/// `suspensionDamping`, `isSteerable`, `isPowered`, `isBraking`) linked to `vehicleBodyEntity`.
struct VehicleWheel3DComponent final {
    Entity vehicleBodyEntity = kInvalidEntity;
    Math::Vector3 localMountOffset{0.0F, 0.0F, 0.0F};
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

    [[nodiscard]] bool operator==(const VehicleWheel3DComponent& other) const noexcept {
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

[[nodiscard]] bool IsVehicleWheel3DComponentValid(
    const VehicleWheel3DComponent& value) noexcept;

[[nodiscard]] VehicleWheel3DComponent SanitizeVehicleWheel3DComponent(
    const VehicleWheel3DComponent& value) noexcept;

struct VehicleWheel3DObjectDefinition final {
    static constexpr std::string_view defaultName = "VehicleWheel3D";
    VehicleWheel3DComponent wheel{};
};

[[nodiscard]] bool IsVehicleWheel3DObjectDefinitionValid(
    const VehicleWheel3DObjectDefinition& value) noexcept;

void ApplyVehicleWheel3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const VehicleWheel3DObjectDefinition& value);

/// Computes left/right front wheel steering angles using Ackermann geometry.
[[nodiscard]] std::pair<float, float> EvaluateAckermannSteering(
    float steerAngleRadians, float wheelbaseMetres, float trackWidthMetres) noexcept;

/// Computes spring-damper suspension force for a wheel given `compressionMetres` and
/// `compressionVelocityMetresPerSecond`.
[[nodiscard]] float EvaluateVehicleWheelSuspensionForce(
    const VehicleWheel3DComponent& wheel, float compressionMetres,
    float compressionVelocityMetresPerSecond) noexcept;

/// Steps all enabled `VehicleBody3DComponent` and `VehicleWheel3DComponent` entities by
/// `deltaTimeSeconds` using optional per-wheel ground raycasting. Returns vehicles stepped.
std::size_t UpdateVehicle3DObjects(IEntityManager& entityManager, float deltaTimeSeconds,
                                      const VehicleRaycastCallback& groundRaycaster = {});

} // namespace UVE::Scene
