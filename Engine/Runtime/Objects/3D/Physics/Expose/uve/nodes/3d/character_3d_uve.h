// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Character3D: a body moved by its own code rather than by forces - a player, an NPC, an
/// enemy. Node3D > PhysicsObject3D > SolidBody3D > Character3D.
///
/// A new one is ready to walk: it comes with a person-sized capsule and the built-in movement on,
/// so dropping one onto a floor and pressing Play is enough to move it around. No rigid body is
/// attached - the controller owns all of its motion.
struct Character3DNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "Character3D";

    /// Person-sized: 1.8 m tall, 0.8 m across.
    ColliderComponentUVE collider = MakeDefaultColliderUVE();
    CharacterControllerComponentUVE controller{};

    [[nodiscard]] static ColliderComponentUVE MakeDefaultColliderUVE() noexcept {
        ColliderComponentUVE collider{};
        collider.shapeType = ColliderShapeTypeUVE::Capsule;
        collider.radius = 0.4F;
        collider.height = 1.8F;
        return collider;
    }
};

[[nodiscard]] bool IsCharacter3DNodeDefinitionValidUVE(const Character3DNodeDefinitionUVE& value) noexcept;

/// Applies the SolidBody3D base (and through it PhysicsObject3D and Node3D), then the collider
/// and the controller, each only where the entity does not already have one.
void ApplyCharacter3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const Character3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
