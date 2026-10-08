// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "uve/component/camera_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Camera3D scene object: the component set and defaults a freshly
/// created Camera3D entity attaches. This recipe used to be hardcoded in EditorUVE's creation
/// switch behind the legacy EditorEntityKindUVE::Camera case; it now has the same per-file home
/// every other object kind has, so the object's creation behavior is customized in exactly one
/// discoverable place. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: camera state itself still lives
/// only in CameraComponentUVE (and is consumed by Render/CameraSystemUVE).
struct Camera3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. "Camera" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Camera";

    /// Camera authored defaults; the entity's transform is attached by the creation shell.
    CameraComponentUVE camera{};
};

[[nodiscard]] bool IsCamera3DObjectDefinitionValidUVE(const Camera3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Camera3DObjectDefinitionUVE& value);

[[nodiscard]] bool IsDocumentCameraEntityUVE(const IEntityManagerUVE& entityManager, EntityUVE entity);

[[nodiscard]] std::optional<EntityUVE> FindCurrentCameraEntityUVE(IEntityManagerUVE& entityManager);

void MakeCameraCurrentUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

} // namespace UVE::Scene
