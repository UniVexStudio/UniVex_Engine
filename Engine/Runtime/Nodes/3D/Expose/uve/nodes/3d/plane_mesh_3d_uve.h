// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the PlaneMesh3D scene node: the component set and defaults a freshly
/// created PlaneMesh3D entity attaches — a ground-plane primitive mesh plus a thin flat collider
/// (0.5 X/Z half-extents, 0.025 Y — a walkable floor slab rather than a full box). This recipe
/// (including the exact editor-green base color) used to be hardcoded in EditorUVE's creation
/// switch behind the legacy EditorEntityKindUVE::Plane case; it now has the same per-file home
/// every other node kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule
/// this holds the *recipe*, not a second copy of component storage: the mesh record still lives
/// only in PrimitiveMeshComponentUVE (geometry itself is renderer-owned).
struct PlaneMesh3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. "Plane" preserves
    /// the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Plane";

    /// Ground-plane primitive authored defaults (editor-green base color, matching the legacy
    /// recipe).
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Plane, Math::Vector3UVE{0.32F, 0.38F, 0.30F}};
    /// Thin flat collision authored defaults: a floor slab, not a full-height box.
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.025F, 0.5F}};
};

[[nodiscard]] bool IsPlaneMesh3DNodeDefinitionValidUVE(const PlaneMesh3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyPlaneMesh3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const PlaneMesh3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
