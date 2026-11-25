// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vector3.h"
#include "particle_runtime.h"
#include "job_counter.h"
#include "i_thread_pool.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] ParticleRuntimeResult MakeResult(const ParticleRuntimeCode code,
                                                      std::string message) {
    return {code, std::move(message)};
}

} // namespace

ParticleRuntimeResult ParticleRuntime::AttachDetailed(
    const Entity entity, const ParticleEmitterComponent& component) {
    if (entity == kInvalidEntity) {
        return MakeResult(ParticleRuntimeCode::InvalidEntity,
                             "Particle runtime attach requires a valid generational entity.");
    }
    if (!IsParticleEmitterComponentValid(component)) {
        return MakeResult(ParticleRuntimeCode::InvalidComponent,
                             "Particle runtime attach rejected an invalid emitter budget.");
    }
    if (m_instances.find(entity) != m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::DuplicateInstance,
                             "Particle runtime attach rejected a duplicate entity instance.");
    }
    if (m_instances.size() >= kMaximumInstances) {
        return MakeResult(ParticleRuntimeCode::CapacityExceeded,
                             "Particle runtime attach rejected the instance capacity limit.");
    }
    if (component.maxParticles > kMaximumTotalParticleBudget - m_totalBudget) {
        return MakeResult(ParticleRuntimeCode::CapacityExceeded,
                             "Particle runtime attach rejected the aggregate particle budget limit.");
    }

    m_instances.emplace(entity, Instance{entity, component.maxParticles, 0U, 1U, 1U, true, false, {}});
    m_totalBudget += component.maxParticles;
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle emitter ownership attached without allocating particle storage.");
}

ParticleRuntimeResult ParticleRuntime::DetachDetailed(const Entity entity) noexcept {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::NoActiveInstance,
                             "Particle runtime detach found no active entity instance.");
    }
    m_totalBudget -= iterator->second.maxParticles;
    m_instances.erase(iterator);
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle emitter ownership detached and its budget was released.");
}

ParticleRuntimeResult ParticleRuntime::SetEnabledDetailed(const Entity entity,
                                                                    const bool enabled) noexcept {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::NoActiveInstance,
                             "Particle runtime enable update found no active entity instance.");
    }
    if (iterator->second.enabled == enabled) {
        return MakeResult(ParticleRuntimeCode::Unchanged,
                             "Particle runtime enabled state was already unchanged.");
    }
    iterator->second.enabled = enabled;
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle runtime enabled state updated without changing ownership budget.");
}

ParticleRuntimeResult ParticleRuntime::SetLiveParticleCountDetailed(
    const Entity entity, const std::uint32_t liveParticles) noexcept {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::NoActiveInstance,
                             "Particle runtime live-count update found no active entity instance.");
    }
    if (liveParticles > iterator->second.maxParticles) {
        return MakeResult(ParticleRuntimeCode::LiveParticleCountExceeded,
                             "Particle runtime live count exceeds the emitter budget.");
    }
    if (iterator->second.liveParticles == liveParticles) {
        return MakeResult(ParticleRuntimeCode::Unchanged,
                             "Particle runtime live count was already unchanged.");
    }
    iterator->second.liveParticles = liveParticles;
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle runtime live count updated within the authored budget.");
}

ParticleRuntimeResult ParticleRuntime::EmitDetailed(const Entity entity,
                                                              const ParticleEmission& emission) {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::NoActiveInstance,
                             "Particle emission found no active entity instance.");
    }
    if (!iterator->second.enabled) {
        return MakeResult(ParticleRuntimeCode::DisabledInstance,
                             "Particle emission was rejected for a disabled entity instance.");
    }
    if (emission.count == 0U) {
        return MakeResult(ParticleRuntimeCode::Unchanged,
                             "Particle emission requested zero new particles.");
    }
    if (emission.lifetimeSeconds <= 0.0F ||
        emission.lifetimeSeconds > kMaximumParticleLifetimeSeconds ||
        !std::isfinite(emission.lifetimeSeconds) || !Math::IsFinite(emission.position) ||
        !Math::IsFinite(emission.velocity)) {
        return MakeResult(ParticleRuntimeCode::InvalidSimulationInput,
                             "Particle emission requires finite position/velocity and bounded positive lifetime.");
    }
    const std::size_t currentCount = iterator->second.particles.size();
    if (static_cast<std::uint64_t>(emission.count) >
        static_cast<std::uint64_t>(iterator->second.maxParticles) - currentCount) {
        return MakeResult(ParticleRuntimeCode::LiveParticleCountExceeded,
                             "Particle emission exceeds the emitter particle budget.");
    }
    if (iterator->second.nextSequence == 0U ||
        static_cast<std::uint64_t>(emission.count) >
            std::numeric_limits<std::uint64_t>::max() - iterator->second.nextSequence + 1U) {
        return MakeResult(ParticleRuntimeCode::CapacityExceeded,
                             "Particle emission would exhaust the deterministic sequence range.");
    }

    iterator->second.particles.reserve(currentCount + emission.count);
    for (std::uint32_t index = 0U; index < emission.count; ++index) {
        const std::uint64_t sequence = iterator->second.nextSequence;
        iterator->second.nextSequence =
            sequence == std::numeric_limits<std::uint64_t>::max() ? 0U : sequence + 1U;
        iterator->second.particles.push_back(
            {emission.position, emission.velocity, emission.lifetimeSeconds, sequence});
    }
    iterator->second.liveParticles = static_cast<std::uint32_t>(iterator->second.particles.size());
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle emission appended deterministic CPU particle state.");
}

bool ParticleRuntime::IsStepFinite(const Instance& instance, const float deltaSeconds,
                                         const Math::Vector3& acceleration) noexcept {
    for (const ParticleState& particle : instance.particles) {
        const Math::Vector3 nextVelocity = particle.velocity + acceleration * deltaSeconds;
        const Math::Vector3 nextPosition = particle.position + nextVelocity * deltaSeconds;
        const float nextLifetime = particle.remainingLifetimeSeconds - deltaSeconds;
        if (!Math::IsFinite(nextVelocity) || !Math::IsFinite(nextPosition) || !std::isfinite(nextLifetime)) {
            return false;
        }
    }
    return true;
}

void ParticleRuntime::CommitStep(Instance& instance, const float deltaSeconds,
                                       const Math::Vector3& acceleration) {
    std::size_t writeIndex = 0U;
    for (const ParticleState& particle : instance.particles) {
        const Math::Vector3 nextVelocity = particle.velocity + acceleration * deltaSeconds;
        const Math::Vector3 nextPosition = particle.position + nextVelocity * deltaSeconds;
        const float nextLifetime = particle.remainingLifetimeSeconds - deltaSeconds;
        if (nextLifetime <= 0.0F) {
            continue;
        }
        instance.particles[writeIndex++] = {nextPosition, nextVelocity, nextLifetime, particle.sequence};
    }
    // Shrinking never reallocates, so this cannot throw; it is the only size change in a step.
    instance.particles.resize(writeIndex);
    instance.liveParticles = static_cast<std::uint32_t>(writeIndex);
}

ParticleRuntimeResult ParticleRuntime::SimulateDetailed(
    const float deltaSeconds, const Math::Vector3& acceleration) noexcept {
    if (deltaSeconds < 0.0F || deltaSeconds > kMaximumSimulationDeltaSeconds ||
        !std::isfinite(deltaSeconds) || !Math::IsFinite(acceleration)) {
        return MakeResult(ParticleRuntimeCode::InvalidSimulationInput,
                             "Particle simulation requires finite acceleration and bounded non-negative delta time.");
    }
    if (deltaSeconds == 0.0F) {
        return MakeResult(ParticleRuntimeCode::Unchanged,
                             "Particle simulation received a zero delta time.");
    }

    bool hasWork = false;
    for (const auto& [entity, instance] : m_instances) {
        static_cast<void>(entity);
        if (!instance.enabled || instance.particles.empty()) {
            continue;
        }
        if (!IsStepFinite(instance, deltaSeconds, acceleration)) {
            return MakeResult(ParticleRuntimeCode::NonFiniteSimulation,
                                 "Particle simulation rejected a non-finite integrated state atomically.");
        }
        hasWork = true;
    }
    if (!hasWork) {
        return MakeResult(ParticleRuntimeCode::Unchanged,
                             "Particle simulation found no enabled live particle state.");
    }

    for (auto& [entity, instance] : m_instances) {
        static_cast<void>(entity);
        if (!instance.enabled || instance.particles.empty()) {
            continue;
        }
        CommitStep(instance, deltaSeconds, acceleration);
    }
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle simulation advanced enabled CPU particle state deterministically.");
}

ParticleRuntimeResult ParticleRuntime::SetWorkerEligibleDetailed(const Entity entity,
                                                                          const bool eligible) noexcept {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return MakeResult(ParticleRuntimeCode::NoActiveInstance,
                             "Worker eligibility requires an attached particle instance.");
    }
    if (iterator->second.workerEligible == eligible) {
        return MakeResult(ParticleRuntimeCode::Unchanged, "Worker eligibility is unchanged.");
    }
    iterator->second.workerEligible = eligible;
    return MakeResult(ParticleRuntimeCode::Applied, "Worker eligibility updated.");
}

namespace {

/// Waits on `counter` when it leaves scope, however it leaves. A phase whose submission throws
/// part-way must still wait for the jobs it already handed out: they hold pointers into this
/// stack frame, and returning before they finish would leave them writing into freed memory.
class JobCounterWaitGuard final {
public:
    explicit JobCounterWaitGuard(Threading::JobCounter& counter) noexcept : m_counter(counter) {}
    JobCounterWaitGuard(const JobCounterWaitGuard&) = delete;
    JobCounterWaitGuard& operator=(const JobCounterWaitGuard&) = delete;
    ~JobCounterWaitGuard() { m_counter.Wait(); }

private:
    Threading::JobCounter& m_counter;
};

} // namespace

ParticleRuntimeResult ParticleRuntime::SimulateDetailed(const float deltaSeconds,
                                                                 const Math::Vector3& acceleration,
                                                                 Threading::IThreadPool* const threadPool) {
    // Collected on the calling thread, in the same map order the serial path walks. Reading the
    // map's structure is the only thing here that is not per-emitter, and it happens before any
    // job exists; nothing below inserts or erases, so every pointer stays valid throughout.
    std::vector<Instance*> work;
    bool anyEligible = false;
    for (auto& [entity, instance] : m_instances) {
        static_cast<void>(entity);
        if (instance.enabled && !instance.particles.empty()) {
            work.push_back(&instance);
            anyEligible = anyEligible || instance.workerEligible;
        }
    }
    if (threadPool == nullptr || threadPool->GetWorkerCount() == 0U || !anyEligible) {
        return SimulateDetailed(deltaSeconds, acceleration);
    }
    // Input validation and the empty cases are the serial path's to answer, so both overloads
    // report exactly the same codes for exactly the same inputs.
    if (deltaSeconds <= 0.0F || deltaSeconds > kMaximumSimulationDeltaSeconds ||
        !std::isfinite(deltaSeconds) || !Math::IsFinite(acceleration)) {
        return SimulateDetailed(deltaSeconds, acceleration);
    }

    // Phase 1: validate. One byte per emitter, not std::vector<bool>: packed bits would make two
    // workers writing neighbouring emitters' answers a data race on the same byte.
    std::vector<std::uint8_t> finite(work.size(), 1U);
    {
        Threading::JobCounter counter;
        const JobCounterWaitGuard waitForWorkers{counter};
        for (std::size_t index = 0; index < work.size(); ++index) {
            Instance* const instance = work[index];
            std::uint8_t* const slot = &finite[index];
            if (instance->workerEligible) {
                threadPool->Submit(
                    [instance, slot, deltaSeconds, acceleration] {
                        *slot = IsStepFinite(*instance, deltaSeconds, acceleration) ? 1U : 0U;
                    },
                    counter);
            } else {
                *slot = IsStepFinite(*instance, deltaSeconds, acceleration) ? 1U : 0U;
            }
        }
    } // The barrier: every validation, on every thread, has finished here.

    if (std::find(finite.cbegin(), finite.cend(), std::uint8_t{0U}) != finite.cend()) {
        return MakeResult(ParticleRuntimeCode::NonFiniteSimulation,
                             "Particle simulation rejected a non-finite integrated state atomically.");
    }

    // Phase 2: commit. Reached only when every emitter validated, so no emitter is written unless
    // all of them will be - the serial path's atomicity, kept across threads.
    {
        Threading::JobCounter counter;
        const JobCounterWaitGuard waitForWorkers{counter};
        for (Instance* const instance : work) {
            if (instance->workerEligible) {
                threadPool->Submit([instance, deltaSeconds, acceleration] {
                    CommitStep(*instance, deltaSeconds, acceleration);
                }, counter);
            } else {
                CommitStep(*instance, deltaSeconds, acceleration);
            }
        }
    }
    return MakeResult(ParticleRuntimeCode::Applied,
                         "Particle simulation advanced enabled CPU particle state deterministically.");
}

ParticleRuntimeSnapshot ParticleRuntime::GetSnapshot() const {
    ParticleRuntimeSnapshot snapshot;
    snapshot.instanceCount = m_instances.size();
    snapshot.totalBudget = m_totalBudget;
    snapshot.instances.reserve(m_instances.size());
    for (const auto& [entity, instance] : m_instances) {
        snapshot.instances.push_back(
            {entity, instance.maxParticles, instance.liveParticles, instance.generation, instance.enabled,
             instance.workerEligible});
    }
    std::sort(snapshot.instances.begin(), snapshot.instances.end(),
              [](const ParticleRuntimeInstanceSnapshot& lhs, const ParticleRuntimeInstanceSnapshot& rhs) {
                  if (lhs.entity.index != rhs.entity.index) {
                      return lhs.entity.index < rhs.entity.index;
                  }
                  return lhs.entity.generation < rhs.entity.generation;
              });
    return snapshot;
}

std::optional<ParticleStateSnapshot> ParticleRuntime::GetParticleSnapshot(
    const Entity entity) const {
    const auto iterator = m_instances.find(entity);
    if (iterator == m_instances.end()) {
        return std::nullopt;
    }
    return ParticleStateSnapshot{entity, iterator->second.generation, iterator->second.particles};
}

bool ParticleRuntime::HasInstance(const Entity entity) const noexcept {
    return m_instances.find(entity) != m_instances.end();
}

std::size_t ParticleRuntime::GetInstanceCount() const noexcept {
    return m_instances.size();
}

std::uint64_t ParticleRuntime::GetTotalBudget() const noexcept {
    return m_totalBudget;
}

} // namespace UVE::Scene

