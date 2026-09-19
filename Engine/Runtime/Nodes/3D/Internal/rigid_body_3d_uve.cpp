// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/rigid_body_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsRigidBody3DNodeDefinitionValidUVE(const RigidBody3DNodeDefinitionUVE& value) noexcept {
    return IsRigidBodyComponentValidUVE(value.body);
}

void ApplyRigidBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const RigidBody3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<RigidBodyComponentUVE>(entity, value.body);
}

} // namespace UVE::Scene
