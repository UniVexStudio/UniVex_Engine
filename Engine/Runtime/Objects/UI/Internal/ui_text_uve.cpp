// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/canvas_layer/ui_text_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUITextNodeDefinitionValidUVE(const UITextNodeDefinitionUVE& value) noexcept {
    return IsUITextComponentValidUVE(value.text);
}

void ApplyUITextNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UITextNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<UITextComponentUVE>(entity, value.text);
}

} // namespace UVE::Scene
