// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the SphereMesh3D scene node: the component set and defaults a
/// freshly created SphereMesh3D entity attaches — a UV-sphere primitive mesh plus a conservative
/// box collider (0.5 half-extents per axis, matching the unit-diameter sphere's bounding box).
/// This recipe (including the exact editor-blue base color) used to be hardcoded in EditorUVE's
/// creation switch behind the legacy EditorEntityKindUVE::UVSphere case; it now has the same
/// per-file home every other node kind has. Per Engine/Runtime/Scene/README.md's "one truth per
/// concept" rule this holds the *recipe*, not a second copy of component storage: the mesh record
/// still lives only in PrimitiveMeshComponentUVE (geometry itself is renderer-owned).
struct SphereMesh3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. "UV Sphere"
    /// preserves the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "UV Sphere";

    /// UV-sphere primitive authored defaults (editor-blue base color, matching the legacy recipe).
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{0.22F, 0.55F, 0.88F}};
    /// Conservative box collision authored defaults around the unit-diameter sphere.
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsSphereMesh3DNodeDefinitionValidUVE(const SphereMesh3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplySphereMesh3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const SphereMesh3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
