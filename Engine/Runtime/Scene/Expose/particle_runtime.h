// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "i_particle_runtime.h"

namespace UVE::Threading {
class IThreadPool;
} // namespace UVE::Threading

namespace UVE::Scene {

/// Bounded CPU particle state for authored emitters. This v1 owns only explicitly emitted particle
/// values and deterministic integration; GPU resources, renderer registration, asset lifetime, and
/// backend selection remain outside the runtime.
class ParticleRuntime final : public IParticleRuntime {
public:
    static constexpr std::size_t kMaximumInstances = 4096U;
    static constexpr std::uint64_t kMaximumTotalParticleBudget = 4'000'000U;
    static constexpr float kMaximumParticleLifetimeSeconds = 3600.0F;
    static constexpr float kMaximumSimulationDeltaSeconds = 0.25F;

    ParticleRuntime() = default;
    ParticleRuntime(const ParticleRuntime&) = delete;
    ParticleRuntime& operator=(const ParticleRuntime&) = delete;

    [[nodiscard]] ParticleRuntimeResult AttachDetailed(
        Entity entity, const ParticleEmitterComponent& component) override;
    [[nodiscard]] bool Attach(Entity entity, const ParticleEmitterComponent& component) {
        return AttachDetailed(entity, component).IsAccepted();
    }
    [[nodiscard]] ParticleRuntimeResult DetachDetailed(Entity entity) noexcept override;
    [[nodiscard]] bool Detach(Entity entity) noexcept {
        return DetachDetailed(entity).IsAccepted();
    }
    [[nodiscard]] ParticleRuntimeResult SetEnabledDetailed(Entity entity, bool enabled) noexcept override;
    [[nodiscard]] ParticleRuntimeResult SetLiveParticleCountDetailed(
        Entity entity, std::uint32_t liveParticles) noexcept override;
    [[nodiscard]] ParticleRuntimeResult EmitDetailed(
        Entity entity, const ParticleEmission& emission) override;
    [[nodiscard]] ParticleRuntimeResult SimulateDetailed(
        float deltaSeconds, const Math::Vector3& acceleration) noexcept override;

    /// Marks whether `entity`'s emitter may be simulated on a worker thread - its resolved Thread
    /// Group mode, supplied by the engine. Defaults to false: nothing leaves the main thread unless
    /// it was put there on purpose.
    [[nodiscard]] ParticleRuntimeResult SetWorkerEligibleDetailed(Entity entity, bool eligible) noexcept;

    /// SimulateDetailed, with worker-eligible emitters integrated on `threadPool` while the rest
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
    [[nodiscard]] ParticleRuntimeResult SimulateDetailed(float deltaSeconds,
                                                               const Math::Vector3& acceleration,
                                                               Threading::IThreadPool* threadPool);
    [[nodiscard]] ParticleRuntimeSnapshot GetSnapshot() const override;
    [[nodiscard]] std::optional<ParticleStateSnapshot> GetParticleSnapshot(Entity entity) const override;
    [[nodiscard]] bool HasInstance(Entity entity) const noexcept override;
    [[nodiscard]] std::size_t GetInstanceCount() const noexcept override;
    [[nodiscard]] std::uint64_t GetTotalBudget() const noexcept override;

private:
    struct Instance final {
        Entity entity;
        std::uint32_t maxParticles = 0U;
        std::uint32_t liveParticles = 0U;
        std::uint64_t generation = 1U;
        std::uint64_t nextSequence = 1U;
        bool enabled = true;
        bool workerEligible = false;
        std::vector<ParticleState> particles;
    };

    /// Whether one emitter's next step stays finite. Read-only, so it is safe to run for different
    /// emitters on different threads at once.
    [[nodiscard]] static bool IsStepFinite(const Instance& instance, float deltaSeconds,
                                              const Math::Vector3& acceleration) noexcept;
    /// Advances one emitter by one step and drops expired particles. Touches only that emitter's
    /// own particle vector, so different emitters may be committed on different threads at once.
    static void CommitStep(Instance& instance, float deltaSeconds, const Math::Vector3& acceleration);

    std::unordered_map<Entity, Instance> m_instances;
    std::uint64_t m_totalBudget = 0U;
};

} // namespace UVE::Scene

