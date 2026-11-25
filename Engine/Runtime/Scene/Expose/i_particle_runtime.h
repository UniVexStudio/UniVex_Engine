#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vector3.h"
#include "particle_emitter_component.h"
#include "entity.h"

namespace UVE::Scene {

enum class ParticleRuntimeCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    InvalidEntity,
    InvalidComponent,
    DuplicateInstance,
    NoActiveInstance,
    CapacityExceeded,
    LiveParticleCountExceeded,
    DisabledInstance,
    InvalidSimulationInput,
    NonFiniteSimulation,
};

struct ParticleRuntimeResult final {
    ParticleRuntimeCode code = ParticleRuntimeCode::InvalidComponent;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == ParticleRuntimeCode::Applied || code == ParticleRuntimeCode::Unchanged;
    }
};

struct ParticleEmission final {
    std::uint32_t count = 0U;
    Math::Vector3 position{};
    Math::Vector3 velocity{};
    float lifetimeSeconds = 0.0F;
};

struct ParticleState final {
    Math::Vector3 position{};
    Math::Vector3 velocity{};
    float remainingLifetimeSeconds = 0.0F;
    std::uint64_t sequence = 0U;

    [[nodiscard]] bool operator==(const ParticleState&) const = default;
};

struct ParticleRuntimeInstanceSnapshot final {
    Entity entity;
    std::uint32_t maxParticles = 0U;
    std::uint32_t liveParticles = 0U;
    std::uint64_t generation = 0U;
    bool enabled = false;
    /// Whether this emitter is simulated on a worker thread when a thread pool is supplied - its
    /// resolved Thread Group mode, as the engine last set it.
    bool workerEligible = false;

    [[nodiscard]] bool operator==(const ParticleRuntimeInstanceSnapshot&) const = default;
};

struct ParticleRuntimeSnapshot final {
    std::size_t instanceCount = 0U;
    std::uint64_t totalBudget = 0U;
    std::vector<ParticleRuntimeInstanceSnapshot> instances;

    [[nodiscard]] bool operator==(const ParticleRuntimeSnapshot&) const = default;
};

struct ParticleStateSnapshot final {
    Entity entity;
    std::uint64_t generation = 0U;
    std::vector<ParticleState> particles;

    [[nodiscard]] bool operator==(const ParticleStateSnapshot&) const = default;
};

/// IParticleRuntime is the gameplay/content-facing boundary for bounded CPU particle state.
/// Implementations own only explicitly emitted particle values and deterministic integration;
/// GPU resources, renderer registration, asset lifetime, and backend selection remain outside.
/// Thread-safety: not thread-safe; callers use it from the scene/runtime thread.
class IParticleRuntime {
public:
    virtual ~IParticleRuntime() = default;

    [[nodiscard]] virtual ParticleRuntimeResult AttachDetailed(
        Entity entity, const ParticleEmitterComponent& component) = 0;
    [[nodiscard]] bool Attach(Entity entity, const ParticleEmitterComponent& component) {
        return AttachDetailed(entity, component).IsAccepted();
    }

    [[nodiscard]] virtual ParticleRuntimeResult DetachDetailed(Entity entity) noexcept = 0;
    [[nodiscard]] bool Detach(Entity entity) noexcept {
        return DetachDetailed(entity).IsAccepted();
    }

    [[nodiscard]] virtual ParticleRuntimeResult SetEnabledDetailed(
        Entity entity, bool enabled) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult SetLiveParticleCountDetailed(
        Entity entity, std::uint32_t liveParticles) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult EmitDetailed(
        Entity entity, const ParticleEmission& emission) = 0;
    [[nodiscard]] virtual ParticleRuntimeResult SimulateDetailed(
        float deltaSeconds, const Math::Vector3& acceleration) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeSnapshot GetSnapshot() const = 0;
    [[nodiscard]] virtual std::optional<ParticleStateSnapshot> GetParticleSnapshot(
        Entity entity) const = 0;
    [[nodiscard]] virtual bool HasInstance(Entity entity) const noexcept = 0;
    [[nodiscard]] virtual std::size_t GetInstanceCount() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t GetTotalBudget() const noexcept = 0;
};

} // namespace UVE::Scene
