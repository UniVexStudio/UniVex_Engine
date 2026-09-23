// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/plane_mesh_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {

bool IsPlaneMesh3DNodeDefinitionValidUVE(const PlaneMesh3DNodeDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplyPlaneMesh3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const PlaneMesh3DNodeDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Node3D, the Node section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, PlaneMesh3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
