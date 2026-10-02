// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/static_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsStatic3DNodeDefinitionValidUVE(const Static3DNodeDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyStatic3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Static3DNodeDefinitionUVE& value) {
    // Static3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Static3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
