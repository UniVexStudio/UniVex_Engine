// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/static_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsStatic3DObjectDefinitionValidUVE(const Static3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyStatic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Static3DObjectDefinitionUVE& value) {
    // Static3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Static3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
