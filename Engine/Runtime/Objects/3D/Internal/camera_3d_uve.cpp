// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/camera_3d_uve.h"

#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsCamera3DObjectDefinitionValidUVE(const Camera3DObjectDefinitionUVE& value) noexcept {
    return IsCameraComponentValidUVE(value.camera);
}

bool IsDocumentCameraEntityUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    return entityManager.IsAliveUVE(entity) &&
           entityManager.HasComponentUVE<CameraComponentUVE>(entity) &&
           entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) &&
           !entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity);
}

void MakeCameraCurrentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!IsDocumentCameraEntityUVE(entityManager, entity)) {
        return;
    }
    entityManager.ForEachUVE<CameraComponentUVE>(
        [&entityManager, entity](const EntityUVE other, CameraComponentUVE& camera) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(other)) {
                camera.current = false;
                return;
            }
            camera.current = other == entity;
        });
}

std::optional<EntityUVE> FindCurrentCameraEntityUVE(IEntityManagerUVE& entityManager) {
    std::optional<EntityUVE> current;
    std::optional<EntityUVE> first;
    entityManager.ForEachUVE<WorldTransformComponentUVE, CameraComponentUVE>(
        [&entityManager, &current, &first](const EntityUVE entity, const WorldTransformComponentUVE&,
                                           const CameraComponentUVE& camera) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity)) {
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

void ApplyCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Camera3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Camera3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CameraComponentUVE>(entity, value.camera);
    bool otherCurrent = false;
    entityManager.ForEachUVE<CameraComponentUVE>(
        [&entityManager, entity, &otherCurrent](const EntityUVE other,
                                                const CameraComponentUVE& camera) {
            if (other != entity && camera.current &&
                IsDocumentCameraEntityUVE(entityManager, other)) {
                otherCurrent = true;
            }
        });
    if (value.camera.current || !otherCurrent) {
        MakeCameraCurrentUVE(entityManager, entity);
    }
}

bool Camera3DUVE::IsDocumentCameraEntityUVE(const IEntityManagerUVE& entityManager,
                                            const EntityUVE entity) {
    return UVE::Scene::IsDocumentCameraEntityUVE(entityManager, entity);
}

std::optional<EntityUVE> Camera3DUVE::FindCurrentCameraEntityUVE(
    IEntityManagerUVE& entityManager) {
    return UVE::Scene::FindCurrentCameraEntityUVE(entityManager);
}

void Camera3DUVE::MakeCameraCurrentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    UVE::Scene::MakeCameraCurrentUVE(entityManager, entity);
}

bool Camera3DUVE::IsDepthWithinClipPlanesUVE(const CameraComponentUVE& camera,
                                             const float viewDepth) noexcept {
    if (!IsCameraComponentValidUVE(camera) || !std::isfinite(viewDepth)) {
        return false;
    }
    return viewDepth >= camera.nearPlane && viewDepth <= camera.farPlane;
}

Math::Matrix4x4UVE Camera3DUVE::BuildViewMatrixUVE(
    const WorldTransformComponentUVE& worldTransform) noexcept {
    if (!IsWorldTransformComponentValidUVE(worldTransform)) {
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    return Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(worldTransform.worldPosition,
                                                              worldTransform.worldRotation);
}

Math::Matrix4x4UVE Camera3DUVE::BuildProjectionMatrixUVE(const CameraComponentUVE& camera,
                                                         const float aspectRatio) noexcept {
    const float safeAspect =
        (std::isfinite(aspectRatio) && aspectRatio > 1e-4F) ? aspectRatio : 1.0F;
    if (!IsCameraComponentValidUVE(camera)) {
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    if (camera.projection == CameraProjectionModeUVE::Orthographic) {
        const float halfHeight = camera.orthographicSize;
        const float halfWidth = halfHeight * safeAspect;
        return Math::Matrix4x4UVE::OrthographicUVE(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                                   camera.nearPlane, camera.farPlane);
    }
    const float fovDegrees = VerticalFieldOfViewDegreesUVE(camera, safeAspect);
    const float fovRadians = Math::DegToRadUVE(fovDegrees);
    return Math::Matrix4x4UVE::PerspectiveUVE(fovRadians, safeAspect, camera.nearPlane,
                                              camera.farPlane);
}

Math::Matrix4x4UVE Camera3DUVE::BuildViewProjectionMatrixUVE(
    const CameraComponentUVE& camera, const WorldTransformComponentUVE& worldTransform,
    const float aspectRatio) noexcept {
    return BuildProjectionMatrixUVE(camera, aspectRatio) * BuildViewMatrixUVE(worldTransform);
}

} // namespace UVE::Scene
