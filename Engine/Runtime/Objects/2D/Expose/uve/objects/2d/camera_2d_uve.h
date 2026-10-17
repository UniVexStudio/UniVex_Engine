// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Camera2D scene object: the component set and defaults a
/// freshly created Camera2D entity attaches - a 1:1 unoffset view that follows nothing
/// until its `current` flag is set.
struct Camera2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Camera2D";
    /// Default document-entity name for a freshly created object of this kind - matches
    /// the registry's own display name exactly.
    static constexpr std::string_view defaultName = "Camera2D";

    Camera2DComponentUVE camera{};

    [[nodiscard]] bool operator==(const Camera2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsCamera2DObjectDefinitionValidUVE(
    const Camera2DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults.
/// The entity must be alive and must not already have any of the attached component types.
void ApplyCamera2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Camera2DObjectDefinitionUVE& value);

/// Orthographic frustum bounds resolved for a `Camera2D` in view space.
struct Camera2DOrthoBoundsUVE final {
    float left = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    float top = 0.0F;

    [[nodiscard]] bool operator==(const Camera2DOrthoBoundsUVE&) const = default;
};

/// Node policy for a `Camera2D` scene object.
///
/// Mirrors `Camera3D`'s active-camera ownership and view-volume math for 2D orthographic cameras,
/// shared by `Camera2DSystemUVE` and 2D scene queries.
class Camera2DUVE final {
public:
    Camera2DUVE() = delete;

    /// True when `entity` is alive, carries `Camera2DComponentUVE`, and is not an editor-internal
    /// entity.
    [[nodiscard]] static bool IsDocumentCamera2DEntityUVE(
        const IEntityManagerUVE& entityManager, EntityUVE entity) noexcept;

    /// Finds the current active document `Camera2D` entity, or `kInvalidEntityUVE` if none.
    [[nodiscard]] static EntityUVE FindCurrentCamera2DEntityUVE(IEntityManagerUVE& entityManager);

    /// Marks `cameraEntity` as the sole `current = true` document `Camera2D` in `entityManager`.
    static void MakeCamera2DCurrentUVE(IEntityManagerUVE& entityManager, EntityUVE cameraEntity);

    /// Resolves half-width and half-height `{viewportSize.x / (2 * zoom.x), viewportSize.y / (2 * zoom.y)}`.
    /// Returns false when `camera` or `viewportSize` is invalid.
    [[nodiscard]] static bool TryResolveViewHalfExtentsUVE(
        const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSize,
        Math::Vector2UVE& outHalfExtents) noexcept;

    /// Resolves the orthographic `[left, right, bottom, top]` bounds around `camera.offset` for
    /// `viewportSize`, consumed by `Camera2DSystemUVE::ComputeProjectionMatrixUVE`.
    [[nodiscard]] static bool TryResolveOrthoBoundsUVE(
        const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSize,
        Camera2DOrthoBoundsUVE& outBounds) noexcept;

    /// Resolves the world-space visible rectangle when the camera sits at `cameraWorldPosition`
    /// with no rotation. Returns an empty rect if invalid.
    [[nodiscard]] static Math::RectUVE ResolveWorldViewRectUVE(
        const Camera2DComponentUVE& camera, const Math::Vector2UVE& cameraWorldPosition,
        const Math::Vector2UVE& viewportSize) noexcept;
};

} // namespace UVE::Scene
