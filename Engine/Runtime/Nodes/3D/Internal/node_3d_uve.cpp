// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/node_3d_uve.h"

#include <string>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsNode3DNodeDefinitionValidUVE(const Node3DNodeDefinitionUVE& /*value*/) noexcept {
    // Node3D carries no authored data - a definition with default-initialized (that is,
    // absent) fields is always valid. The validator exists so the kind keeps the same
    // validate-before-apply seam as every other node kind.
    return true;
}

void ApplyNode3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const Node3DNodeDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureNode3DBaselineUVE(entityManager, entity, Node3DNodeDefinitionUVE::defaultName);
}

void EnsureNode3DBaselineUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // Per-component ensure, deliberately NOT SceneGraphUVE::AttachTransformUVE: that one is
    // all-or-nothing (it refuses an entity that already has any of the three transform
    // components), while this guarantee must also repair a partially-baselined entity - and in
    // both cases an existing component keeps its authored values untouched.
    if (!entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
    }
    if (!entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity, WorldTransformComponentUVE{});
    }
    if (!entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
        entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, HierarchyComponentUVE{});
    }
    if (!entityManager.HasComponentUVE<NameComponentUVE>(entity)) {
        entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{std::string{nameFallback}});
    }
}

} // namespace UVE::Scene
