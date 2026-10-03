// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/script_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsScriptObjectDefinitionValidUVE(const ScriptObjectDefinitionUVE& value) noexcept {
    return IsScriptComponentValidUVE(value.script);
}

void ApplyScriptObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const ScriptObjectDefinitionUVE& value) {
    // Script is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, ScriptObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ScriptComponentUVE>(entity, value.script);
}

} // namespace UVE::Scene
