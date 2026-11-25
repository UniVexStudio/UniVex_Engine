// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sphere_mesh_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsSphereMesh3DObjectDefinitionValid(const SphereMesh3DObjectDefinition& value) noexcept {
    return IsPrimitiveMeshComponentValid(value.mesh) && IsColliderComponentValid(value.collider);
}

void ApplySphereMesh3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const SphereMesh3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, SphereMesh3DObjectDefinition::defaultName);
    entityManager.AddComponent<PrimitiveMeshComponent>(entity, value.mesh);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
}

bool SphereMesh3D::IsSphereMesh(const PrimitiveMeshComponent& mesh) noexcept {
    return IsPrimitiveMeshComponentValid(mesh) && mesh.kind == PrimitiveMeshKind::UVSphere;
}

Math::Vector3 SphereMesh3D::DefaultColliderHalfExtents() noexcept {
    return kDefaultHalfExtents;
}

Math::Aabb SphereMesh3D::LocalBounds() noexcept {
    return Math::Aabb::FromCenterExtents(Math::Vector3{}, kDefaultHalfExtents);
}

float SphereMesh3D::EffectiveRadiusFromScale(const Math::Vector3& worldScale) noexcept {
    if (!Math::IsFinite(worldScale) || worldScale.x <= 0.0F || worldScale.y <= 0.0F ||
        worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float maxAxis = std::max({worldScale.x, worldScale.y, worldScale.z});
    const float radius = kDefaultRadius * maxAxis;
    return std::isfinite(radius) ? radius : 0.0F;
}

void SphereMesh3D::SyncColliderHalfExtents(ColliderComponent& collider) noexcept {
    collider.halfExtents = kDefaultHalfExtents;
}

} // namespace UVE::Scene
