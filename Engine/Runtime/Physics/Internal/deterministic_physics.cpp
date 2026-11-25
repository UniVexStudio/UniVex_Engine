// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "deterministic_physics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace UVE::Physics {

namespace {

/// Above this the lattice coordinate would overflow int64 after the divide, and below it the
/// lattice is too coarse to represent anything meaningful - both mean the caller passed a quantum
/// no simulation could use, so the value is reported as "off lattice" (zero) rather than wrapped.
constexpr float kMinimumQuantum = 1.0e-9F;
constexpr double kMaximumLatticeMagnitude = 9.0e18;

[[nodiscard]] std::int64_t QuantizeDouble(const double value, const double quantum) noexcept {
    if (!std::isfinite(value) || !(quantum > 0.0)) {
        return 0;
    }
    const double lattice = std::nearbyint(value / quantum);
    if (!std::isfinite(lattice) || std::abs(lattice) >= kMaximumLatticeMagnitude) {
        return 0;
    }
    return static_cast<std::int64_t>(lattice);
}

[[nodiscard]] std::uint64_t HashVector(std::uint64_t seed, const Math::Vector3& value,
                                         const float quantum) noexcept {
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.x, quantum)));
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.y, quantum)));
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.z, quantum)));
    return seed;
}

[[nodiscard]] std::uint64_t HashQuaternion(std::uint64_t seed, const Math::Quaternion& value,
                                             const float quantum) noexcept {
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.x, quantum)));
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.y, quantum)));
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.z, quantum)));
    seed = HashScalar(seed, static_cast<std::uint64_t>(QuantizeToLattice(value.w, quantum)));
    return seed;
}

} // namespace

std::int64_t QuantizeToLattice(const float value, const float quantum) noexcept {
    if (!std::isfinite(value) || !std::isfinite(quantum) || !(quantum >= kMinimumQuantum)) {
        return 0;
    }
    // The division runs in double on purpose: `value / quantum` in float can land a hair either
    // side of a lattice midpoint, and a midpoint that rounds differently on two machines is
    // exactly the class of drift this function exists to remove.
    return QuantizeDouble(static_cast<double>(value), static_cast<double>(quantum));
}

float LatticeToScalar(const std::int64_t lattice, const float quantum) noexcept {
    if (!std::isfinite(quantum) || quantum <= 0.0F) {
        return 0.0F;
    }
    return static_cast<float>(static_cast<double>(lattice) * static_cast<double>(quantum));
}

std::uint64_t HashScalar(std::uint64_t seed, const std::uint64_t value) noexcept {
    return HashBytes(seed, &value, sizeof(value));
}

std::uint64_t HashBytes(std::uint64_t seed, const void* const data, const std::size_t size) noexcept {
    if (data == nullptr || size == 0U) {
        return seed;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        seed ^= static_cast<std::uint64_t>(bytes[index]);
        seed *= kFnvPrime64;
    }
    return seed;
}

std::uint64_t ComputeWorldDigest(const std::span<const DeterministicBodySample> samples,
                                    const DeterministicStepSettings& settings) noexcept {
    // Sorted by stable key, never by address or discovery order: the digest must describe the
    // world, not the container the world happens to live in.
    std::vector<DeterministicBodySample> ordered(samples.begin(), samples.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const DeterministicBodySample& lhs, const DeterministicBodySample& rhs) {
                  return MakeBodyKey(lhs.entity) < MakeBodyKey(rhs.entity);
              });

    std::uint64_t digest = kFnvOffsetBasis64;
    // The count goes in first, so adding or removing a body can never collide with a different
    // count that happens to hash to the same stream.
    digest = HashScalar(digest, static_cast<std::uint64_t>(ordered.size()));
    for (const DeterministicBodySample& sample : ordered) {
        const DeterministicBodyKey key = MakeBodyKey(sample.entity);
        digest = HashScalar(digest, static_cast<std::uint64_t>(key.index));
        digest = HashScalar(digest, static_cast<std::uint64_t>(key.generation));
        digest = HashVector(digest, sample.position, settings.positionQuantum);
        // Rotation reuses the position lattice: a quaternion component is a dimensionless number
        // in [-1, 1], and giving it its own spacing would only invite the two to be tuned apart.
        digest = HashQuaternion(digest, sample.rotation, settings.positionQuantum);
        digest = HashVector(digest, sample.velocity, settings.velocityQuantum);
        digest = HashVector(digest, sample.angularVelocity, settings.velocityQuantum);
    }
    return digest;
}

void CanonicalizeCollisionPairs(std::vector<CollisionPair>& pairs) noexcept {
    for (CollisionPair& pair : pairs) {
        if (MakeBodyKey(pair.second) < MakeBodyKey(pair.first)) {
            std::swap(pair.first, pair.second);
            // `separationAxis` points from `first` toward `second`. Swapping the endpoints without
            // flipping the axis would push both bodies deeper into each other.
            pair.separationAxis = Math::Vector3{-pair.separationAxis.x, -pair.separationAxis.y,
                                                   -pair.separationAxis.z};
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const CollisionPair& lhs, const CollisionPair& rhs) {
                  const DeterministicBodyKey lhsFirst = MakeBodyKey(lhs.first);
                  const DeterministicBodyKey rhsFirst = MakeBodyKey(rhs.first);
                  if (lhsFirst < rhsFirst) {
                      return true;
                  }
                  if (rhsFirst < lhsFirst) {
                      return false;
                  }
                  return MakeBodyKey(lhs.second) < MakeBodyKey(rhs.second);
              });
}

} // namespace UVE::Physics
