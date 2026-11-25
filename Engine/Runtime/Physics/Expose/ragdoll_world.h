// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "entity.h"
#include "collider_world_aabb_cache.h"
#include "ragdoll_solver.h"

namespace UVE::Physics {

/// Which world geometry a ragdoll is allowed to hit, and what it must ignore.
struct RagdollCollisionFilter final {
    /// The ragdoll's own layer bits. A collider participates when at least one of these bits also
    /// appears in the collider's mask - a one-sided test, because a ragdoll is not an entity and has
    /// no `collisionLayer` of its own to offer the other direction.
    std::uint32_t layerMask = 0xFFFFFFFFU;
    /// The skeleton's own entity. Its collider, if it has one, is the capsule the character was
    /// standing in when it died; letting the ragdoll collide with it would make every corpse
    /// explode at the moment of the switch.
    Scene::Entity ignoreEntity = Scene::kInvalidEntity;
    /// One more entity to ignore - typically the hitbox owner or a grab-volume the caller does not
    /// want the body to fight with.
    Scene::Entity alsoIgnoreEntity = Scene::kInvalidEntity;
};

/// Optional velocity and dynamic-body metadata aligned 1:1 with the `colliders` snapshot.
/// Allows ragdolls to react to moving platforms / dynamic `Rigid3D` props and apply two-way pushback
/// while keeping sleeping corpses asleep on stationary floors.
struct RagdollColliderMotion final {
    Math::Vector3 linearVelocity{};
    Math::Vector3 angularVelocity{};
    Math::Vector3 worldCenter{};
    float inverseMass = 0.0F;
    bool isDynamic = false;

    [[nodiscard]] bool operator==(const RagdollColliderMotion&) const noexcept = default;
};

struct RagdollContactStats final {
    std::size_t testedColliderCount = 0U;
    std::size_t broadphaseRejectedCount = 0U;
    std::size_t generatedContactCount = 0U;
    std::size_t dynamicContactCount = 0U;
    /// True if at least one contact hit a collider moving faster than the sleep wake speed.
    bool hasMovingColliderContact = false;
    /// True if at least one contact provides upward floor support (normal.y > 0.35).
    bool hasFloorSupport = false;
    /// True when the caller's output buffer filled up.
    bool truncated = false;
};

/// Pre-filters `colliders` against the ragdoll's aggregate world AABB (expanded by `expandMargin`)
/// and collision filter so multi-substep frames only run narrow-phase against nearby geometry.
void BuildRagdollBroadphaseCandidates(const RagdollSolver& solver,
                                         std::span<const Detail::ColliderWorldAabb> colliders,
                                         const RagdollCollisionFilter& filter, float expandMargin,
                                         std::vector<std::size_t>& outCandidateIndices);

/// Builds exact world-space contacts between all ragdoll body capsules and world colliders.
///
/// Uses AABB broadphase rejection, exact closest-point contact generation for Sphere, Capsule,
/// Oriented Box (with 2-point flat-surface capsule manifold for zero-rocking floor stability), and
/// Cylinder AABBs, plus optional dynamic-collider velocity and pushback tagging.
[[nodiscard]] RagdollContactStats BuildRagdollContacts(
    const RagdollSolver& solver, std::span<const Detail::ColliderWorldAabb> colliders,
    const RagdollCollisionFilter& filter, std::span<RagdollContact> outContacts,
    std::span<const RagdollColliderMotion> colliderMotions = {},
    std::span<const std::size_t> candidateIndices = {}) noexcept;

} // namespace UVE::Physics
