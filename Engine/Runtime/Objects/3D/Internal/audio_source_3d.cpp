// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_source_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsAudioSource3DObjectDefinitionValid(const AudioSource3DObjectDefinition& value) noexcept {
    return IsAudioSourceComponentValid(value.source);
}

void ApplyAudioSource3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                           const AudioSource3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, AudioSource3DObjectDefinition::defaultName);
    entityManager.AddComponent<AudioSourceComponent>(entity, value.source);
}

bool AudioSource3D::IsPlayable(const AudioSourceComponent& source) noexcept {
    return IsAudioSourceComponentValid(source) && HasAudioSourceAssetPath(source);
}

bool AudioSource3D::ShouldAutoPlayOnAwake(const AudioSourceComponent& source) noexcept {
    return IsPlayable(source) && source.playOnAwake;
}

std::string_view AudioSource3D::ResolveEffectiveMixerGroup(
    const AudioSourceComponent& source, const std::string_view fallbackMasterGroup) noexcept {
    return source.mixerGroup.empty() ? fallbackMasterGroup : std::string_view{source.mixerGroup};
}

float AudioSource3D::EvaluateDistanceGain(
    const AudioSourceComponent& source, const float listenerDistance) noexcept {
    if (!IsAudioSourceComponentValid(source) || !std::isfinite(listenerDistance) ||
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
        case AudioAttenuationCurve::InverseSquare: {
            const float ratio = source.minDistance / std::max(listenerDistance, source.minDistance);
            const float inverseSquare = ratio * ratio;
            return std::clamp(inverseSquare * (1.0F - t), 0.0F, 1.0F);
        }
        case AudioAttenuationCurve::Linear:
        default:
            return 1.0F - t;
    }
}

float AudioSource3D::ResolveEffectiveGain(
    const AudioSourceComponent& source, const float listenerDistance) noexcept {
    if (!IsAudioSourceComponentValid(source)) {
        return 0.0F;
    }
    return source.volume * EvaluateDistanceGain(source, listenerDistance);
}

bool AudioSource3D::RequiresVoiceReplacement(
    const AudioSourceComponent& previous, const AudioSourceComponent& current) noexcept {
    return previous.audioAssetPath != current.audioAssetPath ||
           previous.looping != current.looping || previous.spatial != current.spatial ||
           previous.minDistance != current.minDistance ||
           previous.maxDistance != current.maxDistance ||
           previous.attenuationCurve != current.attenuationCurve;
}

} // namespace UVE::Scene
