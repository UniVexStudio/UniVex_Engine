// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "camera_system.h"

#include <cmath>
#include <limits>
#include <numbers>

#include "camera_component.h"
#include "world_transform_component.h"
#include "engine_assert.h"
#include "logging_macros.h"
#include "vector3.h"

namespace UVE::Render {
namespace {

[[nodiscard]] bool TryNormalizeWorldTransformRotation(const Scene::WorldTransformComponent& worldTransform,
                                                          Math::Quaternion& outRotation) noexcept {
    return Math::IsFinite(worldTransform.worldPosition) &&
           Math::TryNormalize(worldTransform.worldRotation, outRotation);
}

} // namespace

Math::Matrix4x4 CameraSystem::ComputeViewMatrix(const Scene::IEntityManager& entityManager,
                                                           Scene::Entity cameraEntity) const {
    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(cameraEntity);
    Math::Quaternion normalizedRotation;
    const bool validTransform = TryNormalizeWorldTransformRotation(worldTransform, normalizedRotation);
    UVE_ASSERT(validTransform);
    if (!validTransform) {
        UVE_ERROR("CameraSystem: ComputeViewMatrix received an invalid world transform");
        return Math::Matrix4x4::Identity();
    }
    return Math::Matrix4x4::ViewFromPositionAndRotation(worldTransform.worldPosition, normalizedRotation);
}

Math::Matrix4x4 CameraSystem::ComputeProjectionMatrix(const Scene::IEntityManager& entityManager,
                                                                 Scene::Entity cameraEntity,
                                                                 float aspectRatio) const {
    const Scene::CameraComponent& camera = entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    const bool cameraValid = Scene::IsCameraComponentValid(camera);
    UVE_ASSERT(cameraValid);
    if (!cameraValid) {
        UVE_ERROR("CameraSystem: ComputeProjectionMatrix received invalid camera parameters");
        return Math::Matrix4x4::Identity();
    }
    const bool aspectRatioValid = std::isfinite(aspectRatio) && aspectRatio > 0.0F;
    UVE_ASSERT(aspectRatioValid);
    if (!aspectRatioValid) {
        UVE_ERROR("CameraSystem: ComputeProjectionMatrix received an invalid aspect ratio");
        return Math::Matrix4x4::Identity();
    }
    if (camera.projection == Scene::CameraProjectionMode::Orthographic) {
        const float halfHeight = camera.orthographicSize;
        const float halfWidth = halfHeight * aspectRatio;
        return Math::Matrix4x4::Orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, camera.nearPlane,
                                                   camera.farPlane);
    }
    const float fovYRadians =
        Scene::VerticalFieldOfViewDegrees(camera, aspectRatio) * (std::numbers::pi_v<float> / 180.0F);
    return Math::Matrix4x4::Perspective(fovYRadians, aspectRatio, camera.nearPlane, camera.farPlane);
}

Math::Matrix4x4 CameraSystem::ComputeViewProjection(const Scene::IEntityManager& entityManager,
                                                               Scene::Entity cameraEntity,
                                                               float aspectRatio) const {
    return ComputeProjectionMatrix(entityManager, cameraEntity, aspectRatio) *
           ComputeViewMatrix(entityManager, cameraEntity);
}

Math::Frustum CameraSystem::ExtractFrustum(const Math::Matrix4x4& viewProjection) const {
    return Math::Frustum::FromViewProjection(viewProjection);
}

CameraFrustumCorners CameraSystem::ComputeFrustumCorners(const Scene::IEntityManager& entityManager,
                                                                    Scene::Entity cameraEntity,
                                                                    float aspectRatio) const {
    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(cameraEntity);
    Math::Quaternion normalizedRotation;
    const bool transformValid = TryNormalizeWorldTransformRotation(worldTransform, normalizedRotation);
    UVE_ASSERT(transformValid);
    if (!transformValid) {
        UVE_ERROR("CameraSystem: ComputeFrustumCorners received an invalid world transform");
        return CameraFrustumCorners{};
    }
    const Scene::CameraComponent& camera = entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    const bool cameraValid = Scene::IsCameraComponentValid(camera);
    UVE_ASSERT(cameraValid);
    if (!cameraValid) {
        UVE_ERROR("CameraSystem: ComputeFrustumCorners received invalid camera parameters");
        return CameraFrustumCorners{};
    }
    const bool aspectRatioValid = std::isfinite(aspectRatio) && aspectRatio > 0.0F;
    UVE_ASSERT(aspectRatioValid);
    if (!aspectRatioValid) {
        UVE_ERROR("CameraSystem: ComputeFrustumCorners received an invalid aspect ratio");
        return CameraFrustumCorners{};
    }
    float nearHalfHeight = camera.orthographicSize;
    float nearHalfWidth = nearHalfHeight * aspectRatio;
    float farHalfHeight = nearHalfHeight;
    float farHalfWidth = nearHalfWidth;
    if (camera.projection != Scene::CameraProjectionMode::Orthographic) {
        const float tangent = std::tan(Scene::VerticalFieldOfViewDegrees(camera, aspectRatio) *
                                       (std::numbers::pi_v<float> / 360.0F));
        nearHalfHeight = camera.nearPlane * tangent;
        nearHalfWidth = nearHalfHeight * aspectRatio;
        farHalfHeight = camera.farPlane * tangent;
        farHalfWidth = farHalfHeight * aspectRatio;
    }

    const Math::Vector3 forward = Math::RotateVector(normalizedRotation, {0.0F, 0.0F, -1.0F});
    const Math::Vector3 right = Math::RotateVector(normalizedRotation, {1.0F, 0.0F, 0.0F});
    const Math::Vector3 up = Math::RotateVector(normalizedRotation, {0.0F, 1.0F, 0.0F});
    const Math::Vector3 nearCenter = worldTransform.worldPosition + forward * camera.nearPlane;
    const Math::Vector3 farCenter = worldTransform.worldPosition + forward * camera.farPlane;

    const CameraFrustumCorners floatCorners{
        nearCenter - right * nearHalfWidth - up * nearHalfHeight,
        nearCenter + right * nearHalfWidth - up * nearHalfHeight,
        nearCenter - right * nearHalfWidth + up * nearHalfHeight,
        nearCenter + right * nearHalfWidth + up * nearHalfHeight,
        farCenter - right * farHalfWidth - up * farHalfHeight,
        farCenter + right * farHalfWidth - up * farHalfHeight,
        farCenter - right * farHalfWidth + up * farHalfHeight,
        farCenter + right * farHalfWidth + up * farHalfHeight,
    };
    bool floatCornersFinite = true;
    for (const Math::Vector3 corner : floatCorners) {
        floatCornersFinite = floatCornersFinite && std::isfinite(corner.x) && std::isfinite(corner.y) &&
                              std::isfinite(corner.z);
    }
    if (floatCornersFinite) {
        return floatCorners;
    }

    const double nearPlaneWide = static_cast<double>(camera.nearPlane);
    const double farPlaneWide = static_cast<double>(camera.farPlane);
    const double aspectWide = static_cast<double>(aspectRatio);
    double nearHalfHeightWide = static_cast<double>(camera.orthographicSize);
    double nearHalfWidthWide = nearHalfHeightWide * aspectWide;
    double farHalfHeightWide = nearHalfHeightWide;
    double farHalfWidthWide = nearHalfWidthWide;
    if (camera.projection != Scene::CameraProjectionMode::Orthographic) {
        const double tangentWide =
            std::tan(static_cast<double>(Scene::VerticalFieldOfViewDegrees(camera, aspectRatio)) *
                     (static_cast<double>(std::numbers::pi_v<float>) / 360.0));
        nearHalfHeightWide = nearPlaneWide * tangentWide;
        nearHalfWidthWide = nearHalfHeightWide * aspectWide;
        farHalfHeightWide = farPlaneWide * tangentWide;
        farHalfWidthWide = farHalfHeightWide * aspectWide;
    }
    const double positionX = static_cast<double>(worldTransform.worldPosition.x);
    const double positionY = static_cast<double>(worldTransform.worldPosition.y);
    const double positionZ = static_cast<double>(worldTransform.worldPosition.z);
    const double forwardX = static_cast<double>(forward.x);
    const double forwardY = static_cast<double>(forward.y);
    const double forwardZ = static_cast<double>(forward.z);
    const double rightX = static_cast<double>(right.x);
    const double rightY = static_cast<double>(right.y);
    const double rightZ = static_cast<double>(right.z);
    const double upX = static_cast<double>(up.x);
    const double upY = static_cast<double>(up.y);
    const double upZ = static_cast<double>(up.z);
    const double nearCenterX = positionX + forwardX * nearPlaneWide;
    const double nearCenterY = positionY + forwardY * nearPlaneWide;
    const double nearCenterZ = positionZ + forwardZ * nearPlaneWide;
    const double farCenterX = positionX + forwardX * farPlaneWide;
    const double farCenterY = positionY + forwardY * farPlaneWide;
    const double farCenterZ = positionZ + forwardZ * farPlaneWide;
    const double nearRightX = rightX * nearHalfWidthWide;
    const double nearRightY = rightY * nearHalfWidthWide;
    const double nearRightZ = rightZ * nearHalfWidthWide;
    const double nearUpX = upX * nearHalfHeightWide;
    const double nearUpY = upY * nearHalfHeightWide;
    const double nearUpZ = upZ * nearHalfHeightWide;
    const double farRightX = rightX * farHalfWidthWide;
    const double farRightY = rightY * farHalfWidthWide;
    const double farRightZ = rightZ * farHalfWidthWide;
    const double farUpX = upX * farHalfHeightWide;
    const double farUpY = upY * farHalfHeightWide;
    const double farUpZ = upZ * farHalfHeightWide;
    const auto NarrowFinite = [](const double value) noexcept {
        if (std::isnan(value)) {
            return 0.0F;
        }
        const double maximum = static_cast<double>(std::numeric_limits<float>::max());
        if (value >= maximum) {
            return std::numeric_limits<float>::max();
        }
        if (value <= -maximum) {
            return -std::numeric_limits<float>::max();
        }
        return static_cast<float>(value);
    };
    const auto MakeCorner = [NarrowFinite](const double centerX, const double centerY, const double centerZ,
                                                  const double rightComponentX, const double rightComponentY,
                                                  const double rightComponentZ, const double upComponentX,
                                                  const double upComponentY, const double upComponentZ,
                                                  const double rightSign, const double upSign) {
        return Math::Vector3{
            NarrowFinite(centerX + rightSign * rightComponentX + upSign * upComponentX),
            NarrowFinite(centerY + rightSign * rightComponentY + upSign * upComponentY),
            NarrowFinite(centerZ + rightSign * rightComponentZ + upSign * upComponentZ),
        };
    };

    return {
        MakeCorner(nearCenterX, nearCenterY, nearCenterZ, nearRightX, nearRightY, nearRightZ,
                      nearUpX, nearUpY, nearUpZ, -1.0, -1.0),
        MakeCorner(nearCenterX, nearCenterY, nearCenterZ, nearRightX, nearRightY, nearRightZ,
                      nearUpX, nearUpY, nearUpZ, 1.0, -1.0),
        MakeCorner(nearCenterX, nearCenterY, nearCenterZ, nearRightX, nearRightY, nearRightZ,
                      nearUpX, nearUpY, nearUpZ, -1.0, 1.0),
        MakeCorner(nearCenterX, nearCenterY, nearCenterZ, nearRightX, nearRightY, nearRightZ,
                      nearUpX, nearUpY, nearUpZ, 1.0, 1.0),
        MakeCorner(farCenterX, farCenterY, farCenterZ, farRightX, farRightY, farRightZ,
                      farUpX, farUpY, farUpZ, -1.0, -1.0),
        MakeCorner(farCenterX, farCenterY, farCenterZ, farRightX, farRightY, farRightZ,
                      farUpX, farUpY, farUpZ, 1.0, -1.0),
        MakeCorner(farCenterX, farCenterY, farCenterZ, farRightX, farRightY, farRightZ,
                      farUpX, farUpY, farUpZ, -1.0, 1.0),
        MakeCorner(farCenterX, farCenterY, farCenterZ, farRightX, farRightY, farRightZ,
                      farUpX, farUpY, farUpZ, 1.0, 1.0),
    };
}

Math::Vector3 CameraSystem::GetWorldPosition(const Scene::IEntityManager& entityManager,
                                                         Scene::Entity cameraEntity) const {
    const Scene::WorldTransformComponent& worldTransform =
        entityManager.GetComponent<Scene::WorldTransformComponent>(cameraEntity);
    Math::Quaternion normalizedRotation;
    const bool validTransform = TryNormalizeWorldTransformRotation(worldTransform, normalizedRotation);
    UVE_ASSERT(validTransform);
    if (!validTransform) {
        UVE_ERROR("CameraSystem: GetWorldPosition received an invalid world transform");
        return Math::Vector3{};
    }
    return worldTransform.worldPosition;
}

} // namespace UVE::Render
