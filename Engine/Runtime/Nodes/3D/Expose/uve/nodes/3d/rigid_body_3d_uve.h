// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/rigid_body_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the RigidBody3D scene node: the component set and defaults a freshly
/// created RigidBody3D entity attaches — one simulated rigid body (dynamic by default; gravity
/// and collision response move it, via Physics/PhysicsSystemUVE). This recipe used to be
/// hardcoded inline in EditorUVE's creation switch; it now has the same per-file home every
/// other node kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: body state itself still lives
/// only in RigidBodyComponentUVE.
struct RigidBody3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created RigidBody3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "RigidBody3D";

    /// Simulated-body authored defaults; the entity's transform is attached by the creation shell.
    RigidBodyComponentUVE body{};
};

[[nodiscard]] bool IsRigidBody3DNodeDefinitionValidUVE(const RigidBody3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyRigidBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const RigidBody3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
