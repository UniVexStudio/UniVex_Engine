// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/static_body_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsStaticBody3DNodeDefinitionValidUVE(const StaticBody3DNodeDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyStaticBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const StaticBody3DNodeDefinitionUVE& value) {
    // StaticBody3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, StaticBody3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
