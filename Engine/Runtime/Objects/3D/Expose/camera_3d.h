// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "camera_component.h"
#include "entity.h"
#include "world_transform_component.h"
#include "matrix4x4.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Camera3D scene object: the component set and defaults a freshly
/// created Camera3D entity attaches. This recipe used to be hardcoded in Editor's creation
/// switch behind the legacy EditorEntityKind::Camera case; it now has the same per-file home
/// every other object kind has, so the object's creation behavior is customized in exactly one
/// discoverable place. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: camera state itself still lives
/// only in CameraComponent (and is consumed by Render/CameraSystem).
struct Camera3DObjectDefinition final {
    /// Default document-entity name for a freshly created object of this kind. "Camera" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Camera";

    /// Camera authored defaults; the entity's transform is attached by the creation shell.
    CameraComponent camera{};
};

[[nodiscard]] bool IsCamera3DObjectDefinitionValid(const Camera3DObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaseline - this kind is Object3D plus its recipe.
void ApplyCamera3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Camera3DObjectDefinition& value);

[[nodiscard]] bool IsDocumentCameraEntity(const IEntityManager& entityManager, Entity entity);

[[nodiscard]] std::optional<Entity> FindCurrentCameraEntity(IEntityManager& entityManager);

void MakeCameraCurrent(IEntityManager& entityManager, Entity entity);

class Camera3D final {
public:
    [[nodiscard]] static bool IsDocumentCameraEntity(const IEntityManager& entityManager,
                                                        Entity entity);
    [[nodiscard]] static std::optional<Entity> FindCurrentCameraEntity(
        IEntityManager& entityManager);
    static void MakeCameraCurrent(IEntityManager& entityManager, Entity entity);

    [[nodiscard]] static bool IsDepthWithinClipPlanes(const CameraComponent& camera,
                                                         float viewDepth) noexcept;
    [[nodiscard]] static Math::Matrix4x4 BuildViewMatrix(
        const WorldTransformComponent& worldTransform) noexcept;
    [[nodiscard]] static Math::Matrix4x4 BuildProjectionMatrix(
        const CameraComponent& camera, float aspectRatio = 16.0F / 9.0F) noexcept;
    [[nodiscard]] static Math::Matrix4x4 BuildViewProjectionMatrix(
        const CameraComponent& camera, const WorldTransformComponent& worldTransform,
        float aspectRatio = 16.0F / 9.0F) noexcept;
};

} // namespace UVE::Scene
