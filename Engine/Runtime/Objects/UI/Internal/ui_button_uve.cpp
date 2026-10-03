// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas_layer/ui_button_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIButtonObjectDefinitionValidUVE(const UIButtonObjectDefinitionUVE& value) noexcept {
    return IsUIButtonComponentValidUVE(value.button);
}

void ApplyUIButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UIButtonObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIButtonComponentUVE>(entity, value.button);
}

} // namespace UVE::Scene
