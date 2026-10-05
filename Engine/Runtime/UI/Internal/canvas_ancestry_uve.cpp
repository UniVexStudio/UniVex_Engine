// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/canvas_ancestry_uve.h"

#include "uve/component/canvas_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::UI {

CanvasAncestryUVE ResolveCanvasAncestryUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE entity) {
    CanvasAncestryUVE ancestry{};
    if (!entityManager.IsAliveUVE(entity)) {
        ancestry.visible = false;
        return ancestry;
    }
    Scene::EntityUVE current = entity;
    for (std::size_t depth = 0; depth < kMaximumCanvasAncestorWalkUVE && current != Scene::kInvalidEntityUVE;
         ++depth) {
        if (!entityManager.IsAliveUVE(current)) {
            break;
        }
        if (entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(current)) {
            const Scene::CanvasComponentUVE& canvas =
                entityManager.GetComponentUVE<Scene::CanvasComponentUVE>(current);
            if (!canvas.visible) {
                ancestry.visible = false;
            }
            if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(current) &&
                !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(current).visibleInHierarchy) {
                ancestry.visible = false;
            }
            if (!ancestry.hasCanvas) {
                ancestry.hasCanvas = true;
                ancestry.canvas = current;
                ancestry.sortOrder = canvas.sortOrder;
            }
        }
        if (!entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(current)) {
            break;
        }
        const Scene::EntityUVE parent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(current).parent;
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return ancestry;
}

bool ShouldDrawUiWidgetUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return false;
    }
    if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) &&
        !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
        return false;
    }
    return ResolveCanvasAncestryUVE(entityManager, entity).visible;
}

} // namespace UVE::UI
