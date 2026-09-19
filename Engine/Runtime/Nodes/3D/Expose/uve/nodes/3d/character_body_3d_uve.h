// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_body_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the CharacterBody3D scene node: the component set and defaults a
/// freshly created CharacterBody3D entity attaches — a collider plus a kinematic rigid body,
/// which is the configuration Physics/CharacterControllerUVE drives (gravity never moves it; the
/// controller does). This recipe used to be hardcoded inline in EditorUVE's creation switch; it
/// now has the same per-file home every other node kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage.
struct CharacterBody3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty".)
    static constexpr std::string_view defaultName = "CharacterBody3D";

    /// Character authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
    /// Kinematic by default so the character controller owns all motion (matches the recipe the
    /// editor previously hardcoded: default body with isKinematic flipped to true).
    RigidBodyComponentUVE body = MakeDefaultBodyUVE();

    [[nodiscard]] static RigidBodyComponentUVE MakeDefaultBodyUVE() noexcept {
        RigidBodyComponentUVE body{};
        body.isKinematic = true;
        return body;
    }
};

[[nodiscard]] bool IsCharacterBody3DNodeDefinitionValidUVE(const CharacterBody3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyCharacterBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const CharacterBody3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
