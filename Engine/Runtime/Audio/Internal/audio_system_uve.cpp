// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/audio_system_uve.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_map>

#include "uve/logging/logging_macros_uve.h"
#include "uve/audio/audio_listener_orientation_validation_uve.h"

namespace UVE::Audio {

namespace {

struct SourceStateUVE {
    AudioSourceDescUVE desc;
    std::string mixerGroup = std::string(kMasterAudioMixerGroupNameUVE);
    Math::Vector3UVE position{};
    /// Last frame's position, kept only so UpdateUVE() can derive the velocity Doppler needs. The
    /// flag exists because a source that has just appeared has not moved: treating its first
    /// position as a displacement from the origin would invent one enormous velocity for one frame.
    Math::Vector3UVE previousPosition{};
    bool hasPreviousPosition = false;
    float volume = 1.0F;
    float pitch = 1.0F;
    Pcm16StreamRefillSchedulerUVE streamScheduler;
    PcmGainEffectScheduleUVE effectSchedule;
    /// Non-null for a streaming source: the media the pump pulls from. Borrowed, never owned - the
    /// caller keeps the decoder's lifetime, exactly like a clip resolver is borrowed.
    IAudioStreamSourceUVE* stream = nullptr;
    bool streamEnded = false;
    /// Smoothed occlusion in [0, 1] and the raw target the last physics query answered with. The
    /// timer only exists so the query runs at ten hertz instead of once per frame.
    float occlusion = 0.0F;
    float occlusionTarget = 0.0F;
    float occlusionQueryTimer = 0.0F;
};

/// Moves one bounded window from a streaming source into its device ring. Reading only what the ring
/// can take is what keeps memory bounded: the source never gets to decide how much is resident.
std::size_t PumpStreamingSourceStateUVE(IAudioDeviceUVE& device, SourceStateUVE& state,
                                        const VoiceHandleUVE handle, std::vector<float>& scratch) {
    if (state.stream == nullptr || state.streamEnded) {
        return 0U;
    }
    const std::size_t freeFrames = device.GetStreamFreeFramesUVE(handle);
    if (freeFrames == 0U) {
        return 0U;
    }
    std::size_t frames = state.stream->ReadFramesUVE(freeFrames, scratch);
    if (frames == 0U && state.desc.looping && state.stream->SeekToFrameUVE(0U)) {
        frames = state.stream->ReadFramesUVE(freeFrames, scratch);
    }
    if (frames == 0U) {
        // The media is finished (and, for a looping source, was not seekable): let the voice drain
        // and stop itself rather than leaving it playing silence forever.
        state.streamEnded = true;
        static_cast<void>(device.EndStreamUVE(handle));
        return 0U;
    }
    return device.PushStreamFramesUVE(handle, std::span<const float>{scratch});
}

} // namespace

struct AudioSystemUVE::ImplUVE {
    IAudioDeviceUVE& audioDevice;
    IAudioClipResolverUVE* clipResolver = nullptr;
    Math::Vector3UVE listenerPosition{};
    Math::Vector3UVE previousListenerPosition{};
    bool hasPreviousListenerPosition = false;
    Math::Vector3UVE listenerForward{0.0F, 0.0F, -1.0F};
    Math::Vector3UVE listenerUp{0.0F, 1.0F, 0.0F};
    std::unordered_map<std::uint32_t, SourceStateUVE> sources;
    AudioMixerGroupUVE mixerGroups;
    /// Reused by every pump call so streaming never allocates per frame on the engine thread.
    std::vector<float> streamScratch;
    /// Borrowed; null means "nothing occludes anything", which is the pre-occlusion behaviour.
    const IAudioOcclusionProviderUVE* occlusionProvider = nullptr;

    explicit ImplUVE(IAudioDeviceUVE& device, IAudioClipResolverUVE* resolver)
        : audioDevice(device), clipResolver(resolver) {}
};

AudioSystemUVE::AudioSystemUVE(IAudioDeviceUVE& audioDevice, IAudioClipResolverUVE* clipResolver)
    : m_impl(std::make_unique<ImplUVE>(audioDevice, clipResolver)) {}

AudioSystemUVE::~AudioSystemUVE() = default;

void AudioSystemUVE::SetListenerPositionUVE(Math::Vector3UVE position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        UVE_ERROR("AudioSystemUVE: SetListenerPositionUVE rejected non-finite position");
        return;
    }
    m_impl->listenerPosition = position;
}

void AudioSystemUVE::SetListenerOrientationUVE(Math::Vector3UVE forward, Math::Vector3UVE up) {
    if (!IsAudioListenerOrientationValidUVE(forward, up)) {
        UVE_ERROR("AudioSystemUVE: SetListenerOrientationUVE rejected invalid orientation vectors");
        return;
    }
    m_impl->listenerForward = forward;
    m_impl->listenerUp = up;
}

Math::Vector3UVE AudioSystemUVE::GetListenerPositionUVE() const noexcept {
    return m_impl->listenerPosition;
}

VoiceHandleUVE AudioSystemUVE::CreateSourceUVE(const AudioSourceDescUVE& desc) {
    if (!IsAudioSourceDescValidUVE(desc)) {
        UVE_ERROR("AudioSystemUVE: CreateSourceUVE rejected invalid source descriptor");
        return kInvalidVoiceHandleUVE;
    }
    std::string resolvedAudioAssetPath = desc.audioAssetPath;
    if (m_impl->clipResolver != nullptr) {
        AudioClipResolutionUVE resolution;
        try {
            resolution = m_impl->clipResolver->ResolveAudioClipUVE(desc.audioAssetPath);
        } catch (const std::exception& exception) {
            UVE_ERROR("AudioSystemUVE: audio clip resolver threw for '{}': {}", desc.audioAssetPath,
                      exception.what());
            return kInvalidVoiceHandleUVE;
        } catch (...) {
            UVE_ERROR("AudioSystemUVE: audio clip resolver threw an unknown exception for '{}'",
                      desc.audioAssetPath);
            return kInvalidVoiceHandleUVE;
        }
        if (!resolution.accepted) {
            UVE_ERROR("AudioSystemUVE: audio clip resolution rejected '{}': {}", desc.audioAssetPath,
                      resolution.diagnostic);
            return kInvalidVoiceHandleUVE;
        }
        resolvedAudioAssetPath = resolution.resolvedPath;
    }

    const VoiceHandleUVE voice =
        m_impl->audioDevice.CreateVoiceUVE(AudioVoiceDescUVE{resolvedAudioAssetPath, desc.looping});
    if (voice == kInvalidVoiceHandleUVE) {
        UVE_ERROR("AudioSystemUVE: audio device returned an invalid voice handle for '{}'",
                  resolvedAudioAssetPath);
        return kInvalidVoiceHandleUVE;
    }
    if (m_impl->sources.contains(voice.value)) {
        UVE_ERROR("AudioSystemUVE: audio device returned a duplicate voice handle ({})", voice.value);
        m_impl->audioDevice.DestroyVoiceUVE(voice);
        return kInvalidVoiceHandleUVE;
    }
    SourceStateUVE state;
    state.desc = desc;
    state.desc.audioAssetPath = resolvedAudioAssetPath;
    state.mixerGroup = m_impl->mixerGroups.ResolveGroupNameUVE(desc.mixerGroup);
    state.desc.mixerGroup = state.mixerGroup;
    state.volume = desc.volume;
    state.pitch = desc.pitch;
    static_cast<void>(m_impl->mixerGroups.AttachSourceUVE(state.mixerGroup));
    m_impl->sources.emplace(voice.value, std::move(state));
    return voice;
}

VoiceHandleUVE AudioSystemUVE::CreateStreamingSourceUVE(const AudioSourceDescUVE& desc,
                                                         IAudioStreamSourceUVE& source) {
    if (!IsAudioSourceDescValidUVE(desc)) {
        UVE_ERROR("AudioSystemUVE: CreateStreamingSourceUVE rejected an invalid source descriptor");
        return kInvalidVoiceHandleUVE;
    }
    const AudioStreamFormatUVE format = source.GetFormatUVE();
    if (!IsAudioStreamFormatValidUVE(format)) {
        UVE_ERROR("AudioSystemUVE: CreateStreamingSourceUVE rejected a source with an invalid format ({} Hz, {} channels)",
                  format.sampleRate, format.channels);
        return kInvalidVoiceHandleUVE;
    }

    const VoiceHandleUVE voice = m_impl->audioDevice.CreateStreamVoiceUVE(format);
    if (voice == kInvalidVoiceHandleUVE) {
        UVE_ERROR("AudioSystemUVE: audio device returned an invalid streaming voice handle");
        return kInvalidVoiceHandleUVE;
    }
    if (m_impl->sources.contains(voice.value)) {
        UVE_ERROR("AudioSystemUVE: audio device returned a duplicate voice handle ({})", voice.value);
        m_impl->audioDevice.DestroyVoiceUVE(voice);
        return kInvalidVoiceHandleUVE;
    }

    SourceStateUVE state;
    state.desc = desc;
    // A stream carries its own media; a path here would only suggest a clip that is never decoded.
    state.desc.audioAssetPath.clear();
    state.mixerGroup = m_impl->mixerGroups.ResolveGroupNameUVE(desc.mixerGroup);
    state.desc.mixerGroup = state.mixerGroup;
    state.volume = desc.volume;
    state.pitch = desc.pitch;
    state.stream = &source;
    static_cast<void>(m_impl->mixerGroups.AttachSourceUVE(state.mixerGroup));
    m_impl->sources.emplace(voice.value, std::move(state));
    return voice;
}

std::size_t AudioSystemUVE::PumpStreamingSourceUVE(const VoiceHandleUVE source) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || iterator->second.stream == nullptr) {
        return 0U;
    }
    return PumpStreamingSourceStateUVE(m_impl->audioDevice, iterator->second, source, m_impl->streamScratch);
}

std::size_t AudioSystemUVE::GetStreamingSourceBufferedFramesUVE(const VoiceHandleUVE source) const {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || iterator->second.stream == nullptr) {
        return 0U;
    }
    return m_impl->audioDevice.GetStreamBufferedFramesUVE(source);
}

void AudioSystemUVE::DestroySourceUVE(VoiceHandleUVE source) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: DestroySourceUVE called with an unknown handle ({})", source.value);
        return;
    }
    static_cast<void>(m_impl->mixerGroups.DetachSourceUVE(iterator->second.mixerGroup));
    m_impl->sources.erase(iterator);
    m_impl->audioDevice.DestroyVoiceUVE(source);
}

bool AudioSystemUVE::PlayUVE(VoiceHandleUVE source) {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystemUVE: PlayUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->audioDevice.PlayUVE(source);
}

bool AudioSystemUVE::StopUVE(VoiceHandleUVE source) {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystemUVE: StopUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->audioDevice.StopUVE(source);
}

VoicePlaybackStateUVE AudioSystemUVE::GetSourceStateUVE(VoiceHandleUVE source) const {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystemUVE: GetSourceStateUVE called with an unknown handle ({})", source.value);
        return VoicePlaybackStateUVE::Stopped;
    }
    return m_impl->audioDevice.GetVoiceStateUVE(source);
}

void AudioSystemUVE::SetSourcePositionUVE(VoiceHandleUVE source, Math::Vector3UVE position) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: SetSourcePositionUVE called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        UVE_ERROR("AudioSystemUVE: SetSourcePositionUVE rejected non-finite position for handle ({})", source.value);
        return;
    }
    iterator->second.position = position;
}

void AudioSystemUVE::SetSourceVolumeUVE(VoiceHandleUVE source, float volume) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: SetSourceVolumeUVE called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(volume) || volume < 0.0F) {
        UVE_ERROR("AudioSystemUVE: SetSourceVolumeUVE rejected invalid volume for handle ({})", source.value);
        return;
    }
    iterator->second.volume = volume;
}

void AudioSystemUVE::SetSourcePitchUVE(VoiceHandleUVE source, float pitch) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: SetSourcePitchUVE called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(pitch) || pitch < 0.0F) {
        UVE_ERROR("AudioSystemUVE: SetSourcePitchUVE rejected invalid pitch for handle ({})", source.value);
        return;
    }
    iterator->second.pitch = pitch;
}

bool AudioSystemUVE::RegisterMixerGroupUVE(const std::string_view name, const std::string_view parentName,
                                            const float volumeMultiplier, const float pitchMultiplier) {
    return m_impl->mixerGroups.RegisterGroupUVE(name, parentName, volumeMultiplier, pitchMultiplier);
}

bool AudioSystemUVE::RegisterMixerGroupUVE(const std::string_view name, const float volumeMultiplier,
                                            const float pitchMultiplier) {
    return m_impl->mixerGroups.RegisterGroupUVE(name, volumeMultiplier, pitchMultiplier);
}

bool AudioSystemUVE::RemoveMixerGroupUVE(const std::string_view name) {
    return m_impl->mixerGroups.RemoveGroupUVE(name);
}

bool AudioSystemUVE::SetMixerGroupParentUVE(const std::string_view name, const std::string_view parentName) {
    return m_impl->mixerGroups.SetGroupParentUVE(name, parentName);
}

bool AudioSystemUVE::SetMixerGroupVolumeUVE(const std::string_view name, const float volumeMultiplier) {
    return m_impl->mixerGroups.SetGroupVolumeUVE(name, volumeMultiplier);
}

bool AudioSystemUVE::SetMixerGroupPitchUVE(const std::string_view name, const float pitchMultiplier) {
    return m_impl->mixerGroups.SetGroupPitchUVE(name, pitchMultiplier);
}

bool AudioSystemUVE::SetMixerGroupMutedUVE(const std::string_view name, const bool muted) {
    return m_impl->mixerGroups.SetGroupMutedUVE(name, muted);
}

bool AudioSystemUVE::SetMixerGroupSoloedUVE(const std::string_view name, const bool soloed) {
    return m_impl->mixerGroups.SetGroupSoloedUVE(name, soloed);
}

bool AudioSystemUVE::SetMixerInsertUVE(const std::string_view name, const std::size_t slot,
                                        const AudioInsertDescUVE& desc) {
    return m_impl->mixerGroups.SetGroupInsertUVE(name, slot, desc);
}

bool AudioSystemUVE::ClearMixerInsertUVE(const std::string_view name, const std::size_t slot) {
    return m_impl->mixerGroups.ClearGroupInsertUVE(name, slot);
}

bool AudioSystemUVE::SetMixerSampleRateUVE(const float sampleRateHz) {
    return m_impl->mixerGroups.SetSampleRateUVE(sampleRateHz);
}

bool AudioSystemUVE::SetSourceMixerGroupUVE(const VoiceHandleUVE source, const std::string_view name) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || !m_impl->mixerGroups.HasGroupUVE(name)) {
        return false;
    }
    const std::string resolvedName(name);
    if (iterator->second.mixerGroup == resolvedName) {
        return true;
    }
    if (!m_impl->mixerGroups.AttachSourceUVE(resolvedName)) {
        return false;
    }
    static_cast<void>(m_impl->mixerGroups.DetachSourceUVE(iterator->second.mixerGroup));
    iterator->second.mixerGroup = resolvedName;
    iterator->second.desc.mixerGroup = resolvedName;
    return true;
}

AudioMixerDiagnosticsUVE AudioSystemUVE::GetMixerDiagnosticsUVE(const std::size_t maximumGroups) const {
    return m_impl->mixerGroups.GetDiagnosticsUVE(maximumGroups);
}

bool AudioSystemUVE::ResetSourceStreamUVE(const VoiceHandleUVE source, const std::size_t totalSamples,
                                          const bool loop, const std::size_t cursorSample,
                                          const std::size_t maximumSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: ResetSourceStreamUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.ResetUVE(totalSamples, loop, cursorSample, maximumSamples);
}

bool AudioSystemUVE::ScheduleSourceStreamWindowUVE(const VoiceHandleUVE source,
                                                    const std::size_t requestedSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: ScheduleSourceStreamWindowUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.ScheduleWindowUVE(requestedSamples);
}

bool AudioSystemUVE::PopSourceStreamWindowUVE(const VoiceHandleUVE source,
                                               Pcm16StreamWindowPlanUVE& outPlan) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: PopSourceStreamWindowUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.PopNextWindowUVE(outPlan);
}

bool AudioSystemUVE::ScheduleSourcePcmGainWindowUVE(const VoiceHandleUVE source,
                                                     const PcmGainEffectWindowUVE& window) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: ScheduleSourcePcmGainWindowUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.effectSchedule.ScheduleWindowUVE(window);
}

bool AudioSystemUVE::ApplySourcePcmGainEffectsUVE(const VoiceHandleUVE source,
                                                   const std::vector<float>& inputSamples,
                                                   std::vector<float>& outputSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: ApplySourcePcmGainEffectsUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.effectSchedule.ApplyPendingUVE(inputSamples, outputSamples);
}

bool AudioSystemUVE::ApplySourceBusChainUVE(const VoiceHandleUVE source,
                                             const std::vector<float>& inputSamples,
                                             std::vector<float>& outputSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystemUVE: ApplySourceBusChainUVE called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->mixerGroups.ApplyBusChainUVE(iterator->second.mixerGroup, inputSamples, outputSamples);
}

void AudioSystemUVE::SetOcclusionProviderUVE(const IAudioOcclusionProviderUVE* provider) {
    m_impl->occlusionProvider = provider;
}

void AudioSystemUVE::UpdateUVE(const float deltaSeconds) {
    // A hitch must not manufacture a fast-moving source out of one late frame, and a caller that
    // does not track time (delta 0) simply gets no Doppler - never a division by zero.
    const float stepSeconds = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F)
                                  ? std::min(deltaSeconds, kMaximumAudioVelocityStepSecondsUVE)
                                  : 0.0F;
    Math::Vector3UVE listenerVelocity{};
    if (stepSeconds > 0.0F && m_impl->hasPreviousListenerPosition) {
        listenerVelocity =
            (m_impl->listenerPosition - m_impl->previousListenerPosition) * (1.0F / stepSeconds);
    }
    m_impl->previousListenerPosition = m_impl->listenerPosition;
    m_impl->hasPreviousListenerPosition = true;
    // The same forward/up pair the listener already carries; +X for the default orientation.
    const Math::Vector3UVE listenerRight =
        ComputeAudioListenerRightUVE(m_impl->listenerForward, m_impl->listenerUp);

    for (auto& [handleValue, state] : m_impl->sources) {
        // A streaming source is pumped once per frame, before its parameters are pushed, so the
        // ring is as full as the device will allow when the callback next runs.
        static_cast<void>(
            PumpStreamingSourceStateUVE(m_impl->audioDevice, state, VoiceHandleUVE{handleValue}, m_impl->streamScratch));

        Math::Vector3UVE sourceVelocity{};
        if (stepSeconds > 0.0F && state.hasPreviousPosition) {
            sourceVelocity = (state.position - state.previousPosition) * (1.0F / stepSeconds);
        }
        state.previousPosition = state.position;
        state.hasPreviousPosition = true;

        // Effective, not authored: the source hears its bus composed with every ancestor, muted and
        // solo-silenced buses included. The flat case (one bus under Master) is unchanged because
        // Master contributes unity.
        const float groupVolume = m_impl->mixerGroups.GetEffectiveVolumeUVE(state.mixerGroup);
        const float groupPitch = m_impl->mixerGroups.GetEffectivePitchUVE(state.mixerGroup);
        float attenuation = 1.0F;
        float pan = 0.0F;
        float dopplerPitch = 1.0F;
        float lowPassCutoffHz = 0.0F;
        if (state.desc.spatial) {
            const Math::Vector3UVE listenerDelta = state.position - m_impl->listenerPosition;
            const float distanceSquared = Math::LengthSquaredUVE(listenerDelta);
            if (!std::isfinite(distanceSquared) || distanceSquared < 0.0F) {
                attenuation = 0.0F;
            } else {
                const float distance = std::sqrt(distanceSquared);
                attenuation = std::isfinite(distance)
                    ? ComputeDistanceAttenuationUVE(distance, state.desc.minDistance,
                                                     state.desc.maxDistance, state.desc.attenuationModel)
                    : 0.0F;
            }
            pan = ComputeAudioPanUVE(listenerDelta, listenerRight);
            // The cone is measured from the emitter's axis to the direction it is being heard from;
            // its gain multiplies the distance attenuation rather than replacing it.
            const float coneAngle =
                ComputeAudioConeAngleDegreesUVE(state.desc.coneDirection, -listenerDelta);
            attenuation *= ComputeAudioConeAttenuationUVE(coneAngle, state.desc.coneInnerAngleDegrees,
                                                           state.desc.coneOuterAngleDegrees,
                                                           state.desc.coneOuterGain);
            dopplerPitch = ComputeAudioDopplerPitchUVE(listenerVelocity, sourceVelocity, listenerDelta,
                                                        state.desc.dopplerFactor);

            // Occlusion: query at a bounded rate, smooth every frame, then fold the answer into the
            // attenuation the mixer already understands. A source with no provider, a 2D source or
            // one that opted out decays back to clear rather than freezing at its last blocked
            // value, so removing the provider mid-scene fades out instead of sticking.
            const bool occludable = m_impl->occlusionProvider != nullptr && state.desc.occlusionEnabled;
            if (occludable) {
                state.occlusionQueryTimer -= stepSeconds;
                if (state.occlusionQueryTimer <= 0.0F) {
                    AudioOcclusionQueryUVE query;
                    query.listenerPosition = m_impl->listenerPosition;
                    query.sourcePosition = state.position;
                    query.listenerRight = listenerRight;
                    query.sourceEntity = state.desc.ownerEntity;
                    query.layerMask = state.desc.occlusionLayerMask;
                    const float answer = m_impl->occlusionProvider->QueryOcclusionUVE(query);
                    state.occlusionTarget =
                        std::isfinite(answer) ? std::clamp(answer, 0.0F, 1.0F) : 0.0F;
                    state.occlusionQueryTimer = kAudioOcclusionQueryIntervalSecondsUVE;
                }
            } else {
                state.occlusionTarget = 0.0F;
                state.occlusionQueryTimer = 0.0F;
            }
            state.occlusion = SmoothAudioOcclusionUVE(state.occlusion, state.occlusionTarget, deltaSeconds);
            attenuation *= ComputeAudioOcclusionGainUVE(state.occlusion, state.desc.occlusionGain);
            lowPassCutoffHz =
                ComputeAudioOcclusionCutoffUVE(state.occlusion, state.desc.occlusionLowPassHz);
        }
        AudioMixParametersUVE parameters;
        if (!EvaluateAudioMixParametersUVE(state.volume, state.pitch * dopplerPitch, groupVolume, groupPitch,
                                            attenuation, parameters)) {
            UVE_ERROR("AudioSystemUVE: invalid mix parameters for source {}", handleValue);
            parameters = AudioMixParametersUVE{0.0F, 1.0F};
        }
        static_cast<void>(m_impl->audioDevice.SetVoiceParamsUVE(
            VoiceHandleUVE{handleValue},
            AudioVoiceParamsUVE{state.position, parameters.gain, parameters.pitch, pan, lowPassCutoffHz}));
    }
}

} // namespace UVE::Audio
