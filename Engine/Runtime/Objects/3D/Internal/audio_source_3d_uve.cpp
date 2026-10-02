// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/audio_source_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsAudioSource3DNodeDefinitionValidUVE(const AudioSource3DNodeDefinitionUVE& value) noexcept {
    return IsAudioSourceComponentValidUVE(value.source);
}

void ApplyAudioSource3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const AudioSource3DNodeDefinitionUVE& value) {
    // AudioSource3D is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, AudioSource3DNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, value.source);
}

} // namespace UVE::Scene
