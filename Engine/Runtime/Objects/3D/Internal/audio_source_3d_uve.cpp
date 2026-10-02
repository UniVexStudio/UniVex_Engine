// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/audio_source_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsAudioSource3DObjectDefinitionValidUVE(const AudioSource3DObjectDefinitionUVE& value) noexcept {
    return IsAudioSourceComponentValidUVE(value.source);
}

void ApplyAudioSource3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const AudioSource3DObjectDefinitionUVE& value) {
    // AudioSource3D is Object3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, AudioSource3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, value.source);
}

} // namespace UVE::Scene
