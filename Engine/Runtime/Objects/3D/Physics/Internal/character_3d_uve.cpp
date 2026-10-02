// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/character_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {

bool IsCharacter3DObjectDefinitionValidUVE(const Character3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) && IsCharacterControllerComponentValidUVE(value.controller);
}

void ApplyCharacter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const Character3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplySolidBody3DBaseUVE(entityManager, entity, Character3DObjectDefinitionUVE::defaultName);
    if (!entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    }
    if (!entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<CharacterControllerComponentUVE>(entity, value.controller);
    }
}

} // namespace UVE::Scene
