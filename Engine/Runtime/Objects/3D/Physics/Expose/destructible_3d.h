// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "aabb.h"
#include "entity.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct DestructibleChunkUVE final {
    Math::Vector3UVE localCentroid{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE halfExtents{0.25F, 0.25F, 0.25F};
    float mass = 1.0F;
    std::vector<Math::Vector3UVE> vertices;
    std::vector<std::uint32_t> indices;
    Math::Vector3UVE initialLinearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE initialAngularVelocity{0.0F, 0.0F, 0.0F};
};

/// Fracturable 3D physics object component (`health`, `impactImpulseThreshold`, `chunkCount`,
/// `explosionImpulse`, `debrisLifetimeSeconds`).
struct Destructible3DComponentUVE final {
    Math::AabbUVE localBounds{Math::Vector3UVE{-0.5F, -0.5F, -0.5F},
                              Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    float health = 100.0F;
    float impactImpulseThreshold = 15.0F;
    std::uint32_t chunkCount = 8U;
    float totalMass = 16.0F;
    float explosionImpulse = 6.0F;
    float debrisLifetimeSeconds = 10.0F;
    std::uint32_t fractureSeed = 1337U;
    bool fractured = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Destructible3DComponentUVE&) const = default;
};

struct DestructibleDebrisComponentUVE final {
    EntityUVE sourceEntity = kInvalidEntityUVE;
    Math::Vector3UVE halfExtents{0.25F, 0.25F, 0.25F};
    Math::Vector3UVE linearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE angularVelocity{0.0F, 0.0F, 0.0F};
    float mass = 1.0F;
    float remainingLifetimeSeconds = 10.0F;

    [[nodiscard]] bool operator==(const DestructibleDebrisComponentUVE&) const = default;
};

/// Deterministically fractures `localBounds` into `chunkCount` airtight convex box-cell chunks
/// conserving total volume and mass, assigning radial outward linear & angular velocities from
/// `impactPointLocal`.
[[nodiscard]] std::vector<DestructibleChunkUVE> FractureBoundsIntoChunksUVE(
    const Math::AabbUVE& localBounds, std::uint32_t chunkCount, float totalMass,
    std::uint32_t seed, const Math::Vector3UVE& impactPointLocal, float explosionImpulse);

/// Applies impact `impulse` at `impactPointWorld` to a `Destructible3DComponentUVE` entity.
/// When damage reaches zero or `impulse >= impactImpulseThreshold`, fractures the entity and
/// spawns debris entities returned in `outDebrisEntities`.
[[nodiscard]] bool ApplyDestructibleImpactUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              float impulse,
                                              const Math::Vector3UVE& impactPointWorld,
                                              std::vector<EntityUVE>* outDebrisEntities = nullptr);

/// Integrates active `DestructibleDebrisComponentUVE` entities under `gravity` and destroys
/// expired debris. Returns the count of surviving debris entities stepped.
std::size_t UpdateDestructibleDebrisUVE(IEntityManagerUVE& entityManager,
                                        const Math::Vector3UVE& gravity, float deltaTimeSeconds);

} // namespace UVE::Scene
