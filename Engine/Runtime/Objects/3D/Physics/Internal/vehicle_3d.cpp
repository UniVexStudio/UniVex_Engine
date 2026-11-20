// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vehicle_3d.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "disabled_component.h"
#include "quaternion.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsVehicleBody3DComponentValidUVE(const VehicleBody3DComponentUVE& value) noexcept {
    return std::isfinite(value.mass) && value.mass > 0.0F && std::isfinite(value.engineForce) &&
           std::isfinite(value.brakeForce) && value.brakeForce >= 0.0F &&
           std::isfinite(value.steeringAngleRadians) &&
           std::fabs(value.steeringAngleRadians) <= 1.5F &&
           IsFinite3DObjectVectorUVE(value.linearVelocity) &&
           IsFinite3DObjectVectorUVE(value.angularVelocity) &&
           std::isfinite(value.wheelbaseMetres) && value.wheelbaseMetres > 0.1F &&
           std::isfinite(value.trackWidthMetres) && value.trackWidthMetres > 0.1F;
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
    if (!IsFinite3DObjectVectorUVE(out.angularVelocity)) {
        out.angularVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.wheelbaseMetres) || out.wheelbaseMetres <= 0.1F) {
        out.wheelbaseMetres = 2.6F;
    }
    if (!std::isfinite(out.trackWidthMetres) || out.trackWidthMetres <= 0.1F) {
        out.trackWidthMetres = 1.6F;
    }
    if (!std::isfinite(out.lateralGrip) || out.lateralGrip < 0.0F) {
        out.lateralGrip = 6.0F;
    }
    if (!std::isfinite(out.rollingResistance) || out.rollingResistance < 0.0F) {
        out.rollingResistance = 0.015F;
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
    return IsFinite3DObjectVectorUVE(value.localMountOffset) &&
           std::isfinite(value.wheelRadius) && value.wheelRadius > 0.0F &&
           std::isfinite(value.suspensionRestLength) && value.suspensionRestLength > 0.0F &&
           std::isfinite(value.suspensionStiffness) && value.suspensionStiffness >= 0.0F &&
           std::isfinite(value.suspensionDamping) && value.suspensionDamping >= 0.0F;
}

VehicleWheel3DComponentUVE SanitizeVehicleWheel3DComponentUVE(
    const VehicleWheel3DComponentUVE& value) noexcept {
    VehicleWheel3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.localMountOffset)) {
        out.localMountOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
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
    out.maxSteerAngleRadians =
        std::isfinite(out.maxSteerAngleRadians) ? std::clamp(out.maxSteerAngleRadians, 0.0F, 1.4F) : 0.65F;
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

std::pair<float, float> EvaluateAckermannSteeringUVE(const float steerAngleRadians,
                                                     const float wheelbaseMetres,
                                                     const float trackWidthMetres) noexcept {
    if (!std::isfinite(steerAngleRadians) || std::fabs(steerAngleRadians) < 1.0e-5F ||
        wheelbaseMetres <= 0.1F || trackWidthMetres <= 0.01F) {
        return {0.0F, 0.0F};
    }
    const float clamped = std::clamp(steerAngleRadians, -1.2F, 1.2F);
    const float turnRadius = wheelbaseMetres / std::tan(std::fabs(clamped));
    const float halfTrack = 0.5F * trackWidthMetres;
    const float innerDenom = std::max(turnRadius - halfTrack, 0.1F);
    const float outerDenom = turnRadius + halfTrack;
    const float innerAngle = std::atan(wheelbaseMetres / innerDenom);
    const float outerAngle = std::atan(wheelbaseMetres / outerDenom);
    if (clamped > 0.0F) {
        // Left turn: left wheel is inner (steeper), right wheel is outer.
        return {innerAngle, outerAngle};
    }
    return {-outerAngle, -innerAngle};
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
                                      const float deltaTimeSeconds,
                                      const VehicleRaycastCallbackUVE& groundRaycaster) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }

    struct WheelLinkSummaryUVE final {
        std::uint32_t totalWheels = 0U;
        std::uint32_t groundedWheels = 0U;
        std::uint32_t poweredGrounded = 0U;
        std::uint32_t brakingGrounded = 0U;
        float averageSurfaceFriction = 1.0F;
        float netSuspensionForceY = 0.0F;
        float pitchTorque = 0.0F;
        float rollTorque = 0.0F;
    };
    std::unordered_map<std::uint64_t, WheelLinkSummaryUVE> summaries;
    const auto entityKey = [](const EntityUVE e) noexcept -> std::uint64_t {
        return (static_cast<std::uint64_t>(e.generation) << 32U) | static_cast<std::uint64_t>(e.index);
    };

    // Pass 1: evaluate per-wheel raycast suspension and wheel masks.
    entityManager.ForEachUVE<VehicleWheel3DComponentUVE>(
        [&](const EntityUVE wheelEntity, VehicleWheel3DComponentUVE& wheel) {
            if (!wheel.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(wheelEntity) ||
                !IsVehicleWheel3DComponentValidUVE(wheel) ||
                wheel.vehicleBodyEntity == kInvalidEntityUVE ||
                !entityManager.IsAliveUVE(wheel.vehicleBodyEntity) ||
                !entityManager.HasComponentUVE<VehicleBody3DComponentUVE>(wheel.vehicleBodyEntity)) {
                return;
            }
            const VehicleBody3DComponentUVE& vehicle =
                entityManager.GetComponentUVE<VehicleBody3DComponentUVE>(wheel.vehicleBodyEntity);
            const TransformComponentUVE chassisTr =
                entityManager.HasComponentUVE<TransformComponentUVE>(wheel.vehicleBodyEntity)
                    ? entityManager.GetComponentUVE<TransformComponentUVE>(wheel.vehicleBodyEntity)
                    : TransformComponentUVE{};
            Math::QuaternionUVE chassisRot{};
            if (!Math::TryNormalizeUVE(chassisTr.localRotation, chassisRot)) {
                chassisRot = Math::QuaternionUVE{};
            }

            auto& summary = summaries[entityKey(wheel.vehicleBodyEntity)];
            ++summary.totalWheels;

            const auto [leftSteer, rightSteer] = EvaluateAckermannSteeringUVE(
                vehicle.steeringAngleRadians, vehicle.wheelbaseMetres, vehicle.trackWidthMetres);
            if (wheel.isSteerable) {
                const float rawSteer = wheel.localMountOffset.x < 0.0F ? leftSteer : rightSteer;
                wheel.currentSteerAngleRadians =
                    std::clamp(rawSteer, -wheel.maxSteerAngleRadians, wheel.maxSteerAngleRadians);
            } else {
                wheel.currentSteerAngleRadians = 0.0F;
            }

            const float prevLength = wheel.currentSuspensionLength;
            float friction = 1.0F;
            if (groundRaycaster) {
                const Math::Vector3UVE mountWorld =
                    chassisTr.localPosition + Math::RotateVectorUVE(chassisRot, wheel.localMountOffset);
                const Math::Vector3UVE downDir =
                    Math::RotateVectorUVE(chassisRot, Math::Vector3UVE{0.0F, -1.0F, 0.0F});
                const float maxRay = wheel.suspensionRestLength + wheel.wheelRadius;
                const VehicleGroundSampleUVE sample = groundRaycaster(mountWorld, downDir, maxRay);
                if (sample.hit && sample.hitDistance >= 0.0F && sample.hitDistance <= maxRay) {
                    wheel.inContact = true;
                    wheel.currentSuspensionLength =
                        std::clamp(sample.hitDistance - wheel.wheelRadius, 0.0F, wheel.suspensionRestLength);
                    friction = std::max(0.05F, sample.surfaceFriction);
                } else {
                    wheel.inContact = false;
                    wheel.currentSuspensionLength = wheel.suspensionRestLength;
                }
            }

            const float compression =
                wheel.inContact ? std::max(0.0F, wheel.suspensionRestLength - wheel.currentSuspensionLength) : 0.0F;
            const float compressionVel = (prevLength - wheel.currentSuspensionLength) / deltaTimeSeconds;
            wheel.previousSuspensionLength = prevLength;
            wheel.suspensionForce =
                wheel.inContact ? EvaluateVehicleWheelSuspensionForceUVE(wheel, compression, compressionVel) : 0.0F;

            if (wheel.inContact) {
                ++summary.groundedWheels;
                if (wheel.isPowered) {
                    ++summary.poweredGrounded;
                }
                if (wheel.isBraking) {
                    ++summary.brakingGrounded;
                }
                summary.averageSurfaceFriction += (friction - summary.averageSurfaceFriction) /
                                                  static_cast<float>(summary.groundedWheels);
                summary.netSuspensionForceY += wheel.suspensionForce;
                // Suspension torque arms for pitch (Z offset) and roll (X offset).
                summary.pitchTorque += -wheel.localMountOffset.z * wheel.suspensionForce;
                summary.rollTorque += wheel.localMountOffset.x * wheel.suspensionForce;
            }
        });

    // Pass 2: integrate 6-DOF vehicle chassis linear & angular motion.
    std::size_t steppedVehicles = 0U;
    entityManager.ForEachUVE<VehicleBody3DComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE vehicleEntity, VehicleBody3DComponentUVE& vehicle,
            TransformComponentUVE& localTransform) {
            if (!vehicle.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(vehicleEntity) ||
                !IsVehicleBody3DComponentValidUVE(vehicle)) {
                return;
            }
            const auto summaryIt = summaries.find(entityKey(vehicleEntity));
            const bool hasLinkedWheels = summaryIt != summaries.end() && summaryIt->second.totalWheels > 0U;
            const WheelLinkSummaryUVE summary = hasLinkedWheels ? summaryIt->second : WheelLinkSummaryUVE{};
            vehicle.groundedWheelCount = hasLinkedWheels ? summary.groundedWheels : 4U;

            Math::QuaternionUVE chassisRot{};
            if (!Math::TryNormalizeUVE(localTransform.localRotation, chassisRot)) {
                chassisRot = Math::QuaternionUVE{};
            }
            const Math::Vector3UVE forwardDir =
                Math::RotateVectorUVE(chassisRot, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
            const Math::Vector3UVE rightDir =
                Math::RotateVectorUVE(chassisRot, Math::Vector3UVE{1.0F, 0.0F, 0.0F});

            float forwardSpeed = Math::DotUVE(vehicle.linearVelocity, forwardDir);
            float lateralSpeed = Math::DotUVE(vehicle.linearVelocity, rightDir);

            const bool canDrive = !hasLinkedWheels || summary.poweredGrounded > 0U;
            const bool canBrake = !hasLinkedWheels || summary.brakingGrounded > 0U;
            const float gripScale = hasLinkedWheels ? summary.averageSurfaceFriction : 1.0F;

            // Longitudinal drive acceleration (respecting reverseGear and powered wheel mask).
            if (canDrive && vehicle.engineForce != 0.0F) {
                const float signedEngine =
                    vehicle.reverseGear ? -std::fabs(vehicle.engineForce) : vehicle.engineForce;
                forwardSpeed += (signedEngine * gripScale / vehicle.mass) * deltaTimeSeconds;
            }

            // Braking strictly opposes motion and clamps to zero (never accelerates backwards).
            if (canBrake && vehicle.brakeForce > 0.0F && std::fabs(forwardSpeed) > 1.0e-4F) {
                const float brakeDelta = (vehicle.brakeForce * gripScale / vehicle.mass) * deltaTimeSeconds;
                if (std::fabs(forwardSpeed) <= brakeDelta) {
                    forwardSpeed = 0.0F;
                } else {
                    forwardSpeed -= (forwardSpeed > 0.0F ? brakeDelta : -brakeDelta);
                }
            }

            // Rolling resistance.
            forwardSpeed *= std::max(0.0F, 1.0F - vehicle.rollingResistance * deltaTimeSeconds);

            // Lateral tire grip damps sideways slip velocity.
            const float lateralDamp = std::exp(-vehicle.lateralGrip * gripScale * deltaTimeSeconds);
            lateralSpeed *= lateralDamp;

            // Ackermann kinematic-dynamic yaw rate coupled to forward velocity.
            const float yawRate =
                (std::fabs(vehicle.steeringAngleRadians) > 1.0e-5F && vehicle.wheelbaseMetres > 0.1F)
                    ? (forwardSpeed / vehicle.wheelbaseMetres) * std::tan(vehicle.steeringAngleRadians)
                    : 0.0F;
            vehicle.angularVelocity.y = yawRate;
            if (hasLinkedWheels && groundRaycaster) {
                const float inertia = std::max(vehicle.mass * 0.5F, 100.0F);
                vehicle.angularVelocity.x =
                    (vehicle.angularVelocity.x + (summary.pitchTorque / inertia) * deltaTimeSeconds) *
                    std::exp(-4.0F * deltaTimeSeconds);
                vehicle.angularVelocity.z =
                    (vehicle.angularVelocity.z + (summary.rollTorque / inertia) * deltaTimeSeconds) *
                    std::exp(-4.0F * deltaTimeSeconds);
            }

            const float yawDelta = vehicle.angularVelocity.y * deltaTimeSeconds;
            if (std::fabs(yawDelta) > 1.0e-6F) {
                Math::QuaternionUVE dq{};
                if (Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, yawDelta, dq)) {
                    Math::QuaternionUVE nextRot{};
                    if (Math::TryNormalizeUVE(dq * chassisRot, nextRot)) {
                        localTransform.localRotation = nextRot;
                        chassisRot = nextRot;
                    }
                }
            }

            const Math::Vector3UVE newForward =
                Math::RotateVectorUVE(chassisRot, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
            const Math::Vector3UVE newRight =
                Math::RotateVectorUVE(chassisRot, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
            const float verticalSpeed = vehicle.linearVelocity.y;
            vehicle.linearVelocity = newForward * forwardSpeed + newRight * lateralSpeed +
                                     Math::Vector3UVE{0.0F, verticalSpeed, 0.0F};
            localTransform.localPosition =
                localTransform.localPosition + vehicle.linearVelocity * deltaTimeSeconds;
            vehicle.currentSpeedMetresPerSecond =
                std::sqrt(Math::LengthSquaredUVE(vehicle.linearVelocity));

            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(vehicleEntity)) {
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(vehicleEntity).dirty = true;
            }
            ++steppedVehicles;
        });

    // Pass 3: update wheel visual spin rotation from chassis speed.
    entityManager.ForEachUVE<VehicleWheel3DComponentUVE>(
        [&](const EntityUVE wheelEntity, VehicleWheel3DComponentUVE& wheel) {
            if (!wheel.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(wheelEntity) ||
                !IsVehicleWheel3DComponentValidUVE(wheel) ||
                wheel.vehicleBodyEntity == kInvalidEntityUVE ||
                !entityManager.IsAliveUVE(wheel.vehicleBodyEntity) ||
                !entityManager.HasComponentUVE<VehicleBody3DComponentUVE>(wheel.vehicleBodyEntity)) {
                return;
            }
            const VehicleBody3DComponentUVE& vehicle =
                entityManager.GetComponentUVE<VehicleBody3DComponentUVE>(wheel.vehicleBodyEntity);
            wheel.rotationAngleRadians +=
                (vehicle.currentSpeedMetresPerSecond / wheel.wheelRadius) * deltaTimeSeconds;
        });
    return steppedVehicles;
}

} // namespace UVE::Scene
