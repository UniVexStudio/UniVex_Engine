// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/audio_source_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsAudioSource3DNodeDefinitionValidUVE(const AudioSource3DNodeDefinitionUVE& value) noexcept {
    return IsAudioSourceComponentValidUVE(value.source);
}

void ApplyAudioSource3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const AudioSource3DNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, value.source);
}

} // namespace UVE::Scene
