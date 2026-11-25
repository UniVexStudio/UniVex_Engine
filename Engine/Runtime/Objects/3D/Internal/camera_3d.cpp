// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_3d.h"

#include <cmath>

#include "editor_internal_entity_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "scalar.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsCamera3DObjectDefinitionValid(const Camera3DObjectDefinition& value) noexcept {
    return IsCameraComponentValid(value.camera);
}

bool IsDocumentCameraEntity(const IEntityManager& entityManager, const Entity entity) {
    return entityManager.IsAlive(entity) &&
           entityManager.HasComponent<CameraComponent>(entity) &&
           entityManager.HasComponent<WorldTransformComponent>(entity) &&
           !entityManager.HasComponent<EditorInternalEntityComponent>(entity);
}

void MakeCameraCurrent(IEntityManager& entityManager, const Entity entity) {
    if (!IsDocumentCameraEntity(entityManager, entity)) {
        return;
    }
    entityManager.ForEach<CameraComponent>(
        [&entityManager, entity](const Entity other, CameraComponent& camera) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(other)) {
                camera.current = false;
                return;
            }
            camera.current = other == entity;
        });
}

std::optional<Entity> FindCurrentCameraEntity(IEntityManager& entityManager) {
    std::optional<Entity> current;
    std::optional<Entity> first;
    entityManager.ForEach<WorldTransformComponent, CameraComponent>(
        [&entityManager, &current, &first](const Entity entity, const WorldTransformComponent&,
                                           const CameraComponent& camera) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity)) {
                return;
            }
            if (!first.has_value()) {
                first = entity;
            }
            if (!current.has_value() && camera.current) {
                current = entity;
            }
        });
    return current.has_value() ? current : first;
}

void ApplyCamera3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Camera3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, Camera3DObjectDefinition::defaultName);
    entityManager.AddComponent<CameraComponent>(entity, value.camera);
    bool otherCurrent = false;
    entityManager.ForEach<CameraComponent>(
        [&entityManager, entity, &otherCurrent](const Entity other,
                                                const CameraComponent& camera) {
            if (other != entity && camera.current &&
                IsDocumentCameraEntity(entityManager, other)) {
                otherCurrent = true;
            }
        });
    if (value.camera.current || !otherCurrent) {
        MakeCameraCurrent(entityManager, entity);
    }
}

bool Camera3D::IsDocumentCameraEntity(const IEntityManager& entityManager,
                                            const Entity entity) {
    return UVE::Scene::IsDocumentCameraEntity(entityManager, entity);
}

std::optional<Entity> Camera3D::FindCurrentCameraEntity(
    IEntityManager& entityManager) {
    return UVE::Scene::FindCurrentCameraEntity(entityManager);
}

void Camera3D::MakeCameraCurrent(IEntityManager& entityManager, const Entity entity) {
    UVE::Scene::MakeCameraCurrent(entityManager, entity);
}

bool Camera3D::IsDepthWithinClipPlanes(const CameraComponent& camera,
                                             const float viewDepth) noexcept {
    if (!IsCameraComponentValid(camera) || !std::isfinite(viewDepth)) {
        return false;
    }
    return viewDepth >= camera.nearPlane && viewDepth <= camera.farPlane;
}

Math::Matrix4x4 Camera3D::BuildViewMatrix(
    const WorldTransformComponent& worldTransform) noexcept {
    if (!IsWorldTransformComponentValid(worldTransform)) {
        return Math::Matrix4x4::Identity();
    }
    return Math::Matrix4x4::ViewFromPositionAndRotation(worldTransform.worldPosition,
                                                              worldTransform.worldRotation);
}

Math::Matrix4x4 Camera3D::BuildProjectionMatrix(const CameraComponent& camera,
                                                         const float aspectRatio) noexcept {
    const float safeAspect =
        (std::isfinite(aspectRatio) && aspectRatio > 1e-4F) ? aspectRatio : 1.0F;
    if (!IsCameraComponentValid(camera)) {
        return Math::Matrix4x4::Identity();
    }
    if (camera.projection == CameraProjectionMode::Orthographic) {
        const float halfHeight = camera.orthographicSize;
        const float halfWidth = halfHeight * safeAspect;
        return Math::Matrix4x4::Orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                                   camera.nearPlane, camera.farPlane);
    }
    const float fovDegrees = VerticalFieldOfViewDegrees(camera, safeAspect);
    const float fovRadians = Math::DegToRad(fovDegrees);
    return Math::Matrix4x4::Perspective(fovRadians, safeAspect, camera.nearPlane,
                                              camera.farPlane);
}

Math::Matrix4x4 Camera3D::BuildViewProjectionMatrix(
    const CameraComponent& camera, const WorldTransformComponent& worldTransform,
    const float aspectRatio) noexcept {
    return BuildProjectionMatrix(camera, aspectRatio) * BuildViewMatrix(worldTransform);
}

} // namespace UVE::Scene
