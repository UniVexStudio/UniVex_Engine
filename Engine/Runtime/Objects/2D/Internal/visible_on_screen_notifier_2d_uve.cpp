// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/visible_on_screen_notifier_2d_uve.h"

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool DoRectsOverlapUVE(const Math::RectUVE& a, const Math::RectUVE& b) noexcept {
    if (!Math::IsFiniteUVE(a) || !Math::IsFiniteUVE(b) || a.size.x <= 0.0F || a.size.y <= 0.0F ||
        b.size.x <= 0.0F || b.size.y <= 0.0F) {
        return false;
    }
    return a.position.x < (b.position.x + b.size.x) && (a.position.x + a.size.x) > b.position.x &&
           a.position.y < (b.position.y + b.size.y) && (a.position.y + a.size.y) > b.position.y;
}

} // namespace

bool IsVisibleOnScreenNotifier2DComponentValidUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value) noexcept {
    return Math::IsFiniteUVE(value.localRect) && value.localRect.size.x > 0.0F &&
           value.localRect.size.y > 0.0F;
}

VisibleOnScreenNotifier2DComponentUVE SanitizeVisibleOnScreenNotifier2DComponentUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value) noexcept {
    VisibleOnScreenNotifier2DComponentUVE out = value;
    if (!Math::IsFiniteUVE(out.localRect) || out.localRect.size.x <= 0.0F ||
        out.localRect.size.y <= 0.0F) {
        out.localRect = Math::RectUVE{Math::Vector2UVE{-10.0F, -10.0F},
                                      Math::Vector2UVE{20.0F, 20.0F}};
    }
    return out;
}

Math::RectUVE ResolveVisibleOnScreenNotifier2DWorldRectUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value,
    const Math::Vector2UVE& worldPosition2D) noexcept {
    const VisibleOnScreenNotifier2DComponentUVE safe =
        SanitizeVisibleOnScreenNotifier2DComponentUVE(value);
    if (!Math::IsFiniteUVE(worldPosition2D)) {
        return safe.localRect;
    }
    return Math::RectUVE{worldPosition2D + safe.localRect.position, safe.localRect.size};
}

void StepVisibleOnScreenNotifier2DStateUVE(VisibleOnScreenNotifier2DComponentUVE& value,
                                           const bool currentlyVisible) noexcept {
    const bool wasOnScreen = value.isOnScreen;
    value.isOnScreen = currentlyVisible;
    value.enteredScreenThisFrame = !wasOnScreen && currentlyVisible;
    value.exitedScreenThisFrame = wasOnScreen && !currentlyVisible;
}

bool IsVisibleOnScreenNotifier2DObjectDefinitionValidUVE(
    const VisibleOnScreenNotifier2DObjectDefinitionUVE& value) noexcept {
    return IsVisibleOnScreenNotifier2DComponentValidUVE(value.notifier);
}

void ApplyVisibleOnScreenNotifier2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const VisibleOnScreenNotifier2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<VisibleOnScreenNotifier2DComponentUVE>(
        entity, SanitizeVisibleOnScreenNotifier2DComponentUVE(value.notifier));
}

std::size_t UpdateVisibleOnScreenNotifier2DObjectsUVE(IEntityManagerUVE& entityManager,
                                                      const Math::RectUVE* const activeViewRect) {
    std::size_t visibleCount = 0U;
    entityManager.ForEachUVE<VisibleOnScreenNotifier2DComponentUVE>(
        [&](const EntityUVE entity, VisibleOnScreenNotifier2DComponentUVE& notifier) {
            if (!notifier.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsVisibleOnScreenNotifier2DComponentValidUVE(notifier) ||
                activeViewRect == nullptr) {
                StepVisibleOnScreenNotifier2DStateUVE(notifier, false);
                return;
            }
            Math::Vector2UVE pos2D{};
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                const WorldTransformComponentUVE& world =
                    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
                if (!IsWorldTransformComponentValidUVE(world)) {
                    StepVisibleOnScreenNotifier2DStateUVE(notifier, false);
                    return;
                }
                pos2D = Math::Vector2UVE{world.worldPosition.x, world.worldPosition.y};
            } else if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                const TransformComponentUVE& local =
                    entityManager.GetComponentUVE<TransformComponentUVE>(entity);
                if (!IsTransformComponentValidUVE(local)) {
                    StepVisibleOnScreenNotifier2DStateUVE(notifier, false);
                    return;
                }
                pos2D = Math::Vector2UVE{local.localPosition.x, local.localPosition.y};
            }
            const Math::RectUVE worldRect =
                ResolveVisibleOnScreenNotifier2DWorldRectUVE(notifier, pos2D);
            const bool visible = DoRectsOverlapUVE(worldRect, *activeViewRect);
            StepVisibleOnScreenNotifier2DStateUVE(notifier, visible);
            if (visible) {
                ++visibleCount;
            }
        });
    return visibleCount;
}

} // namespace UVE::Scene
