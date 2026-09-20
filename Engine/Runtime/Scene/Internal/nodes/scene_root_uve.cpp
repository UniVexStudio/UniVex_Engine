// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_root_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

void ApplySceneRootNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootNodeDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The root stands on the same baseline as every other 3D scene node - one guarantee, one
    // owner, so a root repaired after a partial load is indistinguishable from a root the
    // document lifecycle created through the shell.
    EnsureNode3DBaselineUVE(entityManager, entity, SceneRootNodeDefinitionUVE::defaultName);
    if (!entityManager.HasComponentUVE<SceneRootComponentUVE>(entity)) {
        entityManager.AddComponentUVE<SceneRootComponentUVE>(entity, SceneRootComponentUVE{});
    }
}

} // namespace UVE::Scene
