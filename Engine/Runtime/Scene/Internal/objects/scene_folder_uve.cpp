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

void ApplyViewportObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const ViewportObjectDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureObjectBaselineUVE(entityManager, entity, ViewportObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<OutlinerViewportComponentUVE>(entity)) {
        entityManager.AddComponentUVE<OutlinerViewportComponentUVE>(entity, OutlinerViewportComponentUVE{});
    }
}

} // namespace UVE::Scene
