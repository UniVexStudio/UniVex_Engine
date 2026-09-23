// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/mesh_instance_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {

bool IsMeshInstance3DNodeDefinitionValidUVE(const MeshInstance3DNodeDefinitionUVE& value) noexcept {
    return IsMeshComponentValidUVE(value.mesh);
}

void ApplyMeshInstance3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const MeshInstance3DNodeDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Node3D, the Node section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, MeshInstance3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<MeshComponentUVE>(entity, value.mesh);
}

} // namespace UVE::Scene
