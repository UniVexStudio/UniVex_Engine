// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "visible_on_screen_notifier_3d.h"

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsVisibleOnScreenNotifier3DComponentValid(
    const VisibleOnScreenNotifier3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.aabbCenterOffset) &&
           IsFinite3DObjectVector(value.aabbHalfExtents) && value.aabbHalfExtents.x > 0.0F &&
           value.aabbHalfExtents.y > 0.0F && value.aabbHalfExtents.z > 0.0F;
}

VisibleOnScreenNotifier3DComponent SanitizeVisibleOnScreenNotifier3DComponent(
    const VisibleOnScreenNotifier3DComponent& value) noexcept {
    VisibleOnScreenNotifier3DComponent out = value;
    if (!IsFinite3DObjectVector(out.aabbCenterOffset)) {
        out.aabbCenterOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVector(out.aabbHalfExtents) || out.aabbHalfExtents.x <= 0.0F ||
        out.aabbHalfExtents.y <= 0.0F || out.aabbHalfExtents.z <= 0.0F) {
        out.aabbHalfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    }
    return out;
}

Math::Aabb ResolveVisibleOnScreenNotifier3DWorldAabb(
    const VisibleOnScreenNotifier3DComponent& value,
    const WorldTransformComponent& worldTransform) noexcept {
    const VisibleOnScreenNotifier3DComponent safe =
        SanitizeVisibleOnScreenNotifier3DComponent(value);
    const Math::Aabb localBox =
        Math::Aabb::FromCenterExtents(safe.aabbCenterOffset, safe.aabbHalfExtents);
    if (!IsWorldTransformComponentValid(worldTransform)) {
        return localBox;
    }
    return localBox.Transform(BuildWorldTransformMatrix(worldTransform));
}

void StepVisibleOnScreenNotifier3DState(VisibleOnScreenNotifier3DComponent& value,
                                           const bool currentlyVisible) noexcept {
    const bool wasOnScreen = value.isOnScreen;
    value.isOnScreen = currentlyVisible;
    value.enteredScreenThisFrame = !wasOnScreen && currentlyVisible;
    value.exitedScreenThisFrame = wasOnScreen && !currentlyVisible;
}

bool IsVisibleOnScreenNotifier3DObjectDefinitionValid(
    const VisibleOnScreenNotifier3DObjectDefinition& value) noexcept {
    return IsVisibleOnScreenNotifier3DComponentValid(value.notifier);
}

void ApplyVisibleOnScreenNotifier3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const VisibleOnScreenNotifier3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              VisibleOnScreenNotifier3DObjectDefinition::defaultName);
    entityManager.AddComponent<VisibleOnScreenNotifier3DComponent>(
        entity, SanitizeVisibleOnScreenNotifier3DComponent(value.notifier));
}

std::size_t UpdateVisibleOnScreenNotifier3DObjects(
    IEntityManager& entityManager, const Math::Frustum* const activeCameraFrustum) {
    std::size_t visibleCount = 0U;
    entityManager.ForEach<VisibleOnScreenNotifier3DComponent>(
        [&](const Entity entity, VisibleOnScreenNotifier3DComponent& notifier) {
            if (!notifier.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsVisibleOnScreenNotifier3DComponentValid(notifier) ||
                activeCameraFrustum == nullptr ||
                !entityManager.HasComponent<WorldTransformComponent>(entity)) {
                StepVisibleOnScreenNotifier3DState(notifier, false);
                return;
            }
            const WorldTransformComponent& world =
                entityManager.GetComponent<WorldTransformComponent>(entity);
            if (!IsWorldTransformComponentValid(world)) {
                StepVisibleOnScreenNotifier3DState(notifier, false);
                return;
            }
            const Math::Aabb worldAabb =
                ResolveVisibleOnScreenNotifier3DWorldAabb(notifier, world);
            const bool visible = activeCameraFrustum->Intersects(worldAabb);
            StepVisibleOnScreenNotifier3DState(notifier, visible);
            if (visible) {
                ++visibleCount;
            }
        });
    return visibleCount;
}

} // namespace UVE::Scene
