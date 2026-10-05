// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/physics_object_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Abstract physics bases between Object3D and concrete 3D physics objects. These are never created
/// directly; concrete objects apply them so the authored component hierarchy stays consistent:
///
///   Object3D
///   +- PhysicsObject3D       takes part in collision       (PhysicsObjectComponentUVE)
///      +- SolidBody3D        is stopped by what it hits    (SolidBodyComponentUVE)
struct PhysicsObject3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "PhysicsObject3D";
};

struct SolidBody3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "SolidBody3D";
};

/// Applies Object3D and the PhysicsObject3D base component if they are missing.
void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 std::string_view nameFallback);
/// Applies PhysicsObject3D, then the SolidBody3D component if it is missing.
void ApplySolidBody3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                             std::string_view nameFallback);

// =================================================================================================
// What the PhysicsObject3D state means to the physics world.
//
// A physics object's Process mode says whether it is running; `disableMode` says what that should
// meant to the bodies around it. These functions are that one rule, resolved in one place and read
// by collision, raycasts, shape casts, areas and the simulation itself - so an object "taken out"
// is taken out of all of them at once, not of whichever system remembered to check.
// =================================================================================================

/// How much of the physics world a physics object is part of right now.
enum class PhysicsObjectParticipationUVE : std::uint8_t {
    /// Collides, is hit by queries, and the simulation moves it. The answer for everything that is
    /// running, and for entities that are not physics objects at all.
    Active = 0,
    /// Still in the world as an immovable obstacle - what a stopped object asks for when its
    /// disable mode is MakeStatic. Queries still find it; nothing can push it and it never moves.
    StaticOnly,
    /// Out of the physics world entirely: nothing collides with it, no query finds it, and the
    /// simulation neither moves nor deflects it. What Remove asks for.
    Removed,
};

/// The object's participation. `simulationPaused` is how the caller's own schedule sees the pause
/// state: the fixed step always asks with false, because it does not run while paused in the first
/// place, and a frame-time consumer passes whatever the running/paused answer is.
[[nodiscard]] PhysicsObjectParticipationUVE ResolvePhysicsObjectParticipationUVE(
    const IEntityManagerUVE& entityManager, EntityUVE entity, bool simulationPaused) noexcept;

/// Whether the object takes part in collision and queries at all - true for everything except an
/// object whose disable mode says Remove while it is not running.
[[nodiscard]] bool IsPhysicsObjectInWorldUVE(const IEntityManagerUVE& entityManager,
                                             EntityUVE entity) noexcept;

/// Whether the simulation may move this object. False for a removed object and for one kept as a
/// static obstacle, so the same question answers "may it be integrated" and "may it be pushed".
[[nodiscard]] bool IsPhysicsObjectSimulatedUVE(const IEntityManagerUVE& entityManager,
                                               EntityUVE entity) noexcept;

/// The authored collision priority, or 1 for an entity that is not a physics object. 0 is legal and
/// means "never yields" - see GetPhysicsObjectYieldWeightUVE.
[[nodiscard]] float GetPhysicsObjectCollisionPriorityUVE(const IEntityManagerUVE& entityManager,
                                                         EntityUVE entity) noexcept;

/// How much of an overlap this object gives way to: `inverseMass` divided by its collision
/// priority, so heavier bodies and higher priorities yield less. Only the ratio between the two
/// sides of a contact matters, and a priority of 0 - or no mass to give way with - weighs nothing,
/// which makes the object immovable in that contact without a second code path for "static".
[[nodiscard]] float GetPhysicsObjectYieldWeightUVE(const IEntityManagerUVE& entityManager,
                                                   EntityUVE entity, float inverseMass) noexcept;

} // namespace UVE::Scene
