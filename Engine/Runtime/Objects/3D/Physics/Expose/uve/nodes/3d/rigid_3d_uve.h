// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/rigid_body_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Rigid3D scene node: the component set and defaults a freshly
/// created Rigid3D entity attaches — one simulated rigid body (dynamic by default; gravity
/// and collision response move it, via Physics/PhysicsSystemUVE). This recipe used to be
/// hardcoded inline in EditorUVE's creation switch; it now has the same per-file home every
/// other node kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: body state itself still lives
/// only in RigidBodyComponentUVE.
struct Rigid3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created Rigid3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Rigid3D";

    /// Simulated-body authored defaults; the entity's transform is attached by the creation shell.
    RigidBodyComponentUVE body{};
};

[[nodiscard]] bool IsRigid3DNodeDefinitionValidUVE(const Rigid3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyRigid3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Rigid3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
