// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_folder.h"

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

void ApplyFolderObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                  const FolderObjectDefinition& value) {
    static_cast<void>(value);
    EnsureObjectBaseline(entityManager, entity, FolderObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<FolderComponent>(entity)) {
        entityManager.AddComponent<FolderComponent>(entity, FolderComponent{});
    }
}

void ApplyViewport3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                    const Viewport3DObjectDefinition& value) {
    static_cast<void>(value);
    EnsureObjectBaseline(entityManager, entity, Viewport3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<OutlinerViewport3DComponent>(entity)) {
        entityManager.AddComponent<OutlinerViewport3DComponent>(entity, OutlinerViewport3DComponent{});
    }
}

} // namespace UVE::Scene
