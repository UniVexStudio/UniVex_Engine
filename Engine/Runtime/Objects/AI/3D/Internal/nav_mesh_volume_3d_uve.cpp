// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/nav_mesh_volume_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsNavMeshVolume3DObjectComponentValidUVE(const NavMeshVolume3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DObjectStringUVE(value.navigationMeshAssetPath) &&
           std::isfinite(value.cellSize) && value.cellSize > 0.0F && std::isfinite(value.agentRadius) &&
           value.agentRadius > 0.0F && std::isfinite(value.agentHeight) &&
           value.agentHeight >= value.agentRadius * 2.0F && std::isfinite(value.maximumSlopeDegrees) &&
           value.maximumSlopeDegrees > 0.0F && value.maximumSlopeDegrees < 90.0F &&
           std::isfinite(value.maximumStepHeight) && value.maximumStepHeight >= 0.0F;
}

bool IsNavMeshVolume3DObjectDefinitionValidUVE(
    const NavMeshVolume3DObjectDefinitionUVE& value) noexcept {
    return IsNavMeshVolume3DObjectComponentValidUVE(value.volume);
}

void ApplyNavMeshVolume3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const NavMeshVolume3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureObject3DBaselineUVE(entityManager, entity,
                              NavMeshVolume3DObjectDefinitionUVE::defaultName);
    if (entityManager.HasComponentUVE<NavMeshVolume3DComponentUVE>(entity)) {
        entityManager.GetComponentUVE<NavMeshVolume3DComponentUVE>(entity) = value.volume;
    } else {
        entityManager.AddComponentUVE<NavMeshVolume3DComponentUVE>(entity, value.volume);
    }
}

bool NavMeshVolume3DUVE::IsBakingEnabledUVE(const NavMeshVolume3DComponentUVE& volume) noexcept {
    return volume.enabled && IsNavMeshVolume3DObjectComponentValidUVE(volume);
}

Math::AabbUVE NavMeshVolume3DUVE::ResolveWorldBoundsUVE(
    const NavMeshVolume3DComponentUVE& volume, const Math::Vector3UVE& worldPosition) noexcept {
    return Math::AabbUVE::FromCenterExtentsUVE(worldPosition, volume.boundsHalfExtents);
}

float NavMeshVolume3DUVE::DistanceToVolumeBoundsUVE(
    const Math::AabbUVE& bounds, const Math::Vector3UVE& point) noexcept {
    const float deltaX = std::max(std::max(bounds.min.x - point.x, point.x - bounds.max.x), 0.0F);
    const float deltaY = std::max(std::max(bounds.min.y - point.y, point.y - bounds.max.y), 0.0F);
    const float deltaZ = std::max(std::max(bounds.min.z - point.z, point.z - bounds.max.z), 0.0F);
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

bool NavMeshVolume3DUVE::ContainsWorldPointUVE(
    const NavMeshVolume3DComponentUVE& volume, const Math::Vector3UVE& worldPosition,
    const Math::Vector3UVE& point) noexcept {
    if (!IsFinite3DObjectVectorUVE(worldPosition) || !IsFinite3DObjectVectorUVE(point) ||
        !IsNavMeshVolume3DObjectComponentValidUVE(volume)) {
        return false;
    }
    const Math::AabbUVE bounds = ResolveWorldBoundsUVE(volume, worldPosition);
    return DistanceToVolumeBoundsUVE(bounds, point) <= 0.0F;
}

bool NavMeshVolume3DUVE::SharesNavigationLayerUVE(
    const NavMeshVolume3DComponentUVE& volume, const std::uint32_t layerMask) noexcept {
    return (volume.navigationLayers & layerMask) != 0U;
}

void NavMeshVolume3DUVE::RequestRebuildUVE(NavMeshVolume3DComponentUVE& volume) noexcept {
    volume.rebuildRequested = true;
}

bool NavMeshVolume3DUVE::ConsumeRebuildRequestUVE(NavMeshVolume3DComponentUVE& volume) noexcept {
    const bool requested = volume.rebuildRequested;
    volume.rebuildRequested = false;
    return requested;
}

} // namespace UVE::Scene
