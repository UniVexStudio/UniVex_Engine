// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/audio_source_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsAudioSource3DObjectDefinitionValidUVE(const AudioSource3DObjectDefinitionUVE& value) noexcept {
    return IsAudioSourceComponentValidUVE(value.source);
}

void ApplyAudioSource3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const AudioSource3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, AudioSource3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, value.source);
}

bool AudioSource3DUVE::IsPlayableUVE(const AudioSourceComponentUVE& source) noexcept {
    return IsAudioSourceComponentValidUVE(source) && HasAudioSourceAssetPathUVE(source);
}

bool AudioSource3DUVE::ShouldAutoPlayOnAwakeUVE(const AudioSourceComponentUVE& source) noexcept {
    return IsPlayableUVE(source) && source.playOnAwake;
}

std::string_view AudioSource3DUVE::ResolveEffectiveMixerGroupUVE(
    const AudioSourceComponentUVE& source, const std::string_view fallbackMasterGroup) noexcept {
    return source.mixerGroup.empty() ? fallbackMasterGroup : std::string_view{source.mixerGroup};
}

float AudioSource3DUVE::EvaluateDistanceGainUVE(
    const AudioSourceComponentUVE& source, const float listenerDistance) noexcept {
    if (!IsAudioSourceComponentValidUVE(source) || !std::isfinite(listenerDistance) ||
        listenerDistance < 0.0F) {
        return 0.0F;
    }
    if (!source.spatial || listenerDistance <= source.minDistance) {
        return 1.0F;
    }
    if (listenerDistance >= source.maxDistance) {
        return 0.0F;
    }
    const float span = source.maxDistance - source.minDistance;
    if (span <= 0.0F) {
        return 0.0F;
    }
    const float t = std::clamp((listenerDistance - source.minDistance) / span, 0.0F, 1.0F);
    switch (source.attenuationCurve) {
        case AudioAttenuationCurveUVE::InverseSquare: {
            const float ratio = source.minDistance / std::max(listenerDistance, source.minDistance);
            const float inverseSquare = ratio * ratio;
            return std::clamp(inverseSquare * (1.0F - t), 0.0F, 1.0F);
        }
        case AudioAttenuationCurveUVE::Linear:
        default:
            return 1.0F - t;
    }
}

float AudioSource3DUVE::ResolveEffectiveGainUVE(
    const AudioSourceComponentUVE& source, const float listenerDistance) noexcept {
    if (!IsAudioSourceComponentValidUVE(source)) {
        return 0.0F;
    }
    return source.volume * EvaluateDistanceGainUVE(source, listenerDistance);
}

bool AudioSource3DUVE::RequiresVoiceReplacementUVE(
    const AudioSourceComponentUVE& previous, const AudioSourceComponentUVE& current) noexcept {
    return previous.audioAssetPath != current.audioAssetPath ||
           previous.looping != current.looping || previous.spatial != current.spatial ||
           previous.minDistance != current.minDistance ||
           previous.maxDistance != current.maxDistance ||
           previous.attenuationCurve != current.attenuationCurve;
}

} // namespace UVE::Scene
