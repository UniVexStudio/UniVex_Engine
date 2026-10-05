// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/collider_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsCollider3DObjectDefinitionValidUVE(const Collider3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyCollider3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Collider3DObjectDefinitionUVE& value) {
    // Collider3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Collider3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
