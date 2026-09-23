// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_root_uve.h"

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

/// Folds the root's old transform into each direct child's local transform, so removing the root's
/// transform moves nothing on screen. Scenes saved before the root became a pure Node could have
/// moved, rotated or scaled it, and every child's world pose was composed through it.
///
/// Uses the same composition SceneGraphUVE::UpdateUVE applies, so the baked local transform is
/// exactly the world transform the child had. A top-level child never composed from the root, so it
/// is left alone.
void BakeRootTransformIntoChildrenUVE(IEntityManagerUVE& entityManager, const EntityUVE root,
                                      const TransformComponentUVE& rootTransform) {
    if (IsIdentityTransformUVE(rootTransform)) {
        return; // The common case: nothing to fold in, and nothing to touch.
    }
    std::vector<EntityUVE> children;
    entityManager.ForEachUVE<HierarchyComponentUVE, TransformComponentUVE>(
        [&children, root](const EntityUVE entity, HierarchyComponentUVE& hierarchy, TransformComponentUVE& local) {
            if (hierarchy.parent == root && !local.topLevel) {
                children.push_back(entity);
            }
        });
    for (const EntityUVE child : children) {
        TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(child);
        const Math::Vector3UVE scaled = rootTransform.localScale * local.localPosition;
        local.localPosition =
            rootTransform.localPosition + Math::RotateVectorUVE(rootTransform.localRotation, scaled);
        const bool rotationChanged = !(rootTransform.localRotation == Math::QuaternionUVE{});
        local.localRotation = Math::MultiplyUVE(rootTransform.localRotation, local.localRotation);
        local.localScale = rootTransform.localScale * local.localScale;
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

void ApplySceneRootNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const SceneRootNodeDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The root is a pure Node: in the hierarchy, named, with no transform of its own. A transform
    // belongs to Node3D; the root has nothing to place, and children under it start their own
    // transform chains. This is also the one place an older root - created when every root carried a
    // transform - is migrated, so it runs on every load and repair, and is idempotent.
    if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        BakeRootTransformIntoChildrenUVE(entityManager, entity,
                                         entityManager.GetComponentUVE<TransformComponentUVE>(entity));
        entityManager.RemoveComponentUVE<TransformComponentUVE>(entity);
    }
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        entityManager.RemoveComponentUVE<WorldTransformComponentUVE>(entity);
    }
    EnsureComponentUVE<HierarchyComponentUVE>(entityManager, entity);
    if (!entityManager.HasComponentUVE<NameComponentUVE>(entity)) {
        entityManager.AddComponentUVE<NameComponentUVE>(
            entity, NameComponentUVE{std::string{SceneRootNodeDefinitionUVE::defaultName}});
    }
    EnsureComponentUVE<SceneRootComponentUVE>(entityManager, entity);

    // The root's Inspector is exactly the common Node section, always present rather than added:
    // the root offers no Add Component, so anything not attached here could never be reached.
    // Every one defaults to Inherit / empty, which changes nothing about how the scene runs.
    EnsureComponentUVE<ProcessComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ThreadGroupComponentUVE>(entityManager, entity);
    EnsureComponentUVE<PhysicsInterpolationComponentUVE>(entityManager, entity);
    EnsureComponentUVE<AutoTranslateComponentUVE>(entityManager, entity);
    EnsureComponentUVE<EditorDescriptionComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ScriptComponentUVE>(entityManager, entity);
    EnsureComponentUVE<NodeMetadataComponentUVE>(entityManager, entity);
}

} // namespace UVE::Scene
