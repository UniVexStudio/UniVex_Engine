// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/camera_2d_uve.h"

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsCamera2DObjectDefinitionValidUVE(const Camera2DObjectDefinitionUVE& value) noexcept {
    return IsCamera2DComponentValidUVE(value.camera);
}

void ApplyCamera2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Camera2DObjectDefinitionUVE& value) {
    const Camera2DComponentUVE sanitized = SanitizeCamera2DComponentUVE(value.camera);
    entityManager.AddComponentUVE<Camera2DComponentUVE>(entity, sanitized);
    if (sanitized.current) {
        Camera2DUVE::MakeCamera2DCurrentUVE(entityManager, entity);
    }
}

bool Camera2DUVE::IsDocumentCamera2DEntityUVE(
    const IEntityManagerUVE& entityManager, const EntityUVE entity) noexcept {
    return entityManager.IsAliveUVE(entity) &&
           !entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) &&
           entityManager.HasComponentUVE<Camera2DComponentUVE>(entity);
}

EntityUVE Camera2DUVE::FindCurrentCamera2DEntityUVE(IEntityManagerUVE& entityManager) {
    EntityUVE currentCamera = kInvalidEntityUVE;
    entityManager.ForEachUVE<Camera2DComponentUVE>(
        [&](const EntityUVE entity, const Camera2DComponentUVE& camera) {
            if (currentCamera == kInvalidEntityUVE &&
                IsDocumentCamera2DEntityUVE(entityManager, entity) && camera.current) {
                currentCamera = entity;
            }
        });
    return currentCamera;
}

void Camera2DUVE::MakeCamera2DCurrentUVE(
    IEntityManagerUVE& entityManager, const EntityUVE cameraEntity) {
    if (!IsDocumentCamera2DEntityUVE(entityManager, cameraEntity)) {
        return;
    }
    entityManager.ForEachUVE<Camera2DComponentUVE>(
        [&](const EntityUVE entity, Camera2DComponentUVE& camera) {
            if (!IsDocumentCamera2DEntityUVE(entityManager, entity)) {
                return;
            }
            camera.current = (entity == cameraEntity);
        });
}

bool Camera2DUVE::TryResolveViewHalfExtentsUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSize,
    Math::Vector2UVE& outHalfExtents) noexcept {
    if (!IsCamera2DComponentValidUVE(camera) || !Math::IsFiniteUVE(viewportSize) ||
        viewportSize.x <= 0.0F || viewportSize.y <= 0.0F) {
        return false;
    }
    outHalfExtents = Math::Vector2UVE{viewportSize.x / (2.0F * camera.zoom.x),
                                      viewportSize.y / (2.0F * camera.zoom.y)};
    return Math::IsFiniteUVE(outHalfExtents) && outHalfExtents.x > 0.0F && outHalfExtents.y > 0.0F;
}

bool Camera2DUVE::TryResolveOrthoBoundsUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSize,
    Camera2DOrthoBoundsUVE& outBounds) noexcept {
    Math::Vector2UVE halfExtents{};
    if (!TryResolveViewHalfExtentsUVE(camera, viewportSize, halfExtents)) {
        return false;
    }
    outBounds.left = camera.offset.x - halfExtents.x;
    outBounds.right = camera.offset.x + halfExtents.x;
    outBounds.bottom = camera.offset.y - halfExtents.y;
    outBounds.top = camera.offset.y + halfExtents.y;
    return true;
}

Math::RectUVE Camera2DUVE::ResolveWorldViewRectUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& cameraWorldPosition,
    const Math::Vector2UVE& viewportSize) noexcept {
    Camera2DOrthoBoundsUVE bounds{};
    if (!Math::IsFiniteUVE(cameraWorldPosition) ||
        !TryResolveOrthoBoundsUVE(camera, viewportSize, bounds)) {
        return Math::RectUVE{};
    }
    return Math::RectUVE{
        Math::Vector2UVE{cameraWorldPosition.x + bounds.left,
                         cameraWorldPosition.y + bounds.bottom},
        Math::Vector2UVE{bounds.right - bounds.left, bounds.top - bounds.bottom}};
}

} // namespace UVE::Scene
