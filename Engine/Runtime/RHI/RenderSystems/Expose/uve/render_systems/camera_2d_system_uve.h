// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/render_systems/i_camera_2d_system_uve.h"

namespace UVE::Render {

/// Depth range of the 2D view volume in world units: sprites live near z 0 (parallax layers
/// drift a little either way), so a ±100 box holds every sane 2D scene with room to spare.
/// A sprite outside it clips, exactly like a mesh past the 3D far plane.
inline constexpr float kCamera2DViewNearUVE = -100.0F;
inline constexpr float kCamera2DViewFarUVE = 100.0F;

/// Camera2DSystemUVE is the concrete, engine-standard implementation of ICamera2DSystemUVE.
/// Deliberately stateless (no members, no PIMPL) — matches SceneGraphUVE's precedent for
/// utility services that always take the IEntityManagerUVE& they operate on as an explicit
/// parameter rather than owning a reference to one.
class Camera2DSystemUVE final : public ICamera2DSystemUVE {
public:
    [[nodiscard]] Math::Matrix4x4UVE ComputeViewMatrixUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity) const override;
    [[nodiscard]] Math::Matrix4x4UVE ComputeProjectionMatrixUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
        const Math::Vector2UVE& viewportSize) const override;
    [[nodiscard]] Math::Matrix4x4UVE ComputeViewProjectionUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
        const Math::Vector2UVE& viewportSize) const override;
};

} // namespace UVE::Render
