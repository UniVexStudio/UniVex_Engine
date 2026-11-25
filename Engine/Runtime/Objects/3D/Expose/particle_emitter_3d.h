// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "entity.h"
#include "particle_emitter_component.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the ParticleEmitter3D scene object: the component set and defaults a
/// freshly created ParticleEmitter3D entity attaches.
struct ParticleEmitter3DObjectDefinition final {
    static constexpr std::string_view defaultName = "ParticleEmitter3D";

    ParticleEmitterComponent emitter{};
};

[[nodiscard]] bool IsParticleEmitter3DObjectDefinitionValid(
    const ParticleEmitter3DObjectDefinition& value) noexcept;

void ApplyParticleEmitter3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                               const ParticleEmitter3DObjectDefinition& value);

enum class ParticleSimulationBackend3D : std::uint8_t {
    CPU = 0,
    GPU = 1,
};

struct ParticleBackendTagComponent final {
    ParticleSimulationBackend3D backend = ParticleSimulationBackend3D::CPU;

    [[nodiscard]] bool operator==(const ParticleBackendTagComponent&) const = default;
};

struct GPUParticles3DObjectDefinition final {
    static constexpr std::string_view defaultName = "GPUParticles3D";
    ParticleEmitterComponent emitter{};
};

struct CPUParticles3DObjectDefinition final {
    static constexpr std::string_view defaultName = "CPUParticles3D";
    ParticleEmitterComponent emitter{};
};

void ApplyGPUParticles3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const GPUParticles3DObjectDefinition& value);
void ApplyCPUParticles3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const CPUParticles3DObjectDefinition& value);

/// Node policy for a `ParticleEmitter3D` object.
///
/// Owns the emission-rate interval, fractional spawn budgeting, and ballistic integration helpers
/// for authored `ParticleEmitterComponent` configurations.
class ParticleEmitter3D final {
public:
    ParticleEmitter3D() = delete;

    /// True when `emitter` is valid, `emitting` is true, and `emissionRate` and `maxParticles` are positive.
    [[nodiscard]] static bool IsEmitting(const ParticleEmitterComponent& emitter) noexcept;

    /// Returns `1.0F / emitter.emissionRate` when `IsEmitting(emitter)`, else `0.0F`.
    [[nodiscard]] static float SpawnIntervalSeconds(
        const ParticleEmitterComponent& emitter) noexcept;

    /// Accumulates `deltaSeconds * emitter.emissionRate` into `inOutRemainderParticles` and returns
    /// the number of new particles to spawn this step, clamped to the remaining capacity
    /// (`emitter.maxParticles - currentLiveParticles`).
    [[nodiscard]] static std::uint32_t AccumulateSpawnCount(
        const ParticleEmitterComponent& emitter, float deltaSeconds,
        float& inOutRemainderParticles, std::uint32_t currentLiveParticles) noexcept;

    /// Integrates a particle's velocity over `deltaSeconds` under world `gravity`.
    [[nodiscard]] static Math::Vector3 StepVelocity(
        const Math::Vector3& velocity, const Math::Vector3& gravity,
        float deltaSeconds) noexcept;

    /// Evaluates normalized age in `[0, 1]` for `elapsedSeconds` over `emitter.lifetimeSeconds`.
    [[nodiscard]] static float NormalizedAge(
        const ParticleEmitterComponent& emitter, float elapsedSeconds) noexcept;
};

} // namespace UVE::Scene
