// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/canvas_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCanvasObjectDefinitionValidUVE(const CanvasObjectDefinitionUVE& value) noexcept {
    return IsCanvasComponentValidUVE(value.canvas);
}

void ApplyCanvasObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const CanvasObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<CanvasComponentUVE>(entity, value.canvas);
}

} // namespace UVE::Scene
