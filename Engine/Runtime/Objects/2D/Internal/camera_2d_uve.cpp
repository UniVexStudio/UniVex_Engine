// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/camera_2d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCamera2DObjectDefinitionValidUVE(const Camera2DObjectDefinitionUVE& value) noexcept {
    return IsCamera2DComponentValidUVE(value.camera);
}

void ApplyCamera2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Camera2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Camera2DComponentUVE>(entity, value.camera);
}

} // namespace UVE::Scene
