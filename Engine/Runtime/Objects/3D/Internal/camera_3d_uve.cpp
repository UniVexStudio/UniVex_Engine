// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/camera_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsCamera3DObjectDefinitionValidUVE(const Camera3DObjectDefinitionUVE& value) noexcept {
    return IsCameraComponentValidUVE(value.camera);
}

void ApplyCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Camera3DObjectDefinitionUVE& value) {
    // Camera3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, Camera3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CameraComponentUVE>(entity, value.camera);
}

} // namespace UVE::Scene
