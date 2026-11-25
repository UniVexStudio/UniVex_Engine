// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "canvas.h"

#include "visibility_component.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsCanvasObjectDefinitionValid(const CanvasObjectDefinition& value) noexcept {
    return IsCanvasComponentValid(value.canvas);
}

void ApplyCanvasObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                    const CanvasObjectDefinition& value) {
    entityManager.AddComponent<CanvasComponent>(entity, value.canvas);
}

bool Canvas::IsCanvasVisible(const CanvasComponent& canvas) noexcept {
    return IsCanvasComponentValid(canvas) && ResolveInheritedCanvasVisibility(&canvas, true);
}

bool Canvas::IsEntityCanvasVisible(
    const IEntityManager& entityManager, const Entity entity) noexcept {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<CanvasComponent>(entity)) {
        return false;
    }
    const CanvasComponent& canvas = entityManager.GetComponent<CanvasComponent>(entity);
    if (!IsCanvasVisible(canvas)) {
        return false;
    }
    const VisibilityComponent* const visibility =
        entityManager.HasComponent<VisibilityComponent>(entity)
            ? &entityManager.GetComponent<VisibilityComponent>(entity)
            : nullptr;
    return IsEffectivelyVisible(visibility);
}

std::int32_t Canvas::EffectiveSortOrder(const CanvasComponent& canvas) noexcept {
    return IsCanvasComponentValid(canvas) ? ResolveEffectiveCanvasSortOrder(&canvas, 0) : 0;
}

bool Canvas::RendersBefore(
    const CanvasComponent& lhs, const CanvasComponent& rhs) noexcept {
    return EffectiveSortOrder(lhs) < EffectiveSortOrder(rhs);
}

} // namespace UVE::Scene
