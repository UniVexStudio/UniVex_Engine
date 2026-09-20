// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/camera_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Camera3D scene node: the component set and defaults a freshly
/// created Camera3D entity attaches. This recipe used to be hardcoded in EditorUVE's creation
/// switch behind the legacy EditorEntityKindUVE::Camera case; it now has the same per-file home
/// every other node kind has, so the node's creation behavior is customized in exactly one
/// discoverable place. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: camera state itself still lives
/// only in CameraComponentUVE (and is consumed by Render/CameraSystemUVE).
struct Camera3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. "Camera" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Camera";

    /// Camera authored defaults; the entity's transform is attached by the creation shell.
    CameraComponentUVE camera{};
};

[[nodiscard]] bool IsCamera3DNodeDefinitionValidUVE(const Camera3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyCamera3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Camera3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
