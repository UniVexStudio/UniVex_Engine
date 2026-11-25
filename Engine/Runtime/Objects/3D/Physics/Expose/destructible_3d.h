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

class IEntityManager;

struct DestructibleChunk final {
    Math::Vector3 localCentroid{0.0F, 0.0F, 0.0F};
    Math::Vector3 halfExtents{0.25F, 0.25F, 0.25F};
    float mass = 1.0F;
    std::vector<Math::Vector3> vertices;
    std::vector<std::uint32_t> indices;
    Math::Vector3 initialLinearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3 initialAngularVelocity{0.0F, 0.0F, 0.0F};
};

/// Fracturable 3D physics object component (`health`, `impactImpulseThreshold`, `chunkCount`,
/// `explosionImpulse`, `debrisLifetimeSeconds`).
struct Destructible3DComponent final {
    Math::Aabb localBounds{Math::Vector3{-0.5F, -0.5F, -0.5F},
                              Math::Vector3{0.5F, 0.5F, 0.5F}};
    float health = 100.0F;
    float impactImpulseThreshold = 15.0F;
    std::uint32_t chunkCount = 8U;
    float totalMass = 16.0F;
    float explosionImpulse = 6.0F;
    float debrisLifetimeSeconds = 10.0F;
    std::uint32_t fractureSeed = 1337U;
    bool fractured = false;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Destructible3DComponent&) const = default;
};

struct DestructibleDebrisComponent final {
    Entity sourceEntity = kInvalidEntity;
    Math::Vector3 halfExtents{0.25F, 0.25F, 0.25F};
    Math::Vector3 linearVelocity{0.0F, 0.0F, 0.0F};
    Math::Vector3 angularVelocity{0.0F, 0.0F, 0.0F};
    float mass = 1.0F;
    float remainingLifetimeSeconds = 10.0F;

    [[nodiscard]] bool operator==(const DestructibleDebrisComponent&) const = default;
};

/// Deterministically fractures `localBounds` into `chunkCount` airtight convex box-cell chunks
/// conserving total volume and mass, assigning radial outward linear & angular velocities from
/// `impactPointLocal`.
[[nodiscard]] std::vector<DestructibleChunk> FractureBoundsIntoChunks(
    const Math::Aabb& localBounds, std::uint32_t chunkCount, float totalMass,
    std::uint32_t seed, const Math::Vector3& impactPointLocal, float explosionImpulse);

/// Applies impact `impulse` at `impactPointWorld` to a `Destructible3DComponent` entity.
/// When damage reaches zero or `impulse >= impactImpulseThreshold`, fractures the entity and
/// spawns debris entities returned in `outDebrisEntities`.
[[nodiscard]] bool ApplyDestructibleImpact(IEntityManager& entityManager, Entity entity,
                                              float impulse,
                                              const Math::Vector3& impactPointWorld,
                                              std::vector<Entity>* outDebrisEntities = nullptr);

/// Integrates active `DestructibleDebrisComponent` entities under `gravity` and destroys
/// expired debris. Returns the count of surviving debris entities stepped.
std::size_t UpdateDestructibleDebris(IEntityManager& entityManager,
                                        const Math::Vector3& gravity, float deltaTimeSeconds);

} // namespace UVE::Scene
