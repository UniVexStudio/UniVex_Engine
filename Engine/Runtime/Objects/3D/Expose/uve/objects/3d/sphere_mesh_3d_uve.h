// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the SphereMesh3D scene object: the component set and defaults a
/// freshly created SphereMesh3D entity attaches — a UV-sphere primitive mesh plus a conservative
/// box collider (0.5 half-extents per axis, matching the unit-diameter sphere's bounding box).
/// This recipe (including its neutral white base color) used to be hardcoded in EditorUVE's
/// creation switch behind the legacy EditorEntityKindUVE::UVSphere case; it now has the same
/// per-file home every other object kind has. Per Engine/Runtime/Scene/README.md's "one truth per
/// concept" rule this holds the *recipe*, not a second copy of component storage: the mesh record
/// still lives only in PrimitiveMeshComponentUVE (geometry itself is renderer-owned).
struct SphereMesh3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. "UV Sphere"
    /// preserves the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "UV Sphere";

    /// UV-sphere primitive authored defaults (neutral white; materials/colors can be authored later).
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    /// Conservative box collision authored defaults around the unit-diameter sphere.
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
};

[[nodiscard]] bool IsSphereMesh3DObjectDefinitionValidUVE(const SphereMesh3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplySphereMesh3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const SphereMesh3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
