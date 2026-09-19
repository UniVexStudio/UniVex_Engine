// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_root_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ApplySceneRootNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootNodeDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity) ||
        entityManager.HasComponentUVE<SceneRootComponentUVE>(entity)) {
        return;
    }
    entityManager.AddComponentUVE<SceneRootComponentUVE>(entity, SceneRootComponentUVE{});
}

} // namespace UVE::Scene
