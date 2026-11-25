// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "visibility_region_3d.h"

#include <cmath>

#include "editor_internal_entity_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"
#include "decal_3d.h"
#include "fog_volume_3d.h"

namespace UVE::Scene {

bool IsVisibilityRegion3DObjectComponentValid(const VisibilityRegion3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F;
}

void ApplyVisibilityRegion3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                                const VisibilityRegion3DObjectDefinition& value) {
    ApplyObject3DRecipe(entityManager, entity, VisibilityRegion3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) &&
        !entityManager.HasComponent<VisibilityRegion3DComponent>(entity)) {
        entityManager.AddComponent<VisibilityRegion3DComponent>(entity, value.region);
    }
}

bool ResolveVisibilityRegion3DContainsPoint(const VisibilityRegion3DComponent& config,
                                               const Math::Vector3& regionWorldPosition,
                                               const Math::Vector3& pointWorld) noexcept {
    if (!IsVisibilityRegion3DObjectComponentValid(config)) {
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

bool ResolveVisibilityRegion3DAnyViewerInside(
    const VisibilityRegion3DComponent& config, const Math::Vector3& regionWorldPosition,
    const std::vector<Math::Vector3>& viewerPositions) noexcept {
    for (const Math::Vector3& viewer : viewerPositions) {
        if (ResolveVisibilityRegion3DContainsPoint(config, regionWorldPosition, viewer)) {
            return true;
        }
    }
    return false;
}

bool CarriesVisibilityRegion3DDrawable(IEntityManager& entityManager, const Entity entity) {
    return entityManager.HasComponent<MeshComponent>(entity) ||
           entityManager.HasComponent<PrimitiveMeshComponent>(entity) ||
           entityManager.HasComponent<Decal3DComponent>(entity) ||
           entityManager.HasComponent<ParticleEmitterComponent>(entity) ||
           entityManager.HasComponent<FogVolume3DComponent>(entity);
}

std::uint32_t ResolveVisibilityRegion3DDrawableLayers(IEntityManager& entityManager,
                                                         const Entity entity) {
    if (entityManager.HasComponent<MeshComponent>(entity)) {
        return entityManager.GetComponent<MeshComponent>(entity).visibilityLayers;
    }
    if (CarriesVisibilityRegion3DDrawable(entityManager, entity)) {
        return kDefaultVisibilityRegionDrawableLayers;
    }
    return 0U;
}

bool IsVisibilityRegion3DDrawHidden(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return false;
    }
    if (!entityManager.HasComponent<VisibilityRegion3DMembershipComponent>(entity)) {
        return false;
    }
    const VisibilityRegion3DMembershipComponent& membership =
        entityManager.GetComponent<VisibilityRegion3DMembershipComponent>(entity);
    const bool ownerAlive = membership.region != kInvalidEntity &&
                            entityManager.IsAlive(membership.region) &&
                            entityManager.HasComponent<VisibilityRegion3DComponent>(membership.region);
    return !ResolveVisibilityRegion3DMembershipLive(ownerAlive, membership.live);
}

void CollectVisibilityRegion3DGizmos(IEntityManager& entityManager,
                                        std::vector<VisibilityRegion3DGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, VisibilityRegion3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const VisibilityRegion3DComponent& config) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty ||
                !IsVisibilityRegion3DObjectComponentValid(config)) {
                return;
            }
            VisibilityRegion3DGizmo gizmo;
            gizmo.origin = world.worldPosition;
            gizmo.halfExtents = config.halfExtents;
            gizmo.enabled = config.enabled;
            gizmo.color = config.enabled ? Math::Vector3{1.0F, 0.72F, 0.28F}
                                         : Math::Vector3{0.50F, 0.48F, 0.42F};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
