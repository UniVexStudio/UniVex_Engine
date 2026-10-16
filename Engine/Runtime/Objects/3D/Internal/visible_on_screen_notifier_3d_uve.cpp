// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/visible_on_screen_notifier_3d_uve.h"

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsVisibleOnScreenNotifier3DComponentValidUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.aabbCenterOffset) &&
           IsFinite3DObjectVectorUVE(value.aabbHalfExtents) && value.aabbHalfExtents.x > 0.0F &&
           value.aabbHalfExtents.y > 0.0F && value.aabbHalfExtents.z > 0.0F;
}

VisibleOnScreenNotifier3DComponentUVE SanitizeVisibleOnScreenNotifier3DComponentUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value) noexcept {
    VisibleOnScreenNotifier3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.aabbCenterOffset)) {
        out.aabbCenterOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVectorUVE(out.aabbHalfExtents) || out.aabbHalfExtents.x <= 0.0F ||
        out.aabbHalfExtents.y <= 0.0F || out.aabbHalfExtents.z <= 0.0F) {
        out.aabbHalfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    }
    return out;
}

Math::AabbUVE ResolveVisibleOnScreenNotifier3DWorldAabbUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value,
    const WorldTransformComponentUVE& worldTransform) noexcept {
    const VisibleOnScreenNotifier3DComponentUVE safe =
        SanitizeVisibleOnScreenNotifier3DComponentUVE(value);
    const Math::AabbUVE localBox =
        Math::AabbUVE::FromCenterExtentsUVE(safe.aabbCenterOffset, safe.aabbHalfExtents);
    if (!IsWorldTransformComponentValidUVE(worldTransform)) {
        return localBox;
    }
    return localBox.TransformUVE(BuildWorldTransformMatrixUVE(worldTransform));
}

void StepVisibleOnScreenNotifier3DStateUVE(VisibleOnScreenNotifier3DComponentUVE& value,
                                           const bool currentlyVisible) noexcept {
    const bool wasOnScreen = value.isOnScreen;
    value.isOnScreen = currentlyVisible;
    value.enteredScreenThisFrame = !wasOnScreen && currentlyVisible;
    value.exitedScreenThisFrame = wasOnScreen && !currentlyVisible;
}

bool IsVisibleOnScreenNotifier3DObjectDefinitionValidUVE(
    const VisibleOnScreenNotifier3DObjectDefinitionUVE& value) noexcept {
    return IsVisibleOnScreenNotifier3DComponentValidUVE(value.notifier);
}

void ApplyVisibleOnScreenNotifier3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const VisibleOnScreenNotifier3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              VisibleOnScreenNotifier3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<VisibleOnScreenNotifier3DComponentUVE>(
        entity, SanitizeVisibleOnScreenNotifier3DComponentUVE(value.notifier));
}

std::size_t UpdateVisibleOnScreenNotifier3DObjectsUVE(
    IEntityManagerUVE& entityManager, const Math::FrustumUVE* const activeCameraFrustum) {
    std::size_t visibleCount = 0U;
    entityManager.ForEachUVE<VisibleOnScreenNotifier3DComponentUVE>(
        [&](const EntityUVE entity, VisibleOnScreenNotifier3DComponentUVE& notifier) {
            if (!notifier.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsVisibleOnScreenNotifier3DComponentValidUVE(notifier) ||
                activeCameraFrustum == nullptr ||
                !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                StepVisibleOnScreenNotifier3DStateUVE(notifier, false);
                return;
            }
            const WorldTransformComponentUVE& world =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            if (!IsWorldTransformComponentValidUVE(world)) {
                StepVisibleOnScreenNotifier3DStateUVE(notifier, false);
                return;
            }
            const Math::AabbUVE worldAabb =
                ResolveVisibleOnScreenNotifier3DWorldAabbUVE(notifier, world);
            const bool visible = activeCameraFrustum->IntersectsUVE(worldAabb);
            StepVisibleOnScreenNotifier3DStateUVE(notifier, visible);
            if (visible) {
                ++visibleCount;
            }
        });
    return visibleCount;
}

} // namespace UVE::Scene
