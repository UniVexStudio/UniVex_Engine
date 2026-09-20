// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/light_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsLight3DNodeDefinitionValidUVE(const Light3DNodeDefinitionUVE& value) noexcept {
    // Any light type is a valid Light3D recipe — Directional is merely the creation default
    // (the classic "sun light"); Point and Spot start life as one inspector edit away.
    return IsLightComponentValidUVE(value.light);
}

void ApplyLight3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Light3DNodeDefinitionUVE& value) {
    // Light3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Light3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<LightComponentUVE>(entity, value.light);
}

} // namespace UVE::Scene
