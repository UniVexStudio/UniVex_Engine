// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the BoxMesh3D scene object: the component set and defaults a freshly
/// created BoxMesh3D entity attaches — a cube primitive mesh plus a matching box collider. This
/// recipe (including its neutral white base color) used to be hardcoded in EditorUVE's
/// creation switch behind the legacy EditorEntityKindUVE::Cube case; it now has the same per-file
/// home every other object kind has, so the object's look and collision defaults are customized in
/// exactly one discoverable place. Per Engine/Runtime/Scene/README.md's "one truth per concept"
/// rule this holds the *recipe*, not a second copy of component storage: the mesh record still
/// lives only in PrimitiveMeshComponentUVE (geometry itself is renderer-owned).
struct BoxMesh3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. "Cube" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Cube";

    /// Cube primitive authored defaults (neutral white; materials/colors can be authored later).
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    /// Unit-cube collision authored defaults (0.5 half-extents per axis).
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsBoxMesh3DObjectDefinitionValidUVE(const BoxMesh3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyBoxMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const BoxMesh3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
