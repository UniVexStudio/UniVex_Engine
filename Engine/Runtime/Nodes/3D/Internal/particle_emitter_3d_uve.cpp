// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/particle_emitter_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsParticleEmitter3DNodeDefinitionValidUVE(const ParticleEmitter3DNodeDefinitionUVE& value) noexcept {
    return IsParticleEmitterComponentValidUVE(value.emitter);
}

void ApplyParticleEmitter3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const ParticleEmitter3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
}

} // namespace UVE::Scene
