// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/vehicle_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsVehicleBody3DComponentValidUVE(const VehicleBody3DComponentUVE& value) noexcept {
    return std::isfinite(value.mass) && value.mass > 0.0F && std::isfinite(value.engineForce) &&
           std::isfinite(value.brakeForce) && value.brakeForce >= 0.0F &&
           std::isfinite(value.steeringAngleRadians) &&
           std::fabs(value.steeringAngleRadians) <= 1.5F &&
           IsFinite3DObjectVectorUVE(value.linearVelocity);
}

VehicleBody3DComponentUVE SanitizeVehicleBody3DComponentUVE(
    const VehicleBody3DComponentUVE& value) noexcept {
    VehicleBody3DComponentUVE out = value;
    if (!std::isfinite(out.mass) || out.mass <= 0.0F) {
        out.mass = 1200.0F;
    }
    if (!std::isfinite(out.engineForce)) {
        out.engineForce = 0.0F;
    }
    if (!std::isfinite(out.brakeForce) || out.brakeForce < 0.0F) {
        out.brakeForce = 0.0F;
    }
    if (!std::isfinite(out.steeringAngleRadians)) {
        out.steeringAngleRadians = 0.0F;
    } else {
        out.steeringAngleRadians = std::clamp(out.steeringAngleRadians, -1.5F, 1.5F);
    }
    if (!IsFinite3DObjectVectorUVE(out.linearVelocity)) {
        out.linearVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    return out;
}

bool IsVehicleBody3DObjectDefinitionValidUVE(
    const VehicleBody3DObjectDefinitionUVE& value) noexcept {
    return IsVehicleBody3DComponentValidUVE(value.vehicle);
}

void ApplyVehicleBody3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const VehicleBody3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, VehicleBody3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<VehicleBody3DComponentUVE>(
        entity, SanitizeVehicleBody3DComponentUVE(value.vehicle));
}

bool IsVehicleWheel3DComponentValidUVE(const VehicleWheel3DComponentUVE& value) noexcept {
    return std::isfinite(value.wheelRadius) && value.wheelRadius > 0.0F &&
           std::isfinite(value.suspensionRestLength) && value.suspensionRestLength > 0.0F &&
           std::isfinite(value.suspensionStiffness) && value.suspensionStiffness >= 0.0F &&
           std::isfinite(value.suspensionDamping) && value.suspensionDamping >= 0.0F;
}

VehicleWheel3DComponentUVE SanitizeVehicleWheel3DComponentUVE(
    const VehicleWheel3DComponentUVE& value) noexcept {
    VehicleWheel3DComponentUVE out = value;
    if (!std::isfinite(out.wheelRadius) || out.wheelRadius <= 0.0F) {
        out.wheelRadius = 0.35F;
    }
    if (!std::isfinite(out.suspensionRestLength) || out.suspensionRestLength <= 0.0F) {
        out.suspensionRestLength = 0.3F;
    }
    if (!std::isfinite(out.suspensionStiffness) || out.suspensionStiffness < 0.0F) {
        out.suspensionStiffness = 35.0F;
    }
    if (!std::isfinite(out.suspensionDamping) || out.suspensionDamping < 0.0F) {
        out.suspensionDamping = 4.5F;
    }
    return out;
}

bool IsVehicleWheel3DObjectDefinitionValidUVE(
    const VehicleWheel3DObjectDefinitionUVE& value) noexcept {
    return IsVehicleWheel3DComponentValidUVE(value.wheel);
}

void ApplyVehicleWheel3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const VehicleWheel3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              VehicleWheel3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<VehicleWheel3DComponentUVE>(
        entity, SanitizeVehicleWheel3DComponentUVE(value.wheel));
}

float EvaluateVehicleWheelSuspensionForceUVE(
    const VehicleWheel3DComponentUVE& wheel, const float compressionMetres,
    const float compressionVelocityMetresPerSecond) noexcept {
    if (!IsVehicleWheel3DComponentValidUVE(wheel) || !std::isfinite(compressionMetres) ||
        compressionMetres <= 0.0F || !std::isfinite(compressionVelocityMetresPerSecond)) {
        return 0.0F;
    }
    const float force = wheel.suspensionStiffness * compressionMetres +
                        wheel.suspensionDamping * compressionVelocityMetresPerSecond;
    return std::max(0.0F, force);
}

std::size_t UpdateVehicle3DObjectsUVE(IEntityManagerUVE& entityManager,
                                      const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::size_t steppedVehicles = 0U;
    entityManager.ForEachUVE<VehicleBody3DComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE vehicleEntity, VehicleBody3DComponentUVE& vehicle,
            TransformComponentUVE& localTransform) {
            if (!vehicle.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(vehicleEntity) ||
                !IsVehicleBody3DComponentValidUVE(vehicle)) {
                return;
            }
            const float accel = (vehicle.engineForce - vehicle.brakeForce) / vehicle.mass;
            const Math::Vector3UVE forwardDir{-std::sin(vehicle.steeringAngleRadians), 0.0F,
                                              -std::cos(vehicle.steeringAngleRadians)};
            vehicle.linearVelocity =
                vehicle.linearVelocity + forwardDir * (accel * deltaTimeSeconds);
            localTransform.localPosition =
                localTransform.localPosition + vehicle.linearVelocity * deltaTimeSeconds;
            vehicle.currentSpeedMetresPerSecond =
                std::sqrt(Math::LengthSquaredUVE(vehicle.linearVelocity));
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(vehicleEntity)) {
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(vehicleEntity).dirty =
                    true;
            }
            ++steppedVehicles;
        });

    entityManager.ForEachUVE<VehicleWheel3DComponentUVE>(
        [&](const EntityUVE wheelEntity, VehicleWheel3DComponentUVE& wheel) {
            if (!wheel.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(wheelEntity) ||
                !IsVehicleWheel3DComponentValidUVE(wheel) ||
                wheel.vehicleBodyEntity == kInvalidEntityUVE ||
                !entityManager.IsAliveUVE(wheel.vehicleBodyEntity) ||
                !entityManager.HasComponentUVE<VehicleBody3DComponentUVE>(
                    wheel.vehicleBodyEntity)) {
                return;
            }
            const VehicleBody3DComponentUVE& vehicle =
                entityManager.GetComponentUVE<VehicleBody3DComponentUVE>(wheel.vehicleBodyEntity);
            const float compression =
                std::max(0.0F, wheel.suspensionRestLength - wheel.currentSuspensionLength);
            wheel.suspensionForce = EvaluateVehicleWheelSuspensionForceUVE(wheel, compression, 0.0F);
            wheel.rotationAngleRadians +=
                (vehicle.currentSpeedMetresPerSecond / wheel.wheelRadius) * deltaTimeSeconds;
        });
    return steppedVehicles;
}

} // namespace UVE::Scene
