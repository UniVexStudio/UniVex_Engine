// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Render {

/// ICamera2DSystemUVE is the 2D twin of ICameraSystemUVE: the view/projection math for a
/// Camera2D entity, with no renderer state of its own. Every method reads `cameraEntity`'s
/// `Scene::WorldTransformComponentUVE` (position/rotation) and `Scene::Camera2DComponentUVE`
/// (zoom/offset) — the entity must have both, exactly the same contract
/// `IEntityManagerUVE::GetComponentUVE<T>()` already enforces (asserts alive + has the
/// component). There is no graceful-missing-component fallback: entities reach this system
/// only with a scene-graph world transform alongside their authored camera, so a camera
/// entity missing its world transform shouldn't occur in practice.
/// Thread-safety: implementations should be stateless (holding no members) and thread-safe,
/// matching `SceneGraphUVE`'s contract — every method only reads the `IEntityManagerUVE` passed
/// in.
class ICamera2DSystemUVE {
public:
    virtual ~ICamera2DSystemUVE() = default;

    /// The world-to-view transform for `cameraEntity`, built directly from its world position
    /// and rotation (see `Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE`) — a rolled 2D
    /// camera shows a rolled world, exactly like its 3D twin.
    [[nodiscard]] virtual Math::Matrix4x4UVE ComputeViewMatrixUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity) const = 0;

    /// The view-to-clip projection for `cameraEntity`: an orthographic box `viewportSize` /
    /// `zoom` world units across, centered on the camera's `offset` in view space — from its
    /// `Camera2DComponentUVE` and the caller-supplied `viewportSize` (the pixel size of the
    /// target being rendered into, not itself part of any component, since the same camera
    /// entity may render into differently-shaped targets, e.g. the main editor viewport vs.
    /// the entity editor's).
    [[nodiscard]] virtual Math::Matrix4x4UVE ComputeProjectionMatrixUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
        const Math::Vector2UVE& viewportSize) const = 0;

    /// Convenience combining `ComputeProjectionMatrixUVE(...) * ComputeViewMatrixUVE(...)` —
    /// the single matrix most callers (e.g. culling, `Renderer2DUVE`) actually need.
    [[nodiscard]] virtual Math::Matrix4x4UVE ComputeViewProjectionUVE(
        const Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
        const Math::Vector2UVE& viewportSize) const = 0;
};

} // namespace UVE::Render
