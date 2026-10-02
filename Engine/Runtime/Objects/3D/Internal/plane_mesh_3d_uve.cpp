// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/plane_mesh_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsPlaneMesh3DObjectDefinitionValidUVE(const PlaneMesh3DObjectDefinitionUVE& value) noexcept {
    return IsPrimitiveMeshComponentValidUVE(value.mesh) && IsColliderComponentValidUVE(value.collider);
}

void ApplyPlaneMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const PlaneMesh3DObjectDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Object3D, the Object section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, PlaneMesh3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(entity, value.mesh);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
