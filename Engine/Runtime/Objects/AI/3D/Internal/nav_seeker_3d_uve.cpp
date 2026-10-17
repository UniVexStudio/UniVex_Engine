// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/nav_seeker_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsNavSeeker3DObjectComponentValidUVE(const NavSeeker3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.targetPosition) && IsFinite3DObjectVectorUVE(value.nextPathPosition) &&
           IsFinite3DObjectVectorUVE(value.desiredVelocity) && std::isfinite(value.radius) && value.radius > 0.0F &&
           std::isfinite(value.height) && value.height >= value.radius * 2.0F && std::isfinite(value.maxSpeed) &&
           value.maxSpeed > 0.0F && std::isfinite(value.pathUpdateInterval) && value.pathUpdateInterval > 0.0F &&
           value.pathUpdateInterval <= 10.0F && std::isfinite(value.acceleration) &&
           value.acceleration >= 0.0F && std::isfinite(value.waypointRadius) && value.waypointRadius >= 0.0F &&
           std::isfinite(value.targetTolerance) && value.targetTolerance >= 0.0F &&
           std::isfinite(value.slowDownRadius) && value.slowDownRadius >= 0.0F &&
           std::isfinite(value.avoidanceRadius) && value.avoidanceRadius >= 0.0F &&
           value.navigationLayers != 0U && value.pathStatus <= NavigationAgentPathStatusUVE::Failed;
}

bool IsNavSeeker3DObjectDefinitionValidUVE(
    const NavSeeker3DObjectDefinitionUVE& value) noexcept {
    return IsNavSeeker3DObjectComponentValidUVE(value.seeker);
}

void ApplyNavSeeker3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                         const EntityUVE entity,
                                         const NavSeeker3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureObject3DBaselineUVE(entityManager, entity,
                              NavSeeker3DObjectDefinitionUVE::defaultName);
    if (entityManager.HasComponentUVE<NavSeeker3DComponentUVE>(entity)) {
        entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(entity) = value.seeker;
    } else {
        entityManager.AddComponentUVE<NavSeeker3DComponentUVE>(entity, value.seeker);
    }
}

bool NavSeeker3DUVE::IsActiveUVE(const NavSeeker3DComponentUVE& seeker) noexcept {
    return seeker.enabled && IsNavSeeker3DObjectComponentValidUVE(seeker);
}

float NavSeeker3DUVE::EffectiveAvoidanceRadiusUVE(
    const NavSeeker3DComponentUVE& seeker) noexcept {
    if (!seeker.avoidanceEnabled || !std::isfinite(seeker.avoidanceRadius) ||
        seeker.avoidanceRadius <= 0.0F) {
        return 0.0F;
    }
    return seeker.avoidanceRadius;
}

float NavSeeker3DUVE::PlanarDistanceToTargetUVE(
    const NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept {
    if (!IsFinite3DObjectVectorUVE(currentPosition) ||
        !IsFinite3DObjectVectorUVE(seeker.targetPosition)) {
        return 0.0F;
    }
    const float dx = seeker.targetPosition.x - currentPosition.x;
    const float dz = seeker.targetPosition.z - currentPosition.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool NavSeeker3DUVE::IsWithinTargetToleranceUVE(
    const NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept {
    if (!IsNavSeeker3DObjectComponentValidUVE(seeker) ||
        !IsFinite3DObjectVectorUVE(currentPosition)) {
        return false;
    }
    return PlanarDistanceToTargetUVE(seeker, currentPosition) <= seeker.targetTolerance;
}

float NavSeeker3DUVE::ResolveArrivalSpeedScaleUVE(
    const NavSeeker3DComponentUVE& seeker, const float distanceToTargetMetres) noexcept {
    if (!IsNavSeeker3DObjectComponentValidUVE(seeker) || !std::isfinite(distanceToTargetMetres) ||
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

void NavSeeker3DUVE::PublishIdleStateUVE(
    NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& currentPosition) noexcept {
    seeker.desiredVelocity = Math::Vector3UVE{};
    seeker.nextPathPosition = currentPosition;
    seeker.pathStatus = NavigationAgentPathStatusUVE::Idle;
    seeker.pathChanged = false;
    seeker.targetReached = false;
}

bool NavSeeker3DUVE::TrySetTargetPositionUVE(
    NavSeeker3DComponentUVE& seeker, const Math::Vector3UVE& targetPosition) noexcept {
    if (!IsFinite3DObjectVectorUVE(targetPosition)) {
        return false;
    }
    seeker.targetPosition = targetPosition;
    seeker.targetReached = false;
    return true;
}

} // namespace UVE::Scene
