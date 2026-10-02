// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/particle_emitter_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

namespace UVE::Scene {

bool IsParticleEmitter3DNodeDefinitionValidUVE(const ParticleEmitter3DNodeDefinitionUVE& value) noexcept {
    return IsParticleEmitterComponentValidUVE(value.emitter);
}

void ApplyParticleEmitter3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const ParticleEmitter3DNodeDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Node3D, the Node section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, ParticleEmitter3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
}

} // namespace UVE::Scene
