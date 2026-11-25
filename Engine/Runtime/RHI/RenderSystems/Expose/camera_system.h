// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_camera_system.h"

namespace UVE::Render {

/// CameraSystem is the concrete, engine-standard implementation of ICameraSystem.
/// Deliberately stateless (no members, no PIMPL) — matches SceneGraph's precedent for
/// utility services that always take the IEntityManager& they operate on as an explicit
/// parameter rather than owning a reference to one.
class CameraSystem final : public ICameraSystem {
public:
    [[nodiscard]] Math::Matrix4x4 ComputeViewMatrix(const Scene::IEntityManager& entityManager,
                                                            Scene::Entity cameraEntity) const override;
    [[nodiscard]] Math::Matrix4x4 ComputeProjectionMatrix(const Scene::IEntityManager& entityManager,
                                                                  Scene::Entity cameraEntity,
                                                                  float aspectRatio) const override;
    [[nodiscard]] Math::Matrix4x4 ComputeViewProjection(const Scene::IEntityManager& entityManager,
                                                                Scene::Entity cameraEntity,
                                                                float aspectRatio) const override;
    [[nodiscard]] Math::Frustum ExtractFrustum(const Math::Matrix4x4& viewProjection) const override;
    [[nodiscard]] CameraFrustumCorners ComputeFrustumCorners(const Scene::IEntityManager& entityManager,
                                                                     Scene::Entity cameraEntity,
                                                                     float aspectRatio) const override;
    [[nodiscard]] Math::Vector3 GetWorldPosition(const Scene::IEntityManager& entityManager,
                                                          Scene::Entity cameraEntity) const override;
};

} // namespace UVE::Render
