// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/visibility_region_3d_uve.h"

#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"

namespace UVE::Scene {

bool IsVisibilityRegion3DObjectComponentValidUVE(const VisibilityRegion3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
}

bool ResolveVisibilityRegion3DContainsPointUVE(const VisibilityRegion3DComponentUVE& config,
                                               const Math::Vector3UVE& regionWorldPosition,
                                               const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsVisibilityRegion3DObjectComponentValidUVE(config)) {
        return false;
    }
    const float localX = pointWorld.x - regionWorldPosition.x;
    const float localY = pointWorld.y - regionWorldPosition.y;
    const float localZ = pointWorld.z - regionWorldPosition.z;
    if (!std::isfinite(localX) || !std::isfinite(localY) || !std::isfinite(localZ)) {
        return false;
    }
    // Per-axis containment, boundary INCLUDED on both sides: the box is exactly what the author
    // sees (unlike the world partition's cells, which partition space and therefore need a
    // half-open boundary, one box's skin belongs to itself exactly once).
    return std::abs(localX) <= config.halfExtents.x && std::abs(localY) <= config.halfExtents.y &&
           std::abs(localZ) <= config.halfExtents.z;
}

bool ResolveVisibilityRegion3DAnyViewerInsideUVE(
    const VisibilityRegion3DComponentUVE& config, const Math::Vector3UVE& regionWorldPosition,
    const std::vector<Math::Vector3UVE>& viewerPositions) noexcept {
    for (const Math::Vector3UVE& viewer : viewerPositions) {
        if (ResolveVisibilityRegion3DContainsPointUVE(config, regionWorldPosition, viewer)) {
            return true;
        }
    }
    return false;
}

bool CarriesVisibilityRegion3DDrawableUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    return entityManager.HasComponentUVE<MeshComponentUVE>(entity) ||
           entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity) ||
           entityManager.HasComponentUVE<Decal3DComponentUVE>(entity) ||
           entityManager.HasComponentUVE<ParticleEmitterComponentUVE>(entity) ||
           entityManager.HasComponentUVE<FogVolume3DComponentUVE>(entity);
}

std::uint32_t ResolveVisibilityRegion3DDrawableLayersUVE(IEntityManagerUVE& entityManager,
                                                         const EntityUVE entity) {
    if (entityManager.HasComponentUVE<MeshComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<MeshComponentUVE>(entity).visibilityLayers;
    }
    if (CarriesVisibilityRegion3DDrawableUVE(entityManager, entity)) {
        return kDefaultVisibilityRegionDrawableLayersUVE;
    }
    return 0U;
}

bool IsVisibilityRegion3DDrawHiddenUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<VisibilityRegion3DMembershipComponentUVE>(entity)) {
        return false;
    }
    const VisibilityRegion3DMembershipComponentUVE& membership =
        entityManager.GetComponentUVE<VisibilityRegion3DMembershipComponentUVE>(entity);
    const bool ownerAlive = membership.region != kInvalidEntityUVE &&
                            entityManager.IsAliveUVE(membership.region) &&
                            entityManager.HasComponentUVE<VisibilityRegion3DComponentUVE>(membership.region);
    return !ResolveVisibilityRegion3DMembershipLiveUVE(ownerAlive, membership.live);
}

void CollectVisibilityRegion3DGizmosUVE(IEntityManagerUVE& entityManager,
                                        std::vector<VisibilityRegion3DGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, VisibilityRegion3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const VisibilityRegion3DComponentUVE& config) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty ||
                !IsVisibilityRegion3DObjectComponentValidUVE(config)) {
                return;
            }
            VisibilityRegion3DGizmoUVE gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.halfExtents = config.halfExtents;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3UVE{1.0F, 0.72F, 0.28F}
                                         : Math::Vector3UVE{0.50F, 0.48F, 0.42F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
