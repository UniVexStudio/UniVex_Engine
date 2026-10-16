// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/plane_mesh_3d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsPlaneMesh3DObjectDefinitionValidUVE(const PlaneMesh3DObjectDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplyPlaneMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const PlaneMesh3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, PlaneMesh3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

bool PlaneMesh3DUVE::IsPlaneMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept {
    return IsPrimitiveMeshComponentValidUVE(mesh) && mesh.kind == PrimitiveMeshKindUVE::Plane;
}

Math::Vector3UVE PlaneMesh3DUVE::DefaultColliderHalfExtentsUVE() noexcept {
    return kDefaultColliderHalfExtentsUVE;
}

Math::AabbUVE PlaneMesh3DUVE::LocalBoundsUVE() noexcept {
    return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, kDefaultColliderHalfExtentsUVE);
}

Math::Vector3UVE PlaneMesh3DUVE::WorldNormalUVE(const Math::QuaternionUVE& worldRotation) noexcept {
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(worldRotation, normalized)) {
        return kLocalNormalUVE;
    }
    return Math::RotateVectorUVE(normalized, kLocalNormalUVE);
}

float PlaneMesh3DUVE::SurfaceAreaFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept {
    if (!Math::IsFiniteUVE(worldScale) || worldScale.x <= 0.0F || worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float area = worldScale.x * worldScale.z;
    return std::isfinite(area) ? area : 0.0F;
}

void PlaneMesh3DUVE::SyncColliderHalfExtentsUVE(ColliderComponentUVE& collider) noexcept {
    collider.halfExtents = kDefaultColliderHalfExtentsUVE;
}

} // namespace UVE::Scene
