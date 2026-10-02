// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/canvas_layer/canvas_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCanvasNodeDefinitionValidUVE(const CanvasNodeDefinitionUVE& value) noexcept {
    return IsCanvasComponentValidUVE(value.canvas);
}

void ApplyCanvasNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const CanvasNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<CanvasComponentUVE>(entity, value.canvas);
}

} // namespace UVE::Scene
