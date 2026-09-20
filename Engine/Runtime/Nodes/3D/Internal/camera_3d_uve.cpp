// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/camera_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsCamera3DNodeDefinitionValidUVE(const Camera3DNodeDefinitionUVE& value) noexcept {
    return IsCameraComponentValidUVE(value.camera);
}

void ApplyCamera3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Camera3DNodeDefinitionUVE& value) {
    // Camera3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, Camera3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CameraComponentUVE>(entity, value.camera);
}

} // namespace UVE::Scene
