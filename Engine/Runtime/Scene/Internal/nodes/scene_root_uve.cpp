// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_root_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<ComponentT>(entity)) {
        entityManager.AddComponentUVE<ComponentT>(entity, ComponentT{});
    }
}

} // namespace

void ApplySceneRootNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootNodeDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The root is a pure Node: in the hierarchy, named, with no transform of its own - children
    // under it start their own transform chains. The shared baseline also migrates an older root
    // created when every root carried a transform, so this runs on every load and repair.
    EnsureNodeBaselineUVE(entityManager, entity, SceneRootNodeDefinitionUVE::defaultName);
    EnsureComponentUVE<SceneRootComponentUVE>(entityManager, entity);
}

} // namespace UVE::Scene
