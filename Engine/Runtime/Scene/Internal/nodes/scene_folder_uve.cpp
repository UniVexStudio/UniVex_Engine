// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_folder_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

void ApplyFolderNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const FolderNodeDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureNodeBaselineUVE(entityManager, entity, FolderNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<FolderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<FolderComponentUVE>(entity, FolderComponentUVE{});
    }
}

} // namespace UVE::Scene
