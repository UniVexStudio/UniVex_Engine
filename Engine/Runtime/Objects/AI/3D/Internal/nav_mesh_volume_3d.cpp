// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "nav_mesh_volume_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsNavMeshVolume3DObjectComponentValid(const NavMeshVolume3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.boundsHalfExtents) && value.boundsHalfExtents.x > 0.0F &&
           value.boundsHalfExtents.y > 0.0F && value.boundsHalfExtents.z > 0.0F &&
           value.navigationLayers != 0U && IsBounded3DObjectString(value.navigationMeshAssetPath) &&
           std::isfinite(value.cellSize) && value.cellSize > 0.0F && std::isfinite(value.agentRadius) &&
           value.agentRadius > 0.0F && std::isfinite(value.agentHeight) &&
           value.agentHeight >= value.agentRadius * 2.0F && std::isfinite(value.maximumSlopeDegrees) &&
           value.maximumSlopeDegrees > 0.0F && value.maximumSlopeDegrees < 90.0F &&
           std::isfinite(value.maximumStepHeight) && value.maximumStepHeight >= 0.0F;
}

bool IsNavMeshVolume3DObjectDefinitionValid(
    const NavMeshVolume3DObjectDefinition& value) noexcept {
    return IsNavMeshVolume3DObjectComponentValid(value.volume);
}

void ApplyNavMeshVolume3DObjectDefinition(IEntityManager& entityManager,
                                             const Entity entity,
                                             const NavMeshVolume3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    EnsureObject3DBaseline(entityManager, entity,
                              NavMeshVolume3DObjectDefinition::defaultName);
    if (entityManager.HasComponent<NavMeshVolume3DComponent>(entity)) {
        entityManager.GetComponent<NavMeshVolume3DComponent>(entity) = value.volume;
    } else {
        entityManager.AddComponent<NavMeshVolume3DComponent>(entity, value.volume);
    }
}

bool NavMeshVolume3D::IsBakingEnabled(const NavMeshVolume3DComponent& volume) noexcept {
    return volume.enabled && IsNavMeshVolume3DObjectComponentValid(volume);
}

Math::Aabb NavMeshVolume3D::ResolveWorldBounds(
    const NavMeshVolume3DComponent& volume, const Math::Vector3& worldPosition) noexcept {
    return Math::Aabb::FromCenterExtents(worldPosition, volume.boundsHalfExtents);
}

float NavMeshVolume3D::DistanceToVolumeBounds(
    const Math::Aabb& bounds, const Math::Vector3& point) noexcept {
    const float deltaX = std::max(std::max(bounds.min.x - point.x, point.x - bounds.max.x), 0.0F);
    const float deltaY = std::max(std::max(bounds.min.y - point.y, point.y - bounds.max.y), 0.0F);
    const float deltaZ = std::max(std::max(bounds.min.z - point.z, point.z - bounds.max.z), 0.0F);
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

bool NavMeshVolume3D::ContainsWorldPoint(
    const NavMeshVolume3DComponent& volume, const Math::Vector3& worldPosition,
    const Math::Vector3& point) noexcept {
    if (!IsFinite3DObjectVector(worldPosition) || !IsFinite3DObjectVector(point) ||
        !IsNavMeshVolume3DObjectComponentValid(volume)) {
        return false;
    }
    const Math::Aabb bounds = ResolveWorldBounds(volume, worldPosition);
    return DistanceToVolumeBounds(bounds, point) <= 0.0F;
}

bool NavMeshVolume3D::SharesNavigationLayer(
    const NavMeshVolume3DComponent& volume, const std::uint32_t layerMask) noexcept {
    return (volume.navigationLayers & layerMask) != 0U;
}

void NavMeshVolume3D::RequestRebuild(NavMeshVolume3DComponent& volume) noexcept {
    volume.rebuildRequested = true;
}

bool NavMeshVolume3D::ConsumeRebuildRequest(NavMeshVolume3DComponent& volume) noexcept {
    const bool requested = volume.rebuildRequested;
    volume.rebuildRequested = false;
    return requested;
}

} // namespace UVE::Scene
