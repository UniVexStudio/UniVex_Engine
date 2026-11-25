// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "object_3d.h"

#include <string>
#include <vector>

#include "auto_translate_component.h"
#include "editor_description_component.h"
#include "hierarchy_component.h"
#include "name_component.h"
#include "object_metadata_component.h"
#include "physics_interpolation_component.h"
#include "process_component.h"
#include "script_component.h"
#include "thread_group_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "quaternion.h"

namespace UVE::Scene {
namespace {

template <typename ComponentT>
void EnsureComponent(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.HasComponent<ComponentT>(entity)) {
        entityManager.AddComponent<ComponentT>(entity, ComponentT{});
    }
}

[[nodiscard]] bool IsIdentityTransform(const TransformComponent& transform) noexcept {
    return transform.localPosition == Math::Vector3{} && transform.localRotation == Math::Quaternion{} &&
           transform.localScale == Math::Vector3{1.0F, 1.0F, 1.0F};
}

/// Folds the object's old transform into each direct child's local transform, so removing the object's
/// transform moves nothing on screen. Scenes saved before the object became a pure Object could have
/// moved, rotated or scaled it, and every child's world pose was composed through it.
///
/// Uses the same composition SceneGraph::Update applies, so the baked local transform is
/// exactly the world transform the child had. A top-level child never composed from the object, so it
/// is left alone.
void BakeTransformIntoChildren(IEntityManager& entityManager, const Entity object,
                                      const TransformComponent& objectTransform) {
    if (IsIdentityTransform(objectTransform)) {
        return; // The common case: nothing to fold in, and nothing to touch.
    }
    std::vector<Entity> children;
    entityManager.ForEach<HierarchyComponent, TransformComponent>(
        [&children, object](const Entity entity, HierarchyComponent& hierarchy, TransformComponent& local) {
            if (hierarchy.parent == object && !local.topLevel) {
                children.push_back(entity);
            }
        });
    for (const Entity child : children) {
        TransformComponent& local = entityManager.GetComponent<TransformComponent>(child);
        const Math::Vector3 scaled = objectTransform.localScale * local.localPosition;
        local.localPosition =
            objectTransform.localPosition + Math::RotateVector(objectTransform.localRotation, scaled);
        const bool rotationChanged = !(objectTransform.localRotation == Math::Quaternion{});
        local.localRotation = Math::Multiply(objectTransform.localRotation, local.localRotation);
        local.localScale = objectTransform.localScale * local.localScale;
        if (rotationChanged) {
            // The stored Euler angles described the old rotation. Making the quaternion the truth is
            // what a gizmo drag does too; without it the next Euler edit would snap the child back.
            local.rotationEditMode = RotationEditMode::Quaternion;
        }
        if (entityManager.HasComponent<WorldTransformComponent>(child)) {
            entityManager.GetComponent<WorldTransformComponent>(child).dirty = true;
        }
    }
}

} // namespace

bool IsObject3DObjectDefinitionValid(const Object3DObjectDefinition& /*value*/) noexcept {
    // Object3D carries no authored data - a definition with default-initialized (that is,
    // absent) fields is always valid. The validator exists so the kind keeps the same
    // validate-before-apply seam as every other object kind.
    return true;
}

void ApplyObject3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                  const Object3DObjectDefinition& value) {
    static_cast<void>(value);
    EnsureObject3DBaseline(entityManager, entity, Object3DObjectDefinition::defaultName);
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    // A Object3D's Inspector is Transform, Visibility and the Object section, and it has no Add
    // Component, so everything it shows is attached here.
    EnsureComponent<VisibilityComponent>(entityManager, entity);
    EnsureCommonObjectSection(entityManager, entity);
}

void EnsureCommonObjectSection(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    EnsureComponent<ProcessComponent>(entityManager, entity);
    EnsureComponent<ThreadGroupComponent>(entityManager, entity);
    EnsureComponent<PhysicsInterpolationComponent>(entityManager, entity);
    EnsureComponent<AutoTranslateComponent>(entityManager, entity);
    EnsureComponent<EditorDescriptionComponent>(entityManager, entity);
    EnsureComponent<ScriptComponent>(entityManager, entity);
    EnsureComponent<ObjectMetadataComponent>(entityManager, entity);
}

void EnsureObject3DBaseline(IEntityManager& entityManager, const Entity entity,
                             const std::string_view nameFallback) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    // Per-component ensure, deliberately NOT SceneGraph::AttachTransform: that one is
    // all-or-nothing (it refuses an entity that already has any of the three transform
    // components), while this guarantee must also repair a partially-baselined entity - and in
    // both cases an existing component keeps its authored values untouched.
    if (!entityManager.HasComponent<TransformComponent>(entity)) {
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
    }
    if (!entityManager.HasComponent<WorldTransformComponent>(entity)) {
        entityManager.AddComponent<WorldTransformComponent>(entity, WorldTransformComponent{});
    }
    if (!entityManager.HasComponent<HierarchyComponent>(entity)) {
        entityManager.AddComponent<HierarchyComponent>(entity, HierarchyComponent{});
    }
    if (!entityManager.HasComponent<NameComponent>(entity)) {
        entityManager.AddComponent<NameComponent>(entity, NameComponent{std::string{nameFallback}});
    }
}

void EnsureObjectBaseline(IEntityManager& entityManager, const Entity entity,
                           const std::string_view nameFallback) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    // A pure Object has no transform. The creation shell attaches one to every entity, and scenes
    // saved before a kind became a pure Object carry one too; it is folded into the children, so
    // removing it moves nothing on screen, and then removed.
    if (entityManager.HasComponent<TransformComponent>(entity)) {
        BakeTransformIntoChildren(entityManager, entity,
                                     entityManager.GetComponent<TransformComponent>(entity));
        entityManager.RemoveComponent<TransformComponent>(entity);
    }
    if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
        entityManager.RemoveComponent<WorldTransformComponent>(entity);
    }
    // Visibility is a Object3D property: a pure Object draws nothing, so it has nothing to hide.
    if (entityManager.HasComponent<VisibilityComponent>(entity)) {
        entityManager.RemoveComponent<VisibilityComponent>(entity);
    }
    EnsureComponent<HierarchyComponent>(entityManager, entity);
    if (!entityManager.HasComponent<NameComponent>(entity)) {
        entityManager.AddComponent<NameComponent>(entity, NameComponent{std::string{nameFallback}});
    }
    EnsureCommonObjectSection(entityManager, entity);
}

} // namespace UVE::Scene
