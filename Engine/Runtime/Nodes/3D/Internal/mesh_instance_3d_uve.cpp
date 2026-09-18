// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/mesh_instance_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsMeshInstance3DNodeDefinitionValidUVE(const MeshInstance3DNodeDefinitionUVE& value) noexcept {
    return IsMeshComponentValidUVE(value.mesh);
}

void ApplyMeshInstance3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const MeshInstance3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<MeshComponentUVE>(entity, value.mesh);
}

} // namespace UVE::Scene
