// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/box_mesh_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsBoxMesh3DObjectDefinitionValidUVE(const BoxMesh3DObjectDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplyBoxMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const BoxMesh3DObjectDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Object3D, the Object section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, BoxMesh3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
