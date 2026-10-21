// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/deterministic_physics_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace UVE::Physics {

namespace {

/// Above this the lattice coordinate would overflow int64 after the divide, and below it the
/// lattice is too coarse to represent anything meaningful - both mean the caller passed a quantum
/// no simulation could use, so the value is reported as "off lattice" (zero) rather than wrapped.
constexpr float kMinimumQuantumUVE = 1.0e-9F;
constexpr double kMaximumLatticeMagnitudeUVE = 9.0e18;

[[nodiscard]] std::int64_t QuantizeDoubleUVE(const double value, const double quantum) noexcept {
    if (!std::isfinite(value) || !(quantum > 0.0)) {
        return 0;
    }
    const double lattice = std::nearbyint(value / quantum);
    if (!std::isfinite(lattice) || std::abs(lattice) >= kMaximumLatticeMagnitudeUVE) {
        return 0;
    }
    return static_cast<std::int64_t>(lattice);
}

[[nodiscard]] std::uint64_t HashVectorUVE(std::uint64_t seed, const Math::Vector3UVE& value,
                                         const float quantum) noexcept {
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.x, quantum)));
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.y, quantum)));
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.z, quantum)));
    return seed;
}

[[nodiscard]] std::uint64_t HashQuaternionUVE(std::uint64_t seed, const Math::QuaternionUVE& value,
                                             const float quantum) noexcept {
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.x, quantum)));
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.y, quantum)));
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.z, quantum)));
    seed = HashScalarUVE(seed, static_cast<std::uint64_t>(QuantizeToLatticeUVE(value.w, quantum)));
    return seed;
}

} // namespace

std::int64_t QuantizeToLatticeUVE(const float value, const float quantum) noexcept {
    if (!std::isfinite(value) || !std::isfinite(quantum) || !(quantum >= kMinimumQuantumUVE)) {
        return 0;
    }
    // The division runs in double on purpose: `value / quantum` in float can land a hair either
    // side of a lattice midpoint, and a midpoint that rounds differently on two machines is
    // exactly the class of drift this function exists to remove.
    return QuantizeDoubleUVE(static_cast<double>(value), static_cast<double>(quantum));
}

float LatticeToScalarUVE(const std::int64_t lattice, const float quantum) noexcept {
    if (!std::isfinite(quantum) || quantum <= 0.0F) {
        return 0.0F;
    }
    return static_cast<float>(static_cast<double>(lattice) * static_cast<double>(quantum));
}

std::uint64_t HashScalarUVE(std::uint64_t seed, const std::uint64_t value) noexcept {
    return HashBytesUVE(seed, &value, sizeof(value));
}

std::uint64_t HashBytesUVE(std::uint64_t seed, const void* const data, const std::size_t size) noexcept {
    if (data == nullptr || size == 0U) {
        return seed;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        seed ^= static_cast<std::uint64_t>(bytes[index]);
        seed *= kFnvPrime64UVE;
    }
    return seed;
}

std::uint64_t ComputeWorldDigestUVE(const std::span<const DeterministicBodySampleUVE> samples,
                                    const DeterministicStepSettingsUVE& settings) noexcept {
    // Sorted by stable key, never by address or discovery order: the digest must describe the
    // world, not the container the world happens to live in.
    std::vector<DeterministicBodySampleUVE> ordered(samples.begin(), samples.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const DeterministicBodySampleUVE& lhs, const DeterministicBodySampleUVE& rhs) {
                  return MakeBodyKeyUVE(lhs.entity) < MakeBodyKeyUVE(rhs.entity);
              });

    std::uint64_t digest = kFnvOffsetBasis64UVE;
    // The count goes in first, so adding or removing a body can never collide with a different
    // count that happens to hash to the same stream.
    digest = HashScalarUVE(digest, static_cast<std::uint64_t>(ordered.size()));
    for (const DeterministicBodySampleUVE& sample : ordered) {
        const DeterministicBodyKeyUVE key = MakeBodyKeyUVE(sample.entity);
        digest = HashScalarUVE(digest, static_cast<std::uint64_t>(key.index));
        digest = HashScalarUVE(digest, static_cast<std::uint64_t>(key.generation));
        digest = HashVectorUVE(digest, sample.position, settings.positionQuantum);
        // Rotation reuses the position lattice: a quaternion component is a dimensionless number
        // in [-1, 1], and giving it its own spacing would only invite the two to be tuned apart.
        digest = HashQuaternionUVE(digest, sample.rotation, settings.positionQuantum);
        digest = HashVectorUVE(digest, sample.velocity, settings.velocityQuantum);
        digest = HashVectorUVE(digest, sample.angularVelocity, settings.velocityQuantum);
    }
    return digest;
}

void CanonicalizeCollisionPairsUVE(std::vector<CollisionPairUVE>& pairs) noexcept {
    for (CollisionPairUVE& pair : pairs) {
        if (MakeBodyKeyUVE(pair.second) < MakeBodyKeyUVE(pair.first)) {
            std::swap(pair.first, pair.second);
            // `separationAxis` points from `first` toward `second`. Swapping the endpoints without
            // flipping the axis would push both bodies deeper into each other.
            pair.separationAxis = Math::Vector3UVE{-pair.separationAxis.x, -pair.separationAxis.y,
                                                   -pair.separationAxis.z};
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const CollisionPairUVE& lhs, const CollisionPairUVE& rhs) {
                  const DeterministicBodyKeyUVE lhsFirst = MakeBodyKeyUVE(lhs.first);
                  const DeterministicBodyKeyUVE rhsFirst = MakeBodyKeyUVE(rhs.first);
                  if (lhsFirst < rhsFirst) {
                      return true;
                  }
                  if (rhsFirst < lhsFirst) {
                      return false;
                  }
                  return MakeBodyKeyUVE(lhs.second) < MakeBodyKeyUVE(rhs.second);
              });
}

} // namespace UVE::Physics
