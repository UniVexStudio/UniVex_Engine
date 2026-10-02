// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/rigid_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsRigid3DNodeDefinitionValidUVE(const Rigid3DNodeDefinitionUVE& value) noexcept {
    return IsRigidBodyComponentValidUVE(value.body);
}

void ApplyRigid3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Rigid3DNodeDefinitionUVE& value) {
    // Rigid3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Rigid3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<RigidBodyComponentUVE>(entity, value.body);
}

} // namespace UVE::Scene
