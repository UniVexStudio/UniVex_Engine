// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/script_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsScriptNodeDefinitionValidUVE(const ScriptNodeDefinitionUVE& value) noexcept {
    return IsScriptComponentValidUVE(value.script);
}

void ApplyScriptNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const ScriptNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<ScriptComponentUVE>(entity, value.script);
}

} // namespace UVE::Scene
