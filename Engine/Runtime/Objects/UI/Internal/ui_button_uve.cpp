// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/canvas_layer/ui_button_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIButtonNodeDefinitionValidUVE(const UIButtonNodeDefinitionUVE& value) noexcept {
    return IsUIButtonComponentValidUVE(value.button);
}

void ApplyUIButtonNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UIButtonNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIButtonComponentUVE>(entity, value.button);
}

} // namespace UVE::Scene
