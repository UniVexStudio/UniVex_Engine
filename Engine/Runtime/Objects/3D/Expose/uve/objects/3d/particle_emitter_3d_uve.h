// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the ParticleEmitter3D scene object: the component set and defaults a
/// freshly created ParticleEmitter3D entity attaches.
struct ParticleEmitter3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "ParticleEmitter3D";

    ParticleEmitterComponentUVE emitter{};
};

[[nodiscard]] bool IsParticleEmitter3DObjectDefinitionValidUVE(
    const ParticleEmitter3DObjectDefinitionUVE& value) noexcept;

void ApplyParticleEmitter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const ParticleEmitter3DObjectDefinitionUVE& value);

enum class ParticleSimulationBackend3DUVE : std::uint8_t {
    CPU = 0,
    GPU = 1,
};

struct ParticleBackendTagComponentUVE final {
    ParticleSimulationBackend3DUVE backend = ParticleSimulationBackend3DUVE::CPU;

    [[nodiscard]] bool operator==(const ParticleBackendTagComponentUVE&) const = default;
};

struct GPUParticles3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "GPUParticles3D";
    ParticleEmitterComponentUVE emitter{};
};

struct CPUParticles3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CPUParticles3D";
    ParticleEmitterComponentUVE emitter{};
};

void ApplyGPUParticles3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const GPUParticles3DObjectDefinitionUVE& value);
void ApplyCPUParticles3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const CPUParticles3DObjectDefinitionUVE& value);

/// Node policy for a `ParticleEmitter3D` object.
///
/// Owns the emission-rate interval, fractional spawn budgeting, and ballistic integration helpers
/// for authored `ParticleEmitterComponentUVE` configurations.
class ParticleEmitter3DUVE final {
public:
    ParticleEmitter3DUVE() = delete;

    /// True when `emitter` is valid, `emitting` is true, and `emissionRate` and `maxParticles` are positive.
    [[nodiscard]] static bool IsEmittingUVE(const ParticleEmitterComponentUVE& emitter) noexcept;

    /// Returns `1.0F / emitter.emissionRate` when `IsEmittingUVE(emitter)`, else `0.0F`.
    [[nodiscard]] static float SpawnIntervalSecondsUVE(
        const ParticleEmitterComponentUVE& emitter) noexcept;

    /// Accumulates `deltaSeconds * emitter.emissionRate` into `inOutRemainderParticles` and returns
    /// the number of new particles to spawn this step, clamped to the remaining capacity
    /// (`emitter.maxParticles - currentLiveParticles`).
    [[nodiscard]] static std::uint32_t AccumulateSpawnCountUVE(
        const ParticleEmitterComponentUVE& emitter, float deltaSeconds,
        float& inOutRemainderParticles, std::uint32_t currentLiveParticles) noexcept;

    /// Integrates a particle's velocity over `deltaSeconds` under world `gravity`.
    [[nodiscard]] static Math::Vector3UVE StepVelocityUVE(
        const Math::Vector3UVE& velocity, const Math::Vector3UVE& gravity,
        float deltaSeconds) noexcept;

    /// Evaluates normalized age in `[0, 1]` for `elapsedSeconds` over `emitter.lifetimeSeconds`.
    [[nodiscard]] static float NormalizedAgeUVE(
        const ParticleEmitterComponentUVE& emitter, float elapsedSeconds) noexcept;
};

} // namespace UVE::Scene
