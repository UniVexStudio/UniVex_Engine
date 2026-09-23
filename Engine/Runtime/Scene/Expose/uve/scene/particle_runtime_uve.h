// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "uve/scene/i_particle_runtime_uve.h"

namespace UVE::Threading {
class IThreadPoolUVE;
} // namespace UVE::Threading

namespace UVE::Scene {

/// Bounded CPU particle state for authored emitters. This v1 owns only explicitly emitted particle
/// values and deterministic integration; GPU resources, renderer registration, asset lifetime, and
/// backend selection remain outside the runtime.
class ParticleRuntimeUVE final : public IParticleRuntimeUVE {
public:
    static constexpr std::size_t kMaximumInstancesUVE = 4096U;
    static constexpr std::uint64_t kMaximumTotalParticleBudgetUVE = 4'000'000U;
    static constexpr float kMaximumParticleLifetimeSecondsUVE = 3600.0F;
    static constexpr float kMaximumSimulationDeltaSecondsUVE = 0.25F;

    ParticleRuntimeUVE() = default;
    ParticleRuntimeUVE(const ParticleRuntimeUVE&) = delete;
    ParticleRuntimeUVE& operator=(const ParticleRuntimeUVE&) = delete;

    [[nodiscard]] ParticleRuntimeResultUVE AttachDetailedUVE(
        EntityUVE entity, const ParticleEmitterComponentUVE& component) override;
    [[nodiscard]] bool AttachUVE(EntityUVE entity, const ParticleEmitterComponentUVE& component) {
        return AttachDetailedUVE(entity, component).IsAcceptedUVE();
    }
    [[nodiscard]] ParticleRuntimeResultUVE DetachDetailedUVE(EntityUVE entity) noexcept override;
    [[nodiscard]] bool DetachUVE(EntityUVE entity) noexcept {
        return DetachDetailedUVE(entity).IsAcceptedUVE();
    }
    [[nodiscard]] ParticleRuntimeResultUVE SetEnabledDetailedUVE(EntityUVE entity, bool enabled) noexcept override;
    [[nodiscard]] ParticleRuntimeResultUVE SetLiveParticleCountDetailedUVE(
        EntityUVE entity, std::uint32_t liveParticles) noexcept override;
    [[nodiscard]] ParticleRuntimeResultUVE EmitDetailedUVE(
        EntityUVE entity, const ParticleEmissionUVE& emission) override;
    [[nodiscard]] ParticleRuntimeResultUVE SimulateDetailedUVE(
        float deltaSeconds, const Math::Vector3UVE& acceleration) noexcept override;

    /// Marks whether `entity`'s emitter may be simulated on a worker thread - its resolved Thread
    /// Group mode, supplied by the engine. Defaults to false: nothing leaves the main thread unless
    /// it was put there on purpose.
    [[nodiscard]] ParticleRuntimeResultUVE SetWorkerEligibleDetailedUVE(EntityUVE entity, bool eligible) noexcept;

    /// SimulateDetailedUVE, with worker-eligible emitters integrated on `threadPool` while the rest
    /// stay on the calling thread.
    ///
    /// SAME CONTRACT AS THE SERIAL PATH, INCLUDING ATOMICITY. Simulation is all-or-nothing across
    /// every emitter: if any emitter would integrate to a non-finite state, no emitter is written.
    /// The parallel version keeps that by splitting into two phases with a barrier between them -
    /// every emitter is validated (in parallel where eligible) before any emitter is written - so a
    /// failure found on a worker still leaves the whole runtime untouched.
    ///
    /// Both phases call the same per-emitter functions the serial path calls, so the arithmetic is
    /// the same code in the same order and the result is bit-identical to a serial simulate.
    ///
    /// A null pool, a pool with no workers, or no worker-eligible emitter takes the serial path.
    /// Not noexcept, unlike the serial overload: handing work to a pool allocates.
    [[nodiscard]] ParticleRuntimeResultUVE SimulateDetailedUVE(float deltaSeconds,
                                                               const Math::Vector3UVE& acceleration,
                                                               Threading::IThreadPoolUVE* threadPool);
    [[nodiscard]] ParticleRuntimeSnapshotUVE GetSnapshotUVE() const override;
    [[nodiscard]] std::optional<ParticleStateSnapshotUVE> GetParticleSnapshotUVE(EntityUVE entity) const override;
    [[nodiscard]] bool HasInstanceUVE(EntityUVE entity) const noexcept override;
    [[nodiscard]] std::size_t GetInstanceCountUVE() const noexcept override;
    [[nodiscard]] std::uint64_t GetTotalBudgetUVE() const noexcept override;

private:
    struct InstanceUVE final {
        EntityUVE entity;
        std::uint32_t maxParticles = 0U;
        std::uint32_t liveParticles = 0U;
        std::uint64_t generation = 1U;
        std::uint64_t nextSequence = 1U;
        bool enabled = true;
        bool workerEligible = false;
        std::vector<ParticleStateUVE> particles;
    };

    /// Whether one emitter's next step stays finite. Read-only, so it is safe to run for different
    /// emitters on different threads at once.
    [[nodiscard]] static bool IsStepFiniteUVE(const InstanceUVE& instance, float deltaSeconds,
                                              const Math::Vector3UVE& acceleration) noexcept;
    /// Advances one emitter by one step and drops expired particles. Touches only that emitter's
    /// own particle vector, so different emitters may be committed on different threads at once.
    static void CommitStepUVE(InstanceUVE& instance, float deltaSeconds, const Math::Vector3UVE& acceleration);

    std::unordered_map<EntityUVE, InstanceUVE> m_instances;
    std::uint64_t m_totalBudget = 0U;
};

} // namespace UVE::Scene

