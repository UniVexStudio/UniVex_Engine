// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/canvas_uve.h"

#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCanvasObjectDefinitionValidUVE(const CanvasObjectDefinitionUVE& value) noexcept {
    return IsCanvasComponentValidUVE(value.canvas);
}

void ApplyCanvasObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const CanvasObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<CanvasComponentUVE>(entity, value.canvas);
}

bool CanvasUVE::IsCanvasVisibleUVE(const CanvasComponentUVE& canvas) noexcept {
    return IsCanvasComponentValidUVE(canvas) && ResolveInheritedCanvasVisibilityUVE(&canvas, true);
}

bool CanvasUVE::IsEntityCanvasVisibleUVE(
    const IEntityManagerUVE& entityManager, const EntityUVE entity) noexcept {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<CanvasComponentUVE>(entity)) {
        return false;
    }
    const CanvasComponentUVE& canvas = entityManager.GetComponentUVE<CanvasComponentUVE>(entity);
    if (!IsCanvasVisibleUVE(canvas)) {
        return false;
    }
    const VisibilityComponentUVE* const visibility =
        entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)
            ? &entityManager.GetComponentUVE<VisibilityComponentUVE>(entity)
            : nullptr;
    return IsEffectivelyVisibleUVE(visibility);
}

std::int32_t CanvasUVE::EffectiveSortOrderUVE(const CanvasComponentUVE& canvas) noexcept {
    return IsCanvasComponentValidUVE(canvas) ? ResolveEffectiveCanvasSortOrderUVE(&canvas, 0) : 0;
}

bool CanvasUVE::RendersBeforeUVE(
    const CanvasComponentUVE& lhs, const CanvasComponentUVE& rhs) noexcept {
    return EffectiveSortOrderUVE(lhs) < EffectiveSortOrderUVE(rhs);
}

} // namespace UVE::Scene
