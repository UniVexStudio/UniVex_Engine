// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas_layer/ui_image_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIImageObjectDefinitionValidUVE(const UIImageObjectDefinitionUVE& value) noexcept {
    return IsUIImageComponentValidUVE(value.image);
}

void ApplyUIImageObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UIImageObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIImageComponentUVE>(entity, value.image);
}

} // namespace UVE::Scene
