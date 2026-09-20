// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/canvas_layer/ui_image_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsUIImageNodeDefinitionValidUVE(const UIImageNodeDefinitionUVE& value) noexcept {
    return IsUIImageComponentValidUVE(value.image);
}

void ApplyUIImageNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const UIImageNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIImageComponentUVE>(entity, value.image);
}

} // namespace UVE::Scene
