// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/objects/scene_root_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<ComponentT>(entity)) {
        entityManager.AddComponentUVE<ComponentT>(entity, ComponentT{});
    }
}

} // namespace

void ApplySceneRootObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootObjectDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The root is a pure Object: in the hierarchy, named, with no transform of its own - children
    // under it start their own transform chains. The shared baseline also migrates an older root
    // created when every root carried a transform, so this runs on every load and repair.
    EnsureObjectBaselineUVE(entityManager, entity, SceneRootObjectDefinitionUVE::defaultName);
    EnsureComponentUVE<SceneRootComponentUVE>(entityManager, entity);
}

} // namespace UVE::Scene
