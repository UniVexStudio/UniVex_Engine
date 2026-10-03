// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_text_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUITextObjectDefinitionValidUVE(const UITextObjectDefinitionUVE& value) noexcept {
    return IsUITextComponentValidUVE(value.text);
}

void ApplyUITextObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UITextObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UITextComponentUVE>(entity, value.text);
}

} // namespace UVE::Scene
