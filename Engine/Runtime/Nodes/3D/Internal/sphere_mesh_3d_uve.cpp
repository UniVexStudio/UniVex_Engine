// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/sphere_mesh_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsSphereMesh3DNodeDefinitionValidUVE(const SphereMesh3DNodeDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplySphereMesh3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const SphereMesh3DNodeDefinitionUVE& value) {
    // SphereMesh3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, SphereMesh3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
