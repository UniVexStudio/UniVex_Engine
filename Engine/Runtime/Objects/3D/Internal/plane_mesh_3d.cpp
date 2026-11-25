// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "plane_mesh_3d.h"

#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsPlaneMesh3DObjectDefinitionValid(const PlaneMesh3DObjectDefinition& value) noexcept {
    return IsPrimitiveMeshComponentValid(value.mesh) && IsColliderComponentValid(value.collider);
}

void ApplyPlaneMesh3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const PlaneMesh3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, PlaneMesh3DObjectDefinition::defaultName);
    entityManager.AddComponent<PrimitiveMeshComponent>(entity, value.mesh);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
}

bool PlaneMesh3D::IsPlaneMesh(const PrimitiveMeshComponent& mesh) noexcept {
    return IsPrimitiveMeshComponentValid(mesh) && mesh.kind == PrimitiveMeshKind::Plane;
}

Math::Vector3 PlaneMesh3D::DefaultColliderHalfExtents() noexcept {
    return kDefaultColliderHalfExtents;
}

Math::Aabb PlaneMesh3D::LocalBounds() noexcept {
    return Math::Aabb::FromCenterExtents(Math::Vector3{}, kDefaultColliderHalfExtents);
}

Math::Vector3 PlaneMesh3D::WorldNormal(const Math::Quaternion& worldRotation) noexcept {
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(worldRotation, normalized)) {
        return kLocalNormal;
    }
    return Math::RotateVector(normalized, kLocalNormal);
}

float PlaneMesh3D::SurfaceAreaFromScale(const Math::Vector3& worldScale) noexcept {
    if (!Math::IsFinite(worldScale) || worldScale.x <= 0.0F || worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float area = worldScale.x * worldScale.z;
    return std::isfinite(area) ? area : 0.0F;
}

void PlaneMesh3D::SyncColliderHalfExtents(ColliderComponent& collider) noexcept {
    collider.halfExtents = kDefaultColliderHalfExtents;
}

} // namespace UVE::Scene
