// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/particle_emitter_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsParticleEmitter3DObjectDefinitionValidUVE(const ParticleEmitter3DObjectDefinitionUVE& value) noexcept {
    return IsParticleEmitterComponentValidUVE(value.emitter);
}

void ApplyParticleEmitter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                               const ParticleEmitter3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, ParticleEmitter3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
}

void ApplyGPUParticles3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const GPUParticles3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity,
                                  GPUParticles3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
    entityManager.AddComponentUVE<ParticleBackendTagComponentUVE>(
        entity, ParticleBackendTagComponentUVE{ParticleSimulationBackend3DUVE::GPU});
}

void ApplyCPUParticles3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const CPUParticles3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity,
                                  CPUParticles3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
    entityManager.AddComponentUVE<ParticleBackendTagComponentUVE>(
        entity, ParticleBackendTagComponentUVE{ParticleSimulationBackend3DUVE::CPU});
}

bool ParticleEmitter3DUVE::IsEmittingUVE(const ParticleEmitterComponentUVE& emitter) noexcept {
    return IsParticleEmitterComponentValidUVE(emitter) && emitter.emitting &&
           emitter.emissionRate > 0.0F && emitter.maxParticles > 0U;
}

float ParticleEmitter3DUVE::SpawnIntervalSecondsUVE(
    const ParticleEmitterComponentUVE& emitter) noexcept {
    if (!IsEmittingUVE(emitter)) {
        return 0.0F;
    }
    return 1.0F / emitter.emissionRate;
}

std::uint32_t ParticleEmitter3DUVE::AccumulateSpawnCountUVE(
    const ParticleEmitterComponentUVE& emitter, const float deltaSeconds,
    float& inOutRemainderParticles, const std::uint32_t currentLiveParticles) noexcept {
    if (!IsEmittingUVE(emitter)) {
        return 0U;
    }
    return ConsumeParticleEmitterAutoEmitCountUVE(
        inOutRemainderParticles, emitter, deltaSeconds, currentLiveParticles);
}

Math::Vector3UVE ParticleEmitter3DUVE::StepVelocityUVE(
    const Math::Vector3UVE& velocity, const Math::Vector3UVE& gravity,
    const float deltaSeconds) noexcept {
    if (!Math::IsFiniteUVE(velocity) || !Math::IsFiniteUVE(gravity) ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return velocity;
    }
    return Math::Vector3UVE{velocity.x + gravity.x * deltaSeconds,
                            velocity.y + gravity.y * deltaSeconds,
                            velocity.z + gravity.z * deltaSeconds};
}

float ParticleEmitter3DUVE::NormalizedAgeUVE(
    const ParticleEmitterComponentUVE& emitter, const float elapsedSeconds) noexcept {
    if (!IsParticleEmitterComponentValidUVE(emitter) || !std::isfinite(elapsedSeconds) ||
        elapsedSeconds <= 0.0F) {
        return 0.0F;
    }
    return std::clamp(elapsedSeconds / emitter.lifetimeSeconds, 0.0F, 1.0F);
}

} // namespace UVE::Scene
