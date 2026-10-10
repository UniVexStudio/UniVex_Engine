// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/object_3d_uve.h"

#include "uve/object/object_uve.h"

#include <string>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<ComponentT>(entity)) {
        entityManager.AddComponentUVE<ComponentT>(entity, ComponentT{});
    }
}

} // namespace

bool IsObject3DObjectDefinitionValidUVE(const Object3DObjectDefinitionUVE& /*value*/) noexcept {
    // Object3D carries no authored data - a definition with default-initialized (that is,
    // absent) fields is always valid. The validator exists so the kind keeps the same
    // validate-before-apply seam as every other object kind.
    return true;
}

void ApplyObject3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const Object3DObjectDefinitionUVE& value) {
    static_cast<void>(value);
    EnsureObject3DBaselineUVE(entityManager, entity, Object3DObjectDefinitionUVE::defaultName);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // A Object3D's Inspector is Transform, Visibility and the Object section, and it has no Add
    // Component, so everything it shows is attached here.
    EnsureComponentUVE<VisibilityComponentUVE>(entityManager, entity);
    EnsureCommonObjectSectionUVE(entityManager, entity);
}

void EnsureObject3DBaselineUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
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
