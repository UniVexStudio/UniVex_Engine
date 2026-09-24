// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/character_body_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {

bool IsCharacterBody3DNodeDefinitionValidUVE(const CharacterBody3DNodeDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) && IsCharacterControllerComponentValidUVE(value.controller);
}

void ApplyCharacterBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const CharacterBody3DNodeDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplySolidBody3DBaseUVE(entityManager, entity, CharacterBody3DNodeDefinitionUVE::defaultName);
    if (!entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    }
    if (!entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<CharacterControllerComponentUVE>(entity, value.controller);
    }
}

} // namespace UVE::Scene
