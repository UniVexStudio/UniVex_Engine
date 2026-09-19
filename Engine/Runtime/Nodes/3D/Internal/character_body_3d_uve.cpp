// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/character_body_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCharacterBody3DNodeDefinitionValidUVE(const CharacterBody3DNodeDefinitionUVE& value) noexcept {
    // Kinematic is part of the CharacterBody3D contract, not a tunable: a dynamic body here
    // would make the node a RigidBody3D, and gravity would fight the character controller.
    return IsColliderComponentValidUVE(value.collider) && IsRigidBodyComponentValidUVE(value.body) &&
           value.body.isKinematic;
}

void ApplyCharacterBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const CharacterBody3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    entityManager.AddComponentUVE<RigidBodyComponentUVE>(entity, value.body);
}

} // namespace UVE::Scene
