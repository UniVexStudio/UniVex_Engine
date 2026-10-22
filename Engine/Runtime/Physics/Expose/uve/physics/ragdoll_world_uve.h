// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "uve/component/entity_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"

namespace UVE::Physics {

/// Which world geometry a ragdoll is allowed to hit, and what it must ignore.
struct RagdollCollisionFilterUVE final {
    /// The ragdoll's own layer bits. A collider participates when at least one of these bits also
    /// appears in the collider's mask - a one-sided test, because a ragdoll is not an entity and has
    /// no `collisionLayer` of its own to offer the other direction.
    std::uint32_t layerMask = 0xFFFFFFFFU;
    /// The skeleton's own entity. Its collider, if it has one, is the capsule the character was
    /// standing in when it died; letting the ragdoll collide with it would make every corpse
    /// explode at the moment of the switch.
    Scene::EntityUVE ignoreEntity = Scene::kInvalidEntityUVE;
    /// One more entity to ignore - typically the hitbox owner or a grab-volume the caller does not
    /// want the body to fight with.
    Scene::EntityUVE alsoIgnoreEntity = Scene::kInvalidEntityUVE;
};

struct RagdollContactStatsUVE final {
    std::size_t testedColliderCount = 0U;
    std::size_t generatedContactCount = 0U;
    /// True when the caller's output buffer filled up. The solver still gets a consistent subset -
    /// the deepest contacts, not whichever ones happened to come first - so a truncated frame
    /// degrades rather than resolving the wrong thing.
    bool truncated = false;
};

/// One body's capsule against one world collider.
///
/// This is the whole "the ragdoll reacts when it hits the mesh" contract, and it is deliberately a
/// pure function over a collider snapshot: no ECS access, no allocation, no hidden state. The caller
/// builds the snapshot with `Detail::BuildColliderWorldAabbCacheUVE` once per frame and hands the
/// same array to every ragdoll in the scene.
///
/// Exact shape pairs, in the order the narrow-phase helpers already implement them:
///  * body capsule vs sphere collider   - capsule/sphere
///  * body capsule vs capsule collider  - capsule/capsule
///  * body capsule vs box collider      - capsule/oriented box, so a rotated ramp is not approximated
///  * body capsule vs cylinder collider - capsule/AABB, the one documented approximation: a cylinder
///    has no dedicated narrow phase in this engine yet, and its cached AABB is what a cylinder
///    collider already reports to collision and raycasts.
///
/// The returned normals push the BODY out, which is why every one of them is the negation of the
/// narrow-phase axis: those helpers all report an axis pointing from the capsule toward the other
/// shape, because they were written for the pair resolver that moves the second body.
///
/// Friction and restitution are the combination of the body's authored values and the collider's own
/// material, so a corpse on ice slides and a corpse on carpet does not.
[[nodiscard]] RagdollContactStatsUVE BuildRagdollContactsUVE(
    const RagdollSolverUVE& solver, std::span<const Detail::ColliderWorldAabbUVE> colliders,
    const RagdollCollisionFilterUVE& filter, std::span<RagdollContactUVE> outContacts) noexcept;

} // namespace UVE::Physics
