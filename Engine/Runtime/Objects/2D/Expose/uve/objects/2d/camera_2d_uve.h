// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Camera2D scene object: the component set and defaults a
/// freshly created Camera2D entity attaches - a 1:1 unoffset view that follows nothing
/// until its `current` flag is set. Per Engine/Runtime/Scene/README.md's "one truth per
/// concept" rule this holds the *recipe*, not a second copy of component storage: the
/// camera record itself still lives only in Camera2DComponentUVE. The entity's transform
/// is attached by the creation shell (the Object3D baseline helper lives in the 3D object
/// lib, so - like Sprite2D and the Canvas kinds - this apply stays component-only rather
/// than reaching across).
struct Camera2DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind - matches
    /// the registry's own display name exactly.
    static constexpr std::string_view defaultName = "Camera2D";

    Camera2DComponentUVE camera{};
};

[[nodiscard]] bool IsCamera2DObjectDefinitionValidUVE(
    const Camera2DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults.
/// The entity must be alive and must not already have any of the attached component types.
void ApplyCamera2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Camera2DObjectDefinitionUVE& value);

} // namespace UVE::Scene
