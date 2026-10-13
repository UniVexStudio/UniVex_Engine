// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/audio_source_system_uve.h"

#include <unordered_map>
#include <unordered_set>

#include "uve/logging/logging_macros_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Audio {

namespace {

/// The Audio-namespaced counterpart of a Scene-namespaced AudioSourceComponentUVE's attenuation
/// curve field — mirrors Physics::MaterialOfUVE()'s "component holds plain data, system converts
/// on demand" precedent, keeping engine/scene free of any engine/audio dependency.
[[nodiscard]] AudioAttenuationModelUVE AttenuationModelOfUVE(Scene::AudioAttenuationCurveUVE curve) noexcept {
    switch (curve) {
        case Scene::AudioAttenuationCurveUVE::InverseSquare:
            return AudioAttenuationModelUVE::InverseSquare;
        case Scene::AudioAttenuationCurveUVE::Linear:
        default:
            return AudioAttenuationModelUVE::Linear;
    }
}

[[nodiscard]] AudioSourceDescUVE MakeAudioSourceDescUVE(
    const Scene::AudioSourceComponentUVE& audioSource) {
    AudioSourceDescUVE desc;
    desc.audioAssetPath = audioSource.audioAssetPath;
    desc.mixerGroup = audioSource.mixerGroup.empty() ? std::string(kMasterAudioMixerGroupNameUVE)
                                                      : audioSource.mixerGroup;
    desc.looping = audioSource.looping;
    desc.volume = audioSource.volume;
    desc.pitch = audioSource.pitch;
    desc.spatial = audioSource.spatial;
    desc.minDistance = audioSource.minDistance;
    desc.maxDistance = audioSource.maxDistance;
    desc.attenuationModel = AttenuationModelOfUVE(audioSource.attenuationCurve);
    return desc;
}

[[nodiscard]] bool RequiresVoiceReplacementUVE(const AudioSourceDescUVE& previous,
                                                const AudioSourceDescUVE& current) noexcept {
    return previous.audioAssetPath != current.audioAssetPath || previous.looping != current.looping ||
           previous.spatial != current.spatial || previous.minDistance != current.minDistance ||
           previous.maxDistance != current.maxDistance || previous.attenuationModel != current.attenuationModel;
}

} // namespace

struct AudioSourceSystemUVE::ImplUVE {
    struct SourceStateUVE final {
        VoiceHandleUVE voice;
        AudioSourceDescUVE descriptor;
    };

    std::unordered_map<Scene::EntityUVE, SourceStateUVE> entityToVoice;
};

AudioSourceSystemUVE::AudioSourceSystemUVE() : m_impl(std::make_unique<ImplUVE>()) {}

AudioSourceSystemUVE::~AudioSourceSystemUVE() = default;

AudioSourceSystemUVE::EnsuredVoiceUVE AudioSourceSystemUVE::EnsureVoiceUVE(
    Scene::EntityUVE entity, const Scene::AudioSourceComponentUVE& source, IAudioSystemUVE& audioSystem) {
    if (const auto existing = m_impl->entityToVoice.find(entity); existing != m_impl->entityToVoice.end()) {
        return {existing->second.voice, false};
    }
    if (!Scene::IsAudioSourceComponentValidUVE(source)) {
        return {kInvalidVoiceHandleUVE, false};
    }
    const AudioSourceDescUVE descriptor = MakeAudioSourceDescUVE(source);
    const VoiceHandleUVE voice = audioSystem.CreateSourceUVE(descriptor);
    if (voice == kInvalidVoiceHandleUVE) {
        return {voice, false};
    }
    m_impl->entityToVoice.emplace(entity, ImplUVE::SourceStateUVE{voice, descriptor});
    return {voice, true};
}

bool AudioSourceSystemUVE::PlayEntityUVE(Scene::EntityUVE entity, Scene::IEntityManagerUVE& entityManager,
                                         IAudioSystemUVE& audioSystem) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity)) {
        return false;
    }
    const Scene::AudioSourceComponentUVE& source =
        entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(entity);
    const EnsuredVoiceUVE ensured = EnsureVoiceUVE(entity, source, audioSystem);
    if (ensured.voice == kInvalidVoiceHandleUVE) {
        return false;
    }
    if (ensured.created) {
        const Scene::WorldTransformComponentUVE& worldTransform =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        audioSystem.SetSourcePositionUVE(ensured.voice, worldTransform.worldPosition);
        audioSystem.SetSourceVolumeUVE(ensured.voice, source.volume);
        audioSystem.SetSourcePitchUVE(ensured.voice, source.pitch);
    }
    return audioSystem.PlayUVE(ensured.voice);
}

bool AudioSourceSystemUVE::StopEntityUVE(Scene::EntityUVE entity, IAudioSystemUVE& audioSystem) {
    const auto iterator = m_impl->entityToVoice.find(entity);
    if (iterator == m_impl->entityToVoice.end()) {
        return false;
    }
    return audioSystem.StopUVE(iterator->second.voice);
}

bool AudioSourceSystemUVE::IsEntityPlayingUVE(Scene::EntityUVE entity,
                                              const IAudioSystemUVE& audioSystem) const {
    const auto iterator = m_impl->entityToVoice.find(entity);
    if (iterator == m_impl->entityToVoice.end()) {
        return false;
    }
    return audioSystem.GetSourceStateUVE(iterator->second.voice) == VoicePlaybackStateUVE::Playing;
}

void AudioSourceSystemUVE::SyncUVE(Scene::IEntityManagerUVE& entityManager, IAudioSystemUVE& audioSystem) {
    std::unordered_set<Scene::EntityUVE> seen;

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::AudioSourceComponentUVE>(
        [&](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::AudioSourceComponentUVE& audioSource) {
            seen.insert(entity);
            if (!Scene::IsAudioSourceComponentValidUVE(audioSource)) {
                UVE_ERROR("AudioSourceSystemUVE: ignoring invalid authored audio source on entity ({}, {})",
                          entity.index, entity.generation);
                return;
            }

            const AudioSourceDescUVE desiredDesc = MakeAudioSourceDescUVE(audioSource);
            auto iterator = m_impl->entityToVoice.find(entity);
            if (iterator == m_impl->entityToVoice.end()) {
                const EnsuredVoiceUVE ensured = EnsureVoiceUVE(entity, audioSource, audioSystem);
                if (ensured.voice == kInvalidVoiceHandleUVE) {
                    return;
                }
                iterator = m_impl->entityToVoice.find(entity);
                if (audioSource.playOnAwake) {
                    static_cast<void>(audioSystem.PlayUVE(ensured.voice));
                }
            } else {
                ImplUVE::SourceStateUVE& state = iterator->second;
                if (RequiresVoiceReplacementUVE(state.descriptor, desiredDesc)) {
                    const VoicePlaybackStateUVE previousPlaybackState = audioSystem.GetSourceStateUVE(state.voice);
                    const VoiceHandleUVE replacement = audioSystem.CreateSourceUVE(desiredDesc);
                    if (replacement != kInvalidVoiceHandleUVE) {
                        const bool wasPlaying = previousPlaybackState == VoicePlaybackStateUVE::Playing;
                        if (!wasPlaying || audioSystem.PlayUVE(replacement)) {
                            const VoiceHandleUVE previousVoice = state.voice;
                            state.voice = replacement;
                            state.descriptor = desiredDesc;
                            audioSystem.DestroySourceUVE(previousVoice);
                        } else {
                            audioSystem.DestroySourceUVE(replacement);
                        }
                    }
                }
            }

            if (iterator->second.descriptor.mixerGroup != desiredDesc.mixerGroup &&
                audioSystem.SetSourceMixerGroupUVE(iterator->second.voice, desiredDesc.mixerGroup)) {
                iterator->second.descriptor.mixerGroup = desiredDesc.mixerGroup;
            }
            audioSystem.SetSourcePositionUVE(iterator->second.voice, worldTransform.worldPosition);
            audioSystem.SetSourceVolumeUVE(iterator->second.voice, audioSource.volume);
            audioSystem.SetSourcePitchUVE(iterator->second.voice, audioSource.pitch);
        });

    for (auto iterator = m_impl->entityToVoice.begin(); iterator != m_impl->entityToVoice.end();) {
        if (!seen.contains(iterator->first)) {
            audioSystem.DestroySourceUVE(iterator->second.voice);
            iterator = m_impl->entityToVoice.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

} // namespace UVE::Audio
