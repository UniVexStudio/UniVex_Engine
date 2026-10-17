// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/sphere_mesh_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsSphereMesh3DObjectDefinitionValidUVE(const SphereMesh3DObjectDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplySphereMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const SphereMesh3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, SphereMesh3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

bool SphereMesh3DUVE::IsSphereMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept {
    return IsPrimitiveMeshComponentValidUVE(mesh) && mesh.kind == PrimitiveMeshKindUVE::UVSphere;
}

Math::Vector3UVE SphereMesh3DUVE::DefaultColliderHalfExtentsUVE() noexcept {
    return kDefaultHalfExtentsUVE;
}

Math::AabbUVE SphereMesh3DUVE::LocalBoundsUVE() noexcept {
    return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, kDefaultHalfExtentsUVE);
}

float SphereMesh3DUVE::EffectiveRadiusFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept {
    if (!Math::IsFiniteUVE(worldScale) || worldScale.x <= 0.0F || worldScale.y <= 0.0F ||
        worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float maxAxis = std::max({worldScale.x, worldScale.y, worldScale.z});
    const float radius = kDefaultRadiusUVE * maxAxis;
    return std::isfinite(radius) ? radius : 0.0F;
}

void SphereMesh3DUVE::SyncColliderHalfExtentsUVE(ColliderComponentUVE& collider) noexcept {
    collider.halfExtents = kDefaultHalfExtentsUVE;
}

} // namespace UVE::Scene
