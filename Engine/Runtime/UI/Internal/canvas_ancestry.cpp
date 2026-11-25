// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "canvas_ancestry.h"

#include "canvas_component.h"
#include "hierarchy_component.h"
#include "visibility_component.h"
#include "i_entity_manager.h"
#include "canvas.h"

namespace UVE::UI {

CanvasAncestry ResolveCanvasAncestry(Scene::IEntityManager& entityManager, Scene::Entity entity) {
    CanvasAncestry ancestry{};
    if (!entityManager.IsAlive(entity)) {
        ancestry.visible = false;
        return ancestry;
    }
    Scene::Entity current = entity;
    for (std::size_t depth = 0; depth < kMaximumCanvasAncestorWalk && current != Scene::kInvalidEntity;
         ++depth) {
        if (!entityManager.IsAlive(current)) {
            break;
        }
        if (entityManager.HasComponent<Scene::CanvasComponent>(current)) {
            const Scene::CanvasComponent& canvas =
                entityManager.GetComponent<Scene::CanvasComponent>(current);
            if (!Scene::Canvas::IsEntityCanvasVisible(entityManager, current)) {
                ancestry.visible = false;
            }
            if (!ancestry.hasCanvas) {
                ancestry.hasCanvas = true;
                ancestry.canvas = current;
                ancestry.sortOrder = Scene::Canvas::EffectiveSortOrder(canvas);
            }
        }
        if (!entityManager.HasComponent<Scene::HierarchyComponent>(current)) {
            break;
        }
        const Scene::Entity parent = entityManager.GetComponent<Scene::HierarchyComponent>(current).parent;
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return ancestry;
}

bool ShouldDrawUiWidget(Scene::IEntityManager& entityManager, const Scene::Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return false;
    }
    const Scene::VisibilityComponent* const visibility =
        entityManager.HasComponent<Scene::VisibilityComponent>(entity)
            ? &entityManager.GetComponent<Scene::VisibilityComponent>(entity)
            : nullptr;
    if (!Scene::IsEffectivelyVisible(visibility)) {
        return false;
    }
    return ResolveCanvasAncestry(entityManager, entity).visible;
}

std::size_t UIHierarchyDepth(Scene::IEntityManager& entityManager, const Scene::Entity entity) {
    std::size_t depth = 0U;
    Scene::Entity cursor = entity;
    // The ancestry cap, reused: a cycle bottoms out at the cap instead of hanging the frame.
    for (std::size_t walk = 0U; walk < kMaximumCanvasAncestorWalk; ++walk) {
        if (!entityManager.IsAlive(cursor) ||
            !entityManager.HasComponent<Scene::HierarchyComponent>(cursor)) {
            return depth;
        }
        const Scene::Entity parent =
            entityManager.GetComponent<Scene::HierarchyComponent>(cursor).parent;
        if (parent == Scene::kInvalidEntity || parent == cursor) {
            return depth;
        }
        cursor = parent;
        ++depth;
    }
    return depth;
}

} // namespace UVE::UI
