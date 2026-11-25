// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "nav_seeker_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsNavSeeker3DObjectComponentValid(const NavSeeker3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.targetPosition) && IsFinite3DObjectVector(value.nextPathPosition) &&
           IsFinite3DObjectVector(value.desiredVelocity) && std::isfinite(value.radius) && value.radius > 0.0F &&
           std::isfinite(value.height) && value.height >= value.radius * 2.0F && std::isfinite(value.maxSpeed) &&
           value.maxSpeed > 0.0F && std::isfinite(value.pathUpdateInterval) && value.pathUpdateInterval > 0.0F &&
           value.pathUpdateInterval <= 10.0F && std::isfinite(value.acceleration) &&
           value.acceleration >= 0.0F && std::isfinite(value.waypointRadius) && value.waypointRadius >= 0.0F &&
           std::isfinite(value.targetTolerance) && value.targetTolerance >= 0.0F &&
           std::isfinite(value.slowDownRadius) && value.slowDownRadius >= 0.0F &&
           std::isfinite(value.avoidanceRadius) && value.avoidanceRadius >= 0.0F &&
           value.navigationLayers != 0U && value.pathStatus <= NavigationAgentPathStatus::Failed;
}

bool IsNavSeeker3DObjectDefinitionValid(
    const NavSeeker3DObjectDefinition& value) noexcept {
    return IsNavSeeker3DObjectComponentValid(value.seeker);
}

void ApplyNavSeeker3DObjectDefinition(IEntityManager& entityManager,
                                         const Entity entity,
                                         const NavSeeker3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    EnsureObject3DBaseline(entityManager, entity,
                              NavSeeker3DObjectDefinition::defaultName);
    if (entityManager.HasComponent<NavSeeker3DComponent>(entity)) {
        entityManager.GetComponent<NavSeeker3DComponent>(entity) = value.seeker;
    } else {
        entityManager.AddComponent<NavSeeker3DComponent>(entity, value.seeker);
    }
}

bool NavSeeker3D::IsActive(const NavSeeker3DComponent& seeker) noexcept {
    return seeker.enabled && IsNavSeeker3DObjectComponentValid(seeker);
}

float NavSeeker3D::EffectiveAvoidanceRadius(
    const NavSeeker3DComponent& seeker) noexcept {
    if (!seeker.avoidanceEnabled || !std::isfinite(seeker.avoidanceRadius) ||
        seeker.avoidanceRadius <= 0.0F) {
        return 0.0F;
    }
    return seeker.avoidanceRadius;
}

float NavSeeker3D::PlanarDistanceToTarget(
    const NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept {
    if (!IsFinite3DObjectVector(currentPosition) ||
        !IsFinite3DObjectVector(seeker.targetPosition)) {
        return 0.0F;
    }
    const float dx = seeker.targetPosition.x - currentPosition.x;
    const float dz = seeker.targetPosition.z - currentPosition.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool NavSeeker3D::IsWithinTargetTolerance(
    const NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept {
    if (!IsNavSeeker3DObjectComponentValid(seeker) ||
        !IsFinite3DObjectVector(currentPosition)) {
        return false;
    }
    return PlanarDistanceToTarget(seeker, currentPosition) <= seeker.targetTolerance;
}

float NavSeeker3D::ResolveArrivalSpeedScale(
    const NavSeeker3DComponent& seeker, const float distanceToTargetMetres) noexcept {
    if (!IsNavSeeker3DObjectComponentValid(seeker) || !std::isfinite(distanceToTargetMetres) ||
        distanceToTargetMetres <= seeker.targetTolerance) {
        return 0.0F;
    }
    if (seeker.slowDownRadius <= seeker.targetTolerance ||
        distanceToTargetMetres >= seeker.slowDownRadius) {
        return 1.0F;
    }
    const float span = seeker.slowDownRadius - seeker.targetTolerance;
    return std::clamp((distanceToTargetMetres - seeker.targetTolerance) / span, 0.0F, 1.0F);
}

void NavSeeker3D::PublishIdleState(
    NavSeeker3DComponent& seeker, const Math::Vector3& currentPosition) noexcept {
    seeker.desiredVelocity = Math::Vector3{};
    seeker.nextPathPosition = currentPosition;
    seeker.pathStatus = NavigationAgentPathStatus::Idle;
    seeker.pathChanged = false;
    seeker.targetReached = false;
}

bool NavSeeker3D::TrySetTargetPosition(
    NavSeeker3DComponent& seeker, const Math::Vector3& targetPosition) noexcept {
    if (!IsFinite3DObjectVector(targetPosition)) {
        return false;
    }
    seeker.targetPosition = targetPosition;
    seeker.targetReached = false;
    return true;
}

} // namespace UVE::Scene
