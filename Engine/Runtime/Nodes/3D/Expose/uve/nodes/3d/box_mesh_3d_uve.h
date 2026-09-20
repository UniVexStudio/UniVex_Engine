// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the BoxMesh3D scene node: the component set and defaults a freshly
/// created BoxMesh3D entity attaches — a cube primitive mesh plus a matching box collider. This
/// recipe (including the exact editor-orange base color) used to be hardcoded in EditorUVE's
/// creation switch behind the legacy EditorEntityKindUVE::Cube case; it now has the same per-file
/// home every other node kind has, so the node's look and collision defaults are customized in
/// exactly one discoverable place. Per Engine/Runtime/Scene/README.md's "one truth per concept"
/// rule this holds the *recipe*, not a second copy of component storage: the mesh record still
/// lives only in PrimitiveMeshComponentUVE (geometry itself is renderer-owned).
struct BoxMesh3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. "Cube" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Cube";

    /// Cube primitive authored defaults (editor-orange base color, matching the legacy recipe).
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.73F, 0.48F, 0.21F}};
    /// Unit-cube collision authored defaults (0.5 half-extents per axis).
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsBoxMesh3DNodeDefinitionValidUVE(const BoxMesh3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyBoxMesh3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const BoxMesh3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
