// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/static_body_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsStaticBody3DNodeDefinitionValidUVE(const StaticBody3DNodeDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyStaticBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const StaticBody3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

} // namespace UVE::Scene
