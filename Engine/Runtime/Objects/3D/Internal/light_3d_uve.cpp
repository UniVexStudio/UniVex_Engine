// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/light_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsLight3DObjectDefinitionValidUVE(const Light3DObjectDefinitionUVE& value) noexcept {
    // Any light type is a valid Light3D recipe — Directional is merely the creation default
    // (the classic "sun light"); Point and Spot start life as one inspector edit away.
    return IsLightComponentValidUVE(value.light);
}

void ApplyLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Light3DObjectDefinitionUVE& value) {
    // Light3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Light3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<LightComponentUVE>(entity, value.light);
}

} // namespace UVE::Scene
