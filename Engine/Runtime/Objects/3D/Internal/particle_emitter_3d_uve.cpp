// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/particle_emitter_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsParticleEmitter3DObjectDefinitionValidUVE(const ParticleEmitter3DObjectDefinitionUVE& value) noexcept {
    return IsParticleEmitterComponentValidUVE(value.emitter);
}

void ApplyParticleEmitter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const ParticleEmitter3DObjectDefinitionUVE& value) {
    // A SurfaceInstance3D: that base (RenderInstance3D, Object3D, the Object section) first, then
    // this kind's own part.
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, ParticleEmitter3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, value.emitter);
}

} // namespace UVE::Scene
