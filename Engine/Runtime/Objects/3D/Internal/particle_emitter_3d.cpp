// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_emitter_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsParticleEmitter3DObjectDefinitionValid(const ParticleEmitter3DObjectDefinition& value) noexcept {
    return IsParticleEmitterComponentValid(value.emitter);
}

void ApplyParticleEmitter3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                               const ParticleEmitter3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, ParticleEmitter3DObjectDefinition::defaultName);
    entityManager.AddComponent<ParticleEmitterComponent>(entity, value.emitter);
}

void ApplyGPUParticles3DObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const GPUParticles3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity,
                                  GPUParticles3DObjectDefinition::defaultName);
    entityManager.AddComponent<ParticleEmitterComponent>(entity, value.emitter);
    entityManager.AddComponent<ParticleBackendTagComponent>(
        entity, ParticleBackendTagComponent{ParticleSimulationBackend3D::GPU});
}

void ApplyCPUParticles3DObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const CPUParticles3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity,
                                  CPUParticles3DObjectDefinition::defaultName);
    entityManager.AddComponent<ParticleEmitterComponent>(entity, value.emitter);
    entityManager.AddComponent<ParticleBackendTagComponent>(
        entity, ParticleBackendTagComponent{ParticleSimulationBackend3D::CPU});
}

bool ParticleEmitter3D::IsEmitting(const ParticleEmitterComponent& emitter) noexcept {
    return IsParticleEmitterComponentValid(emitter) && emitter.emitting &&
           emitter.emissionRate > 0.0F && emitter.maxParticles > 0U;
}

float ParticleEmitter3D::SpawnIntervalSeconds(
    const ParticleEmitterComponent& emitter) noexcept {
    if (!IsEmitting(emitter)) {
        return 0.0F;
    }
    return 1.0F / emitter.emissionRate;
}

std::uint32_t ParticleEmitter3D::AccumulateSpawnCount(
    const ParticleEmitterComponent& emitter, const float deltaSeconds,
    float& inOutRemainderParticles, const std::uint32_t currentLiveParticles) noexcept {
    if (!IsEmitting(emitter)) {
        return 0U;
    }
    return ConsumeParticleEmitterAutoEmitCount(
        inOutRemainderParticles, emitter, deltaSeconds, currentLiveParticles);
}

Math::Vector3 ParticleEmitter3D::StepVelocity(
    const Math::Vector3& velocity, const Math::Vector3& gravity,
    const float deltaSeconds) noexcept {
    if (!Math::IsFinite(velocity) || !Math::IsFinite(gravity) ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return velocity;
    }
    return Math::Vector3{velocity.x + gravity.x * deltaSeconds,
                            velocity.y + gravity.y * deltaSeconds,
                            velocity.z + gravity.z * deltaSeconds};
}

float ParticleEmitter3D::NormalizedAge(
    const ParticleEmitterComponent& emitter, const float elapsedSeconds) noexcept {
    if (!IsParticleEmitterComponentValid(emitter) || !std::isfinite(elapsedSeconds) ||
        elapsedSeconds <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(elapsedSeconds / emitter.lifetimeSeconds, 0.0F, 1.0F);
}

} // namespace UVE::Scene
