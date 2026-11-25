// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_source_system.h"

#include <unordered_map>
#include <unordered_set>

#include "logging_macros.h"
#include "audio_source_component.h"
#include "world_transform_component.h"
#include "audio_source_3d.h"

namespace UVE::Audio {

namespace {

/// The Audio-namespaced counterpart of a Scene-namespaced AudioSourceComponent's attenuation
/// curve field — mirrors Physics::MaterialOf()'s "component holds plain data, system converts
/// on demand" precedent, keeping engine/scene free of any engine/audio dependency.
[[nodiscard]] AudioAttenuationModel AttenuationModelOf(Scene::AudioAttenuationCurve curve) noexcept {
    switch (curve) {
        case Scene::AudioAttenuationCurve::InverseSquare:
            return AudioAttenuationModel::InverseSquare;
        case Scene::AudioAttenuationCurve::Linear:
        default:
            return AudioAttenuationModel::Linear;
    }
}

[[nodiscard]] AudioSourceDesc MakeAudioSourceDesc(
    const Scene::Entity entity, const Scene::AudioSourceComponent& audioSource) {
    AudioSourceDesc desc;
    // Carried so the occlusion provider can exclude this emitter's own collider: a radio with a
    // collider must not be blocked by its own geometry.
    desc.ownerEntity = entity;
    desc.audioAssetPath = audioSource.audioAssetPath;
    desc.mixerGroup = std::string{
        Scene::AudioSource3D::ResolveEffectiveMixerGroup(audioSource, kMasterAudioMixerGroupName)};
    desc.looping = audioSource.looping;
    desc.volume = audioSource.volume;
    desc.pitch = audioSource.pitch;
    desc.spatial = audioSource.spatial;
    desc.minDistance = audioSource.minDistance;
    desc.maxDistance = audioSource.maxDistance;
    desc.attenuationModel = AttenuationModelOf(audioSource.attenuationCurve);
    desc.occlusionEnabled = audioSource.occlusionEnabled;
    desc.occlusionGain = audioSource.occlusionGain;
    desc.occlusionLowPassHz = audioSource.occlusionLowPassHz;
    desc.occlusionLayerMask = audioSource.occlusionLayerMask;
    return desc;
}

[[nodiscard]] bool RequiresVoiceReplacement(const AudioSourceDesc& previous,
                                                const AudioSourceDesc& current) noexcept {
    // Everything the mixer bakes into a voice's descriptor at creation goes here. Occlusion settings
    // are in the list for the same reason min/max distance are: they shape how the source is heard,
    // and a runtime edit must reach the device rather than live only in the component.
    return previous.audioAssetPath != current.audioAssetPath || previous.looping != current.looping ||
           previous.spatial != current.spatial || previous.minDistance != current.minDistance ||
           previous.maxDistance != current.maxDistance || previous.attenuationModel != current.attenuationModel ||
           previous.occlusionEnabled != current.occlusionEnabled ||
           previous.occlusionGain != current.occlusionGain ||
           previous.occlusionLowPassHz != current.occlusionLowPassHz ||
           previous.occlusionLayerMask != current.occlusionLayerMask;
}

} // namespace

struct AudioSourceSystem::Impl {
    struct SourceState final {
        VoiceHandle voice;
        AudioSourceDesc descriptor;
    };

    std::unordered_map<Scene::Entity, SourceState> entityToVoice;
};

AudioSourceSystem::AudioSourceSystem() : m_impl(std::make_unique<Impl>()) {}

AudioSourceSystem::~AudioSourceSystem() = default;

AudioSourceSystem::EnsuredVoice AudioSourceSystem::EnsureVoice(
    Scene::Entity entity, const Scene::AudioSourceComponent& source, IAudioSystem& audioSystem) {
    if (const auto existing = m_impl->entityToVoice.find(entity); existing != m_impl->entityToVoice.end()) {
        return {existing->second.voice, false};
    }
    if (!Scene::IsAudioSourceComponentValid(source)) {
        return {kInvalidVoiceHandle, false};
    }
    const AudioSourceDesc descriptor = MakeAudioSourceDesc(entity, source);
    const VoiceHandle voice = audioSystem.CreateSource(descriptor);
    if (voice == kInvalidVoiceHandle) {
        return {voice, false};
    }
    m_impl->entityToVoice.emplace(entity, Impl::SourceState{voice, descriptor});
    return {voice, true};
}

bool AudioSourceSystem::PlayEntity(Scene::Entity entity, Scene::IEntityManager& entityManager,
                                         IAudioSystem& audioSystem) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::AudioSourceComponent>(entity)) {
        return false;
    }
    const Scene::AudioSourceComponent& source =
        entityManager.GetComponent<Scene::AudioSourceComponent>(entity);
    const EnsuredVoice ensured = EnsureVoice(entity, source, audioSystem);
    if (ensured.voice == kInvalidVoiceHandle) {
        return false;
    }
    if (ensured.created) {
        const Scene::WorldTransformComponent& worldTransform =
            entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
        audioSystem.SetSourcePosition(ensured.voice, worldTransform.worldPosition);
        audioSystem.SetSourceVolume(ensured.voice, source.volume);
        audioSystem.SetSourcePitch(ensured.voice, source.pitch);
    }
    return audioSystem.Play(ensured.voice);
}

bool AudioSourceSystem::StopEntity(Scene::Entity entity, IAudioSystem& audioSystem) {
    const auto iterator = m_impl->entityToVoice.find(entity);
    if (iterator == m_impl->entityToVoice.end()) {
        return false;
    }
    return audioSystem.Stop(iterator->second.voice);
}

bool AudioSourceSystem::IsEntityPlaying(Scene::Entity entity,
                                              const IAudioSystem& audioSystem) const {
    const auto iterator = m_impl->entityToVoice.find(entity);
    if (iterator == m_impl->entityToVoice.end()) {
        return false;
    }
    return audioSystem.GetSourceState(iterator->second.voice) == VoicePlaybackState::Playing;
}

void AudioSourceSystem::Sync(Scene::IEntityManager& entityManager, IAudioSystem& audioSystem) {
    std::unordered_set<Scene::Entity> seen;

    entityManager.ForEach<Scene::WorldTransformComponent, Scene::AudioSourceComponent>(
        [&](Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
            const Scene::AudioSourceComponent& audioSource) {
            seen.insert(entity);
            if (!Scene::IsAudioSourceComponentValid(audioSource)) {
                UVE_ERROR("AudioSourceSystem: ignoring invalid authored audio source on entity ({}, {})",
                          entity.index, entity.generation);
                return;
            }

            const AudioSourceDesc desiredDesc = MakeAudioSourceDesc(entity, audioSource);
            auto iterator = m_impl->entityToVoice.find(entity);
            if (iterator == m_impl->entityToVoice.end()) {
                const EnsuredVoice ensured = EnsureVoice(entity, audioSource, audioSystem);
                if (ensured.voice == kInvalidVoiceHandle) {
                    return;
                }
                iterator = m_impl->entityToVoice.find(entity);
                if (audioSource.playOnAwake) {
                    static_cast<void>(audioSystem.Play(ensured.voice));
                }
            } else {
                Impl::SourceState& state = iterator->second;
                if (RequiresVoiceReplacement(state.descriptor, desiredDesc)) {
                    const VoicePlaybackState previousPlaybackState = audioSystem.GetSourceState(state.voice);
                    const VoiceHandle replacement = audioSystem.CreateSource(desiredDesc);
                    if (replacement != kInvalidVoiceHandle) {
                        const bool wasPlaying = previousPlaybackState == VoicePlaybackState::Playing;
                        if (!wasPlaying || audioSystem.Play(replacement)) {
                            const VoiceHandle previousVoice = state.voice;
                            state.voice = replacement;
                            state.descriptor = desiredDesc;
                            audioSystem.DestroySource(previousVoice);
                        } else {
                            audioSystem.DestroySource(replacement);
                        }
                    }
                }
            }

            if (iterator->second.descriptor.mixerGroup != desiredDesc.mixerGroup &&
                audioSystem.SetSourceMixerGroup(iterator->second.voice, desiredDesc.mixerGroup)) {
                iterator->second.descriptor.mixerGroup = desiredDesc.mixerGroup;
            }
            audioSystem.SetSourcePosition(iterator->second.voice, worldTransform.worldPosition);
            audioSystem.SetSourceVolume(iterator->second.voice, audioSource.volume);
            audioSystem.SetSourcePitch(iterator->second.voice, audioSource.pitch);
        });

    for (auto iterator = m_impl->entityToVoice.begin(); iterator != m_impl->entityToVoice.end();) {
        if (!seen.contains(iterator->first)) {
            audioSystem.DestroySource(iterator->second.voice);
            iterator = m_impl->entityToVoice.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

} // namespace UVE::Audio
