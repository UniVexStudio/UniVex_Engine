// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/rigid_body_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Rigid3D scene object: the component set and defaults a freshly
/// created Rigid3D entity attaches — one simulated rigid body (dynamic by default; gravity
/// and collision response move it, via Physics/PhysicsSystemUVE). This recipe used to be
/// hardcoded inline in EditorUVE's creation switch; it now has the same per-file home every
/// other object kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: body state itself still lives
/// only in RigidBodyComponentUVE.
struct Rigid3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty" — the editor created Rigid3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Rigid3D";

    /// Simulated-body authored defaults; the entity's transform is attached by the creation shell.
    RigidBodyComponentUVE body{};
};

[[nodiscard]] bool IsRigid3DObjectDefinitionValidUVE(const Rigid3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyRigid3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Rigid3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
