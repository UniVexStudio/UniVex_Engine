// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/collision_pair_uve.h"

namespace UVE::Physics {

/// Configuration for reproducible stepping.
///
/// Two independent things make a fixed step reproducible across peers, and this feature is only
/// honest if both are stated separately:
///
/// 1. **Order.** The solver is sequential - every resolved pair writes positions the next pair
///    reads - so the result depends on the order pairs are resolved in. That order came from
///    entity storage, which is creation order, which two peers need not share. Canonicalising the
///    pair list removes that source entirely, and is exact: no approximation, no quality cost.
/// 2. **Quantization.** Identical code and identical order still produce identical bits on one
///    build, but not necessarily across compilers or CPUs that contract floating-point operations
///    differently. Every comparison therefore happens on a lattice: state that differs by less
///    than one quantum is treated as equal. Quantizing the SIMULATION would trade real accuracy
///    for that (a body moving less than a quantum per step would stop moving), so only the digest -
///    the thing that answers "are these two simulations still the same?" - is quantized.
struct DeterministicStepSettingsUVE final {
    bool enabled = false;

    /// Lattice spacing for positions, in metres. 1/1024 is about a millimetre: fine enough that no
    /// visible motion is ever swallowed, coarse enough to absorb the last float bit or two.
    float positionQuantum = 1.0F / 1024.0F;

    /// Lattice spacing for linear and angular velocity, in their own units.
    float velocityQuantum = 1.0F / 1024.0F;
};

/// Stable ordering key for an entity.
///
/// Entity indices are recycled, so the generation is part of the key: two different bodies can
/// occupy the same index in different runs, and ordering by index alone would silently reorder
/// them. Ordered lexicographically, index first.
struct DeterministicBodyKeyUVE final {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;

    [[nodiscard]] constexpr bool operator<(const DeterministicBodyKeyUVE& rhs) const noexcept {
        return index < rhs.index || (index == rhs.index && generation < rhs.generation);
    }
    [[nodiscard]] constexpr bool operator==(const DeterministicBodyKeyUVE& rhs) const noexcept {
        return index == rhs.index && generation == rhs.generation;
    }
};

[[nodiscard]] constexpr DeterministicBodyKeyUVE MakeBodyKeyUVE(const Scene::EntityUVE entity) noexcept {
    return DeterministicBodyKeyUVE{entity.index, entity.generation};
}

/// Maps a value onto the integer lattice of spacing `quantum`.
///
/// Returns 0 for a non-finite value or a non-positive quantum, so a NaN can never reach the digest
/// as a paylod-dependent bit pattern - two peers with the same broken input must agree that it is
/// broken, not disagree on how.
[[nodiscard]] std::int64_t QuantizeToLatticeUVE(float value, float quantum) noexcept;

/// A lattice coordinate back as a float. The single multiplication is the only rounding step, so
/// the result is bit-identical wherever the integer is.
[[nodiscard]] float LatticeToScalarUVE(std::int64_t lattice, float quantum) noexcept;

/// One body's state as the digest sees it. Positions and velocities are already world-space: the
/// digest never reads a transform hierarchy, because walking one would reintroduce a traversal
/// order.
struct DeterministicBodySampleUVE final {
    Scene::EntityUVE entity{};
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE velocity{};
    Math::Vector3UVE angularVelocity{};
};

/// FNV-1a, 64-bit. Chosen over a cryptographic hash because the input is a fixed, small,
/// non-adversarial record and this runs per body per step; replay verification compares hashes for
/// equality, never for secrecy.
inline constexpr std::uint64_t kFnvOffsetBasis64UVE = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime64UVE = 1099511628211ULL;

[[nodiscard]] std::uint64_t HashBytesUVE(std::uint64_t seed, const void* data, std::size_t size) noexcept;

[[nodiscard]] std::uint64_t HashScalarUVE(std::uint64_t seed, std::uint64_t value) noexcept;

/// Digest of one world state, independent of the order `samples` happens to be in.
///
/// The samples are sorted by their stable key first, so a scene built in a different order - or
/// stored in a different archetype layout - digests identically. That property is the whole point:
/// a digest that only matched when the storage matched would report false divergence on every
/// peer, which is worse than no digest at all.
[[nodiscard]] std::uint64_t ComputeWorldDigestUVE(
    std::span<const DeterministicBodySampleUVE> samples,
    const DeterministicStepSettingsUVE& settings) noexcept;

/// Puts a pair list into canonical order and orientation.
///
/// Two separate normalizations, both required:
/// - **Order**: pairs are sorted by (first, second) stable keys, so a scene whose entities were
///   created in a different order resolves its overlaps in the same sequence.
/// - **Orientation**: within each pair the lower key becomes `first` again, and `separationAxis` is
///   negated when a swap happens. `separationAxis` points from `first` toward `second`, so a swap
///   without the negation would resolve every corrected pair in the wrong direction.
///
/// Deterministic, in-place, no allocation.
void CanonicalizeCollisionPairsUVE(std::vector<CollisionPairUVE>& pairs) noexcept;

} // namespace UVE::Physics
