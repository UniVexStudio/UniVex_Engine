// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/camera_2d_system_uve.h"

#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/objects/2d/camera_2d_uve.h"

namespace UVE::Render {
namespace {

[[nodiscard]] bool TryNormalizeWorldTransformRotationUVE(const Scene::WorldTransformComponentUVE& worldTransform,
                                                          Math::QuaternionUVE& outRotation) noexcept {
    return Math::IsFiniteUVE(worldTransform.worldPosition) &&
           Math::TryNormalizeUVE(worldTransform.worldRotation, outRotation);
}

} // namespace

Math::Matrix4x4UVE Camera2DSystemUVE::ComputeViewMatrixUVE(const Scene::IEntityManagerUVE& entityManager,
                                                             Scene::EntityUVE cameraEntity) const {
    const Scene::WorldTransformComponentUVE& worldTransform =
        entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(cameraEntity);
    Math::QuaternionUVE normalizedRotation;
    const bool validTransform = TryNormalizeWorldTransformRotationUVE(worldTransform, normalizedRotation);
    UVE_ASSERT(validTransform);
    if (!validTransform) {
        UVE_ERROR("Camera2DSystemUVE: ComputeViewMatrixUVE received an invalid world transform");
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    return Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(worldTransform.worldPosition, normalizedRotation);
}

Math::Matrix4x4UVE Camera2DSystemUVE::ComputeProjectionMatrixUVE(const Scene::IEntityManagerUVE& entityManager,
                                                                   Scene::EntityUVE cameraEntity,
                                                                   const Math::Vector2UVE& viewportSize) const {
    const Scene::Camera2DComponentUVE& camera =
        entityManager.GetComponentUVE<Scene::Camera2DComponentUVE>(cameraEntity);
    const bool cameraValid = Scene::IsCamera2DComponentValidUVE(camera);
    UVE_ASSERT(cameraValid);
    if (!cameraValid) {
        UVE_ERROR("Camera2DSystemUVE: ComputeProjectionMatrixUVE received invalid camera parameters");
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    const bool viewportValid =
        Math::IsFiniteUVE(viewportSize) && viewportSize.x > 0.0F && viewportSize.y > 0.0F;
    UVE_ASSERT(viewportValid);
    if (!viewportValid) {
        UVE_ERROR("Camera2DSystemUVE: ComputeProjectionMatrixUVE received an invalid viewport size");
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    Scene::Camera2DOrthoBoundsUVE orthoBounds{};
    if (!Scene::Camera2DUVE::TryResolveOrthoBoundsUVE(camera, viewportSize, orthoBounds)) {
        return Math::Matrix4x4UVE::IdentityUVE();
    }
    return Math::Matrix4x4UVE::OrthographicUVE(orthoBounds.left, orthoBounds.right,
                                               orthoBounds.bottom, orthoBounds.top,
                                               kCamera2DViewNearUVE, kCamera2DViewFarUVE);
}

Math::Matrix4x4UVE Camera2DSystemUVE::ComputeViewProjectionUVE(const Scene::IEntityManagerUVE& entityManager,
                                                                 Scene::EntityUVE cameraEntity,
                                                                 const Math::Vector2UVE& viewportSize) const {
    return ComputeProjectionMatrixUVE(entityManager, cameraEntity, viewportSize) *
           ComputeViewMatrixUVE(entityManager, cameraEntity);
}

} // namespace UVE::Render
