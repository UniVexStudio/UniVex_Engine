// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_root.h"

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponent(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.HasComponent<ComponentT>(entity)) {
        entityManager.AddComponent<ComponentT>(entity, ComponentT{});
    }
}

} // namespace

void ApplySceneRootObjectDefinition(IEntityManager& entityManager, Entity entity,
                                     const SceneRootObjectDefinition& value) {
    static_cast<void>(value);
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    // The root is a pure Object: in the hierarchy, named, with no transform of its own - children
    // under it start their own transform chains. The shared baseline also migrates an older root
    // created when every root carried a transform, so this runs on every load and repair.
    EnsureObjectBaseline(entityManager, entity, SceneRootObjectDefinition::defaultName);
    EnsureComponent<SceneRootComponent>(entityManager, entity);
}

} // namespace UVE::Scene
