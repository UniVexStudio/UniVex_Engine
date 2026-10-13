// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/objects/scene_folder_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

void ApplyFolderObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const FolderObjectDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureObjectBaselineUVE(entityManager, entity, FolderObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<FolderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<FolderComponentUVE>(entity, FolderComponentUVE{});
    }
}

void ApplyViewport3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Viewport3DObjectDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureObjectBaselineUVE(entityManager, entity, Viewport3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<OutlinerViewport3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<OutlinerViewport3DComponentUVE>(entity, OutlinerViewport3DComponentUVE{});
    }
}

} // namespace UVE::Scene
