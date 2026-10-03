// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/rigid_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsRigid3DObjectDefinitionValidUVE(const Rigid3DObjectDefinitionUVE& value) noexcept {
    return IsRigid3DComponentValidUVE(value.body);
}

void ApplyRigid3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Rigid3DObjectDefinitionUVE& value) {
    // Rigid3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Rigid3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
}

} // namespace UVE::Scene
