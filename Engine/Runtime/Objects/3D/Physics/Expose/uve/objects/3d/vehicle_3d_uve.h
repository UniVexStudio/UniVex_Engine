// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Raycast-vehicle chassis component (`mass`, `engineForce`, `brakeForce`, `steeringAngleRadians`).
struct VehicleBody3DComponentUVE final {
    float mass = 1200.0F;
    float engineForce = 0.0F;
    float brakeForce = 0.0F;
    float steeringAngleRadians = 0.0F;
    Math::Vector3UVE linearVelocity{0.0F, 0.0F, 0.0F};
    bool enabled = true;

    // Runtime state, never persisted:
    float currentSpeedMetresPerSecond = 0.0F;

    [[nodiscard]] bool operator==(const VehicleBody3DComponentUVE& other) const noexcept {
        return mass == other.mass && engineForce == other.engineForce &&
               brakeForce == other.brakeForce &&
               steeringAngleRadians == other.steeringAngleRadians &&
               linearVelocity == other.linearVelocity && enabled == other.enabled;
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
    float wheelRadius = 0.35F;
    float suspensionRestLength = 0.3F;
    float suspensionStiffness = 35.0F;
    float suspensionDamping = 4.5F;
    bool isSteerable = false;
    bool isPowered = true;
    bool isBraking = true;
    bool enabled = true;

    // Runtime state, never persisted:
    bool inContact = true;
    float currentSuspensionLength = 0.3F;
    float suspensionForce = 0.0F;
    float rotationAngleRadians = 0.0F;

    [[nodiscard]] bool operator==(const VehicleWheel3DComponentUVE& other) const noexcept {
        return vehicleBodyEntity == other.vehicleBodyEntity && wheelRadius == other.wheelRadius &&
               suspensionRestLength == other.suspensionRestLength &&
               suspensionStiffness == other.suspensionStiffness &&
               suspensionDamping == other.suspensionDamping && isSteerable == other.isSteerable &&
               isPowered == other.isPowered && isBraking == other.isBraking &&
               enabled == other.enabled;
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

/// Computes spring-damper suspension force for a wheel given `compressionMetres` and
/// `compressionVelocityMetresPerSecond`.
[[nodiscard]] float EvaluateVehicleWheelSuspensionForceUVE(
    const VehicleWheel3DComponentUVE& wheel, float compressionMetres,
    float compressionVelocityMetresPerSecond) noexcept;

/// Steps all enabled `VehicleBody3DComponentUVE` and `VehicleWheel3DComponentUVE` entities by
/// `deltaTimeSeconds`. Returns the number of vehicle bodies stepped.
std::size_t UpdateVehicle3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
