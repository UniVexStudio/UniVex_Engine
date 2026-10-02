// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/area_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsArea3DObjectDefinitionValidUVE(const Area3DObjectDefinitionUVE& value) noexcept {
    return IsAreaComponentValidUVE(value.area);
}

void ApplyArea3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Area3DObjectDefinitionUVE& value) {
    // Area3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Area3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AreaComponentUVE>(entity, value.area);
}

} // namespace UVE::Scene
