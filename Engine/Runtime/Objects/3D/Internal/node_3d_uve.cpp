// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/node_3d_uve.h"

#include <string>
#include <vector>

#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.HasComponentUVE<ComponentT>(entity)) {
        entityManager.AddComponentUVE<ComponentT>(entity, ComponentT{});
    }
}

[[nodiscard]] bool IsIdentityTransformUVE(const TransformComponentUVE& transform) noexcept {
    return transform.localPosition == Math::Vector3UVE{} && transform.localRotation == Math::QuaternionUVE{} &&
           transform.localScale == Math::Vector3UVE{1.0F, 1.0F, 1.0F};
}

/// Folds the node's old transform into each direct child's local transform, so removing the node's
/// transform moves nothing on screen. Scenes saved before the node became a pure Node could have
/// moved, rotated or scaled it, and every child's world pose was composed through it.
///
/// Uses the same composition SceneGraphUVE::UpdateUVE applies, so the baked local transform is
/// exactly the world transform the child had. A top-level child never composed from the node, so it
/// is left alone.
void BakeTransformIntoChildrenUVE(IEntityManagerUVE& entityManager, const EntityUVE node,
                                      const TransformComponentUVE& nodeTransform) {
    if (IsIdentityTransformUVE(nodeTransform)) {
        return; // The common case: nothing to fold in, and nothing to touch.
    }
    std::vector<EntityUVE> children;
    entityManager.ForEachUVE<HierarchyComponentUVE, TransformComponentUVE>(
        [&children, node](const EntityUVE entity, HierarchyComponentUVE& hierarchy, TransformComponentUVE& local) {
            if (hierarchy.parent == node && !local.topLevel) {
                children.push_back(entity);
            }
        });
    for (const EntityUVE child : children) {
        TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(child);
        const Math::Vector3UVE scaled = nodeTransform.localScale * local.localPosition;
        local.localPosition =
            nodeTransform.localPosition + Math::RotateVectorUVE(nodeTransform.localRotation, scaled);
        const bool rotationChanged = !(nodeTransform.localRotation == Math::QuaternionUVE{});
        local.localRotation = Math::MultiplyUVE(nodeTransform.localRotation, local.localRotation);
        local.localScale = nodeTransform.localScale * local.localScale;
        if (rotationChanged) {
            // The stored Euler angles described the old rotation. Making the quaternion the truth is
            // what a gizmo drag does too; without it the next Euler edit would snap the child back.
            local.rotationEditMode = RotationEditModeUVE::Quaternion;
        }
        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(child)) {
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).dirty = true;
        }
    }
}

} // namespace

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
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // A Node3D's Inspector is Transform, Visibility and the Node section, and it has no Add
    // Component, so everything it shows is attached here.
    EnsureComponentUVE<VisibilityComponentUVE>(entityManager, entity);
    EnsureCommonNodeSectionUVE(entityManager, entity);
}

void EnsureCommonNodeSectionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureComponentUVE<ProcessComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ThreadGroupComponentUVE>(entityManager, entity);
    EnsureComponentUVE<PhysicsInterpolationComponentUVE>(entityManager, entity);
    EnsureComponentUVE<AutoTranslateComponentUVE>(entityManager, entity);
    EnsureComponentUVE<EditorDescriptionComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ScriptComponentUVE>(entityManager, entity);
    EnsureComponentUVE<NodeMetadataComponentUVE>(entityManager, entity);
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

void EnsureNodeBaselineUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                           const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // A pure Node has no transform. The creation shell attaches one to every entity, and scenes
    // saved before a kind became a pure Node carry one too; it is folded into the children, so
    // removing it moves nothing on screen, and then removed.
    if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        BakeTransformIntoChildrenUVE(entityManager, entity,
                                     entityManager.GetComponentUVE<TransformComponentUVE>(entity));
        entityManager.RemoveComponentUVE<TransformComponentUVE>(entity);
    }
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        entityManager.RemoveComponentUVE<WorldTransformComponentUVE>(entity);
    }
    // Visibility is a Node3D property: a pure Node draws nothing, so it has nothing to hide.
    if (entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)) {
        entityManager.RemoveComponentUVE<VisibilityComponentUVE>(entity);
    }
    EnsureComponentUVE<HierarchyComponentUVE>(entityManager, entity);
    if (!entityManager.HasComponentUVE<NameComponentUVE>(entity)) {
        entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{std::string{nameFallback}});
    }
    EnsureCommonNodeSectionUVE(entityManager, entity);
}

} // namespace UVE::Scene
