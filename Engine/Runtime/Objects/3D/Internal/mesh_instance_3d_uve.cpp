// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/mesh_instance_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsMeshInstance3DObjectDefinitionValidUVE(const MeshInstance3DObjectDefinitionUVE& value) noexcept {
    return IsMeshComponentValidUVE(value.mesh);
}

void ApplyMeshInstance3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const MeshInstance3DObjectDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Object3D, the Object section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, MeshInstance3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<MeshComponentUVE>(entity, value.mesh);
}

} // namespace UVE::Scene
