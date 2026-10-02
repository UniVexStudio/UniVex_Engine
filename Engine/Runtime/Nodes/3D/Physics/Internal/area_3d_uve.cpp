// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/area_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsArea3DNodeDefinitionValidUVE(const Area3DNodeDefinitionUVE& value) noexcept {
    return IsAreaComponentValidUVE(value.area);
}

void ApplyArea3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Area3DNodeDefinitionUVE& value) {
    // Area3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Area3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AreaComponentUVE>(entity, value.area);
}

} // namespace UVE::Scene
