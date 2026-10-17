// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/box_mesh_3d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsBoxMesh3DObjectDefinitionValidUVE(const BoxMesh3DObjectDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplyBoxMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const BoxMesh3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, BoxMesh3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

bool BoxMesh3DUVE::IsBoxMeshUVE(const PrimitiveMeshComponentUVE& mesh) noexcept {
    return IsPrimitiveMeshComponentValidUVE(mesh) && mesh.kind == PrimitiveMeshKindUVE::Cube;
}

Math::Vector3UVE BoxMesh3DUVE::DefaultColliderHalfExtentsUVE() noexcept {
    return kDefaultHalfExtentsUVE;
}

Math::AabbUVE BoxMesh3DUVE::LocalBoundsUVE() noexcept {
    return Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, kDefaultHalfExtentsUVE);
}

float BoxMesh3DUVE::VolumeFromScaleUVE(const Math::Vector3UVE& worldScale) noexcept {
    if (!Math::IsFiniteUVE(worldScale) || worldScale.x <= 0.0F || worldScale.y <= 0.0F ||
        worldScale.z <= 0.0F) {
        return 0.0F;
    }
    const float volume = worldScale.x * worldScale.y * worldScale.z;
    return std::isfinite(volume) ? volume : 0.0F;
}

void BoxMesh3DUVE::SyncColliderToBoxUVE(ColliderComponentUVE& collider) noexcept {
    collider.shapeType = ColliderShapeTypeUVE::Box;
    collider.halfExtents = kDefaultHalfExtentsUVE;
}

} // namespace UVE::Scene
