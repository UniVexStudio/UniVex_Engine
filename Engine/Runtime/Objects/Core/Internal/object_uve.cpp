// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/object/object_uve.h"

#include <string>
#include <vector>

#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
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

/// Folds the object's old transform into each direct child's local transform, so removing the object's
/// transform moves nothing on screen. Scenes saved before the object became a pure Object could have
/// moved, rotated or scaled it, and every child's world pose was composed through it.
///
/// Uses the same composition SceneGraphUVE::UpdateUVE applies, so the baked local transform is
/// exactly the world transform the child had. A top-level child never composed from the object, so it
/// is left alone.
void BakeTransformIntoChildrenUVE(IEntityManagerUVE& entityManager, const EntityUVE object,
                                  const TransformComponentUVE& objectTransform) {
    if (IsIdentityTransformUVE(objectTransform)) {
        return; // The common case: nothing to fold in, and nothing to touch.
    }
    std::vector<EntityUVE> children;
    entityManager.ForEachUVE<HierarchyComponentUVE, TransformComponentUVE>(
        [&children, object](const EntityUVE entity, HierarchyComponentUVE& hierarchy, TransformComponentUVE& local) {
            if (hierarchy.parent == object && !local.topLevel) {
                children.push_back(entity);
            }
        });
    for (const EntityUVE child : children) {
        TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(child);
        const Math::Vector3UVE scaled = objectTransform.localScale * local.localPosition;
        local.localPosition =
            objectTransform.localPosition + Math::RotateVectorUVE(objectTransform.localRotation, scaled);
        const bool rotationChanged = !(objectTransform.localRotation == Math::QuaternionUVE{});
        local.localRotation = Math::MultiplyUVE(objectTransform.localRotation, local.localRotation);
        local.localScale = objectTransform.localScale * local.localScale;
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

void EnsureCommonObjectSectionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureComponentUVE<ProcessComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ThreadGroupComponentUVE>(entityManager, entity);
    EnsureComponentUVE<PhysicsInterpolationComponentUVE>(entityManager, entity);
    EnsureComponentUVE<AutoTranslateComponentUVE>(entityManager, entity);
    EnsureComponentUVE<EditorDescriptionComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ScriptComponentUVE>(entityManager, entity);
    EnsureComponentUVE<ObjectMetadataComponentUVE>(entityManager, entity);
}

void ApplyObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                              const ObjectDefinitionUVE& value) {
    static_cast<void>(value);
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The Object is a pure Object: in the hierarchy, named, with no transform of its own - children
    // under it start their own transform chains. The shared baseline also migrates an older root
    // created when every root carried a transform, so this runs on every load and repair.
    EnsureObjectBaselineUVE(entityManager, entity, ObjectDefinitionUVE::defaultName);
    EnsureComponentUVE<ObjectComponentUVE>(entityManager, entity);
}

void EnsureObjectBaselineUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    // A pure Object has no transform. The creation shell attaches one to every entity, and scenes
    // saved before a kind became a pure Object carry one too; it is folded into the children, so
    // removing it moves nothing on screen, and then removed.
    if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        BakeTransformIntoChildrenUVE(entityManager, entity,
                                     entityManager.GetComponentUVE<TransformComponentUVE>(entity));
        entityManager.RemoveComponentUVE<TransformComponentUVE>(entity);
    }
    if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        entityManager.RemoveComponentUVE<WorldTransformComponentUVE>(entity);
    }
    // Visibility is a Object3D property: a pure Object draws nothing, so it has nothing to hide.
    if (entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)) {
        entityManager.RemoveComponentUVE<VisibilityComponentUVE>(entity);
    }
    EnsureComponentUVE<HierarchyComponentUVE>(entityManager, entity);
    if (!entityManager.HasComponentUVE<NameComponentUVE>(entity)) {
        entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{std::string{nameFallback}});
    }
    EnsureCommonObjectSectionUVE(entityManager, entity);
}

} // namespace UVE::Scene
