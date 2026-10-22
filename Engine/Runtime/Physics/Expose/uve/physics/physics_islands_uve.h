// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <vector>

#include "uve/physics/collision_pair_uve.h"

namespace UVE::Physics {

/// One simulation island: a maximal set of bodies connected, directly or transitively, through the
/// step's collision pairs.
///
/// The point of an island is independence. Two islands share no body, so nothing the solver does
/// inside one can be observed by the other - no corrected position, no deflected velocity, no
/// ordering. That is what makes them safe to solve concurrently, and it is also why grouping can
/// reorder the pairs: a step that resolves island 0's pairs before island 1's produces exactly the
/// same state as the original interleaving, because no body is in both.
///
/// Terrain-free static geometry does not split islands. A static collider that touches two dynamic
/// bodies puts them in the same island even though the static side never moves, because both are
/// compared against the same penetration/axis data and only the dynamic sides are written - there
/// is no state on the static side to merge, and inventing one would create false dependencies.
///
/// The islands are built from the pair list as given, so the caller's order is the contract: the
/// deterministic path canonicalizes pairs *before* island building, and every path here preserves
/// the relative order of pairs inside an island. Nothing is sorted by address or hash order.
struct PhysicsIslandUVE final {
    /// Index into PhysicsIslandPartitionUVE::pairOrderUVE of this island's first pair.
    std::uint32_t firstPairUVE = 0U;
    /// How many pairs belong to this island.
    std::uint32_t pairCountUVE = 0U;
    /// Distinct bodies touched by this island's pairs. Reported for budgeting and diagnostics; the
    /// solver does not need it.
    std::uint32_t bodyCountUVE = 0U;
};

/// The pair list reordered into islands, plus the island table that indexes into it.
///
/// `pairOrderUVE` is a permutation of [0, pairs.size()): pairs of island 0 first, in their original
/// relative order, then island 1's, and so on. Island ids follow first appearance in the input, so
/// island 0 is the one containing pair 0 - which makes the whole structure reproducible from the
/// pair list alone, with no dependence on entity storage order or hashing.
struct PhysicsIslandPartitionUVE final {
    std::vector<std::uint32_t> pairOrderUVE;
    std::vector<PhysicsIslandUVE> islandsUVE;
    /// Distinct bodies across every island: the size of the snapshot the solver has to take.
    std::uint32_t bodyCountUVE = 0U;

    [[nodiscard]] bool IsEmptyUVE() const noexcept { return islandsUVE.empty(); }
};

/// Groups the collision pairs into simulation islands.
///
/// Union-find with path halving over a dense index assigned in pair-scan order, so the merge is
/// O(pairs * alpha) and touches neither the entity manager nor the scene graph - it runs on a
/// snapshot of the pair list only.
///
/// The merge itself is serial on purpose. A lock-free/parallel union-find (the technique Jolt
/// Physics uses to rebuild islands during its parallel narrow phase) produces an island numbering
/// that depends on which thread won the compare-and-swap, which is exactly the kind of hidden
/// order dependence a deterministic step cannot have; Box2D v3 reached the same conclusion and kept
/// its island merge serial for the same reason. Here the merge is a few nanoseconds per pair
/// relative to the solve it feeds, so serial costs nothing measurable and buys a reproducible
/// partition.
///
/// Pairs with `first == second` are ignored (a body cannot collide with itself): they contribute no
/// union, so they must not create an island of their own either. They are still kept out of
/// `pairOrderUVE`, because resolving a self-pair is meaningless.
[[nodiscard]] PhysicsIslandPartitionUVE BuildPhysicsIslandPartitionUVE(
    const std::vector<CollisionPairUVE>& pairs);

} // namespace UVE::Physics
