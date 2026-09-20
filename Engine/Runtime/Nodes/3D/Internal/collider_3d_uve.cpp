// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/collider_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsCollider3DNodeDefinitionValidUVE(const Collider3DNodeDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyCollider3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Collider3DNodeDefinitionUVE& value) {
    // Collider3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Collider3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
