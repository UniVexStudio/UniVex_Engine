// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_system.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>

#include "logging_macros.h"
#include "audio_listener_orientation_validation.h"

namespace UVE::Audio {

namespace {

struct SourceState {
    AudioSourceDesc desc;
    std::string mixerGroup = std::string(kMasterAudioMixerGroupName);
    Math::Vector3 position{};
    /// Last frame's position, kept only so Update() can derive the velocity Doppler needs. The
    /// flag exists because a source that has just appeared has not moved: treating its first
    /// position as a displacement from the origin would invent one enormous velocity for one frame.
    Math::Vector3 previousPosition{};
    bool hasPreviousPosition = false;
    float volume = 1.0F;
    float pitch = 1.0F;
    Pcm16StreamRefillScheduler streamScheduler;
    PcmGainEffectSchedule effectSchedule;
    /// Non-null for a streaming source: the media the pump pulls from. Borrowed, never owned - the
    /// caller keeps the decoder's lifetime, exactly like a clip resolver is borrowed.
    IAudioStreamSource* stream = nullptr;
    bool streamEnded = false;
    /// Smoothed occlusion in [0, 1] and the raw target the last physics query answered with. The
    /// timer only exists so the query runs at ten hertz instead of once per frame.
    float occlusion = 0.0F;
    float occlusionTarget = 0.0F;
    float occlusionQueryTimer = 0.0F;
};

/// Moves one bounded window from a streaming source into its device ring. Reading only what the ring
/// can take is what keeps memory bounded: the source never gets to decide how much is resident.
std::size_t PumpStreamingSourceState(IAudioDevice& device, SourceState& state,
                                        const VoiceHandle handle, std::vector<float>& scratch) {
    if (state.stream == nullptr || state.streamEnded) {
        return 0U;
    }
    const std::size_t freeFrames = device.GetStreamFreeFrames(handle);
    if (freeFrames == 0U) {
        return 0U;
    }
    std::size_t frames = state.stream->ReadFrames(freeFrames, scratch);
    if (frames == 0U && state.desc.looping && state.stream->SeekToFrame(0U)) {
        frames = state.stream->ReadFrames(freeFrames, scratch);
    }
    if (frames == 0U) {
        // The media is finished (and, for a looping source, was not seekable): let the voice drain
        // and stop itself rather than leaving it playing silence forever.
        state.streamEnded = true;
        static_cast<void>(device.EndStream(handle));
        return 0U;
    }
    return device.PushStreamFrames(handle, std::span<const float>{scratch});
}

} // namespace

struct AudioSystem::Impl {
    IAudioDevice& audioDevice;
    IAudioClipResolver* clipResolver = nullptr;
    Math::Vector3 listenerPosition{};
    Math::Vector3 previousListenerPosition{};
    bool hasPreviousListenerPosition = false;
    Math::Vector3 listenerForward{0.0F, 0.0F, -1.0F};
    Math::Vector3 listenerUp{0.0F, 1.0F, 0.0F};
    std::unordered_map<std::uint32_t, SourceState> sources;
    AudioMixerGroup mixerGroups;
    /// Reused by every pump call so streaming never allocates per frame on the engine thread.
    std::vector<float> streamScratch;
    /// Borrowed; null means "nothing occludes anything", which is the pre-occlusion behaviour.
    const IAudioOcclusionProvider* occlusionProvider = nullptr;

    explicit Impl(IAudioDevice& device, IAudioClipResolver* resolver)
        : audioDevice(device), clipResolver(resolver) {}
};

AudioSystem::AudioSystem(IAudioDevice& audioDevice, IAudioClipResolver* clipResolver)
    : m_impl(std::make_unique<Impl>(audioDevice, clipResolver)) {}

AudioSystem::~AudioSystem() = default;

void AudioSystem::SetListenerPosition(Math::Vector3 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        UVE_ERROR("AudioSystem: SetListenerPosition rejected non-finite position");
        return;
    }
    m_impl->listenerPosition = position;
}

void AudioSystem::SetListenerOrientation(Math::Vector3 forward, Math::Vector3 up) {
    if (!IsAudioListenerOrientationValid(forward, up)) {
        UVE_ERROR("AudioSystem: SetListenerOrientation rejected invalid orientation vectors");
        return;
    }
    m_impl->listenerForward = forward;
    m_impl->listenerUp = up;
}

Math::Vector3 AudioSystem::GetListenerPosition() const noexcept {
    return m_impl->listenerPosition;
}

VoiceHandle AudioSystem::CreateSource(const AudioSourceDesc& desc) {
    if (!IsAudioSourceDescValid(desc)) {
        UVE_ERROR("AudioSystem: CreateSource rejected invalid source descriptor");
        return kInvalidVoiceHandle;
    }
    std::string resolvedAudioAssetPath = desc.audioAssetPath;
    if (m_impl->clipResolver != nullptr) {
        AudioClipResolution resolution;
        try {
            resolution = m_impl->clipResolver->ResolveAudioClip(desc.audioAssetPath);
        } catch (const std::exception& exception) {
            UVE_ERROR("AudioSystem: audio clip resolver threw for '{}': {}", desc.audioAssetPath,
                      exception.what());
            return kInvalidVoiceHandle;
        } catch (...) {
            UVE_ERROR("AudioSystem: audio clip resolver threw an unknown exception for '{}'",
                      desc.audioAssetPath);
            return kInvalidVoiceHandle;
        }
        if (!resolution.accepted) {
            UVE_ERROR("AudioSystem: audio clip resolution rejected '{}': {}", desc.audioAssetPath,
                      resolution.diagnostic);
            return kInvalidVoiceHandle;
        }
        resolvedAudioAssetPath = resolution.resolvedPath;
    }

    const VoiceHandle voice =
        m_impl->audioDevice.CreateVoice(AudioVoiceDesc{resolvedAudioAssetPath, desc.looping});
    if (voice == kInvalidVoiceHandle) {
        UVE_ERROR("AudioSystem: audio device returned an invalid voice handle for '{}'",
                  resolvedAudioAssetPath);
        return kInvalidVoiceHandle;
    }
    if (m_impl->sources.contains(voice.value)) {
        UVE_ERROR("AudioSystem: audio device returned a duplicate voice handle ({})", voice.value);
        m_impl->audioDevice.DestroyVoice(voice);
        return kInvalidVoiceHandle;
    }
    SourceState state;
    state.desc = desc;
    state.desc.audioAssetPath = resolvedAudioAssetPath;
    state.mixerGroup = m_impl->mixerGroups.ResolveGroupName(desc.mixerGroup);
    state.desc.mixerGroup = state.mixerGroup;
    state.volume = desc.volume;
    state.pitch = desc.pitch;
    static_cast<void>(m_impl->mixerGroups.AttachSource(state.mixerGroup));
    m_impl->sources.emplace(voice.value, std::move(state));
    return voice;
}

VoiceHandle AudioSystem::CreateStreamingSource(const AudioSourceDesc& desc,
                                                         IAudioStreamSource& source) {
    if (!IsAudioSourceDescValid(desc)) {
        UVE_ERROR("AudioSystem: CreateStreamingSource rejected an invalid source descriptor");
        return kInvalidVoiceHandle;
    }
    const AudioStreamFormat format = source.GetFormat();
    if (!IsAudioStreamFormatValid(format)) {
        UVE_ERROR("AudioSystem: CreateStreamingSource rejected a source with an invalid format ({} Hz, {} channels)",
                  format.sampleRate, format.channels);
        return kInvalidVoiceHandle;
    }

    const VoiceHandle voice = m_impl->audioDevice.CreateStreamVoice(format);
    if (voice == kInvalidVoiceHandle) {
        UVE_ERROR("AudioSystem: audio device returned an invalid streaming voice handle");
        return kInvalidVoiceHandle;
    }
    if (m_impl->sources.contains(voice.value)) {
        UVE_ERROR("AudioSystem: audio device returned a duplicate voice handle ({})", voice.value);
        m_impl->audioDevice.DestroyVoice(voice);
        return kInvalidVoiceHandle;
    }

    SourceState state;
    state.desc = desc;
    // A stream carries its own media; a path here would only suggest a clip that is never decoded.
    state.desc.audioAssetPath.clear();
    state.mixerGroup = m_impl->mixerGroups.ResolveGroupName(desc.mixerGroup);
    state.desc.mixerGroup = state.mixerGroup;
    state.volume = desc.volume;
    state.pitch = desc.pitch;
    state.stream = &source;
    static_cast<void>(m_impl->mixerGroups.AttachSource(state.mixerGroup));
    m_impl->sources.emplace(voice.value, std::move(state));
    return voice;
}

std::size_t AudioSystem::PumpStreamingSource(const VoiceHandle source) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || iterator->second.stream == nullptr) {
        return 0U;
    }
    return PumpStreamingSourceState(m_impl->audioDevice, iterator->second, source, m_impl->streamScratch);
}

std::size_t AudioSystem::GetStreamingSourceBufferedFrames(const VoiceHandle source) const {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || iterator->second.stream == nullptr) {
        return 0U;
    }
    return m_impl->audioDevice.GetStreamBufferedFrames(source);
}

void AudioSystem::DestroySource(VoiceHandle source) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: DestroySource called with an unknown handle ({})", source.value);
        return;
    }
    static_cast<void>(m_impl->mixerGroups.DetachSource(iterator->second.mixerGroup));
    m_impl->sources.erase(iterator);
    m_impl->audioDevice.DestroyVoice(source);
}

bool AudioSystem::Play(VoiceHandle source) {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystem: Play called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->audioDevice.Play(source);
}

bool AudioSystem::Stop(VoiceHandle source) {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystem: Stop called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->audioDevice.Stop(source);
}

VoicePlaybackState AudioSystem::GetSourceState(VoiceHandle source) const {
    if (!m_impl->sources.contains(source.value)) {
        UVE_ERROR("AudioSystem: GetSourceState called with an unknown handle ({})", source.value);
        return VoicePlaybackState::Stopped;
    }
    return m_impl->audioDevice.GetVoiceState(source);
}

void AudioSystem::SetSourcePosition(VoiceHandle source, Math::Vector3 position) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: SetSourcePosition called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        UVE_ERROR("AudioSystem: SetSourcePosition rejected non-finite position for handle ({})", source.value);
        return;
    }
    iterator->second.position = position;
}

void AudioSystem::SetSourceVolume(VoiceHandle source, float volume) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: SetSourceVolume called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(volume) || volume < 0.0F) {
        UVE_ERROR("AudioSystem: SetSourceVolume rejected invalid volume for handle ({})", source.value);
        return;
    }
    iterator->second.volume = volume;
}

void AudioSystem::SetSourcePitch(VoiceHandle source, float pitch) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: SetSourcePitch called with an unknown handle ({})", source.value);
        return;
    }
    if (!std::isfinite(pitch) || pitch < 0.0F) {
        UVE_ERROR("AudioSystem: SetSourcePitch rejected invalid pitch for handle ({})", source.value);
        return;
    }
    iterator->second.pitch = pitch;
}

bool AudioSystem::RegisterMixerGroup(const std::string_view name, const std::string_view parentName,
                                            const float volumeMultiplier, const float pitchMultiplier) {
    return m_impl->mixerGroups.RegisterGroup(name, parentName, volumeMultiplier, pitchMultiplier);
}

bool AudioSystem::RegisterMixerGroup(const std::string_view name, const float volumeMultiplier,
                                            const float pitchMultiplier) {
    return m_impl->mixerGroups.RegisterGroup(name, volumeMultiplier, pitchMultiplier);
}

bool AudioSystem::RemoveMixerGroup(const std::string_view name) {
    return m_impl->mixerGroups.RemoveGroup(name);
}

bool AudioSystem::SetMixerGroupParent(const std::string_view name, const std::string_view parentName) {
    return m_impl->mixerGroups.SetGroupParent(name, parentName);
}

bool AudioSystem::SetMixerGroupVolume(const std::string_view name, const float volumeMultiplier) {
    return m_impl->mixerGroups.SetGroupVolume(name, volumeMultiplier);
}

bool AudioSystem::SetMixerGroupPitch(const std::string_view name, const float pitchMultiplier) {
    return m_impl->mixerGroups.SetGroupPitch(name, pitchMultiplier);
}

bool AudioSystem::SetMixerGroupMuted(const std::string_view name, const bool muted) {
    return m_impl->mixerGroups.SetGroupMuted(name, muted);
}

bool AudioSystem::SetMixerGroupSoloed(const std::string_view name, const bool soloed) {
    return m_impl->mixerGroups.SetGroupSoloed(name, soloed);
}

bool AudioSystem::SetMixerInsert(const std::string_view name, const std::size_t slot,
                                        const AudioInsertDesc& desc) {
    return m_impl->mixerGroups.SetGroupInsert(name, slot, desc);
}

bool AudioSystem::ClearMixerInsert(const std::string_view name, const std::size_t slot) {
    return m_impl->mixerGroups.ClearGroupInsert(name, slot);
}

bool AudioSystem::SetMixerSampleRate(const float sampleRateHz) {
    return m_impl->mixerGroups.SetSampleRate(sampleRateHz);
}

bool AudioSystem::SetSourceMixerGroup(const VoiceHandle source, const std::string_view name) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end() || !m_impl->mixerGroups.HasGroup(name)) {
        return false;
    }
    const std::string resolvedName(name);
    if (iterator->second.mixerGroup == resolvedName) {
        return true;
    }
    if (!m_impl->mixerGroups.AttachSource(resolvedName)) {
        return false;
    }
    static_cast<void>(m_impl->mixerGroups.DetachSource(iterator->second.mixerGroup));
    iterator->second.mixerGroup = resolvedName;
    iterator->second.desc.mixerGroup = resolvedName;
    return true;
}

AudioMixerDiagnostics AudioSystem::GetMixerDiagnostics(const std::size_t maximumGroups) const {
    return m_impl->mixerGroups.GetDiagnostics(maximumGroups);
}

bool AudioSystem::ResetSourceStream(const VoiceHandle source, const std::size_t totalSamples,
                                          const bool loop, const std::size_t cursorSample,
                                          const std::size_t maximumSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: ResetSourceStream called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.Reset(totalSamples, loop, cursorSample, maximumSamples);
}

bool AudioSystem::ScheduleSourceStreamWindow(const VoiceHandle source,
                                                    const std::size_t requestedSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: ScheduleSourceStreamWindow called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.ScheduleWindow(requestedSamples);
}

bool AudioSystem::PopSourceStreamWindow(const VoiceHandle source,
                                               Pcm16StreamWindowPlan& outPlan) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: PopSourceStreamWindow called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.streamScheduler.PopNextWindow(outPlan);
}

bool AudioSystem::ScheduleSourcePcmGainWindow(const VoiceHandle source,
                                                     const PcmGainEffectWindow& window) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: ScheduleSourcePcmGainWindow called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.effectSchedule.ScheduleWindow(window);
}

bool AudioSystem::ApplySourcePcmGainEffects(const VoiceHandle source,
                                                   const std::vector<float>& inputSamples,
                                                   std::vector<float>& outputSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: ApplySourcePcmGainEffects called with an unknown handle ({})", source.value);
        return false;
    }
    return iterator->second.effectSchedule.ApplyPending(inputSamples, outputSamples);
}

bool AudioSystem::ApplySourceBusChain(const VoiceHandle source,
                                             const std::vector<float>& inputSamples,
                                             std::vector<float>& outputSamples) {
    const auto iterator = m_impl->sources.find(source.value);
    if (iterator == m_impl->sources.end()) {
        UVE_ERROR("AudioSystem: ApplySourceBusChain called with an unknown handle ({})", source.value);
        return false;
    }
    return m_impl->mixerGroups.ApplyBusChain(iterator->second.mixerGroup, inputSamples, outputSamples);
}

AudioMeterReading AudioSystem::GetSourceMeter(const VoiceHandle source) const {
    return m_impl->sources.contains(source.value) ? m_impl->audioDevice.ReadVoiceMeter(source)
                                                  : kSilentAudioMeter;
}

AudioMeterReading AudioSystem::GetMixerGroupMeter(const std::string_view name) const {
    const AudioMixerGroup& mixer = m_impl->mixerGroups;
    if (!mixer.HasGroup(name)) {
        return kSilentAudioMeter;
    }
    // Who belongs to this bus: the bus itself plus everything under it. The tree is walked from
    // each bus upwards instead of storing child lists, because the diagnostics already expose the
    // parent of every group and a mixer is at most kMaximumAudioMixerGroups buses deep - so the
    // whole membership test costs a handful of string comparisons per source.
    const AudioMixerDiagnostics diagnostics = mixer.GetDiagnostics();
    const auto isUnder = [&diagnostics](const std::string& group, const std::string_view ancestor) {
        // Bounded by the bus cap: every step moves to a strictly shallower parent, so this cannot
        // loop even if the tree were somehow malformed.
        std::string current = group;
        for (std::size_t step = 0U; step <= diagnostics.groups.size(); ++step) {
            if (current == ancestor) {
                return true;
            }
            const auto parent = std::find_if(
                diagnostics.groups.cbegin(), diagnostics.groups.cend(),
                [&current](const AudioMixerGroupSnapshot& snapshot) { return snapshot.name == current; });
            if (parent == diagnostics.groups.cend() || parent->parent.empty()) {
                return current == kMasterAudioMixerGroupName && ancestor == kMasterAudioMixerGroupName;
            }
            current = parent->parent;
        }
        return false;
    };

    AudioMeterReading meter = kSilentAudioMeter;
    for (const auto& [handleValue, state] : m_impl->sources) {
        if (!isUnder(state.mixerGroup, name)) {
            continue;
        }
        meter = CombineAudioMeterReadings(meter, m_impl->audioDevice.ReadVoiceMeter(VoiceHandle{handleValue}));
    }
    return meter;
}

void AudioSystem::SetOcclusionProvider(const IAudioOcclusionProvider* provider) {
    m_impl->occlusionProvider = provider;
}

void AudioSystem::Update(const float deltaSeconds) {
    // A hitch must not manufacture a fast-moving source out of one late frame, and a caller that
    // does not track time (delta 0) simply gets no Doppler - never a division by zero.
    const float stepSeconds = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F)
                                  ? std::min(deltaSeconds, kMaximumAudioVelocityStepSeconds)
                                  : 0.0F;
    Math::Vector3 listenerVelocity{};
    if (stepSeconds > 0.0F && m_impl->hasPreviousListenerPosition) {
        listenerVelocity =
            (m_impl->listenerPosition - m_impl->previousListenerPosition) * (1.0F / stepSeconds);
    }
    m_impl->previousListenerPosition = m_impl->listenerPosition;
    m_impl->hasPreviousListenerPosition = true;
    // The same forward/up pair the listener already carries; +X for the default orientation.
    const Math::Vector3 listenerRight =
        ComputeAudioListenerRight(m_impl->listenerForward, m_impl->listenerUp);

    for (auto& [handleValue, state] : m_impl->sources) {
        // A streaming source is pumped once per frame, before its parameters are pushed, so the
        // ring is as full as the device will allow when the callback next runs.
        static_cast<void>(
            PumpStreamingSourceState(m_impl->audioDevice, state, VoiceHandle{handleValue}, m_impl->streamScratch));

        Math::Vector3 sourceVelocity{};
        if (stepSeconds > 0.0F && state.hasPreviousPosition) {
            sourceVelocity = (state.position - state.previousPosition) * (1.0F / stepSeconds);
        }
        state.previousPosition = state.position;
        state.hasPreviousPosition = true;

        // Effective, not authored: the source hears its bus composed with every ancestor, muted and
        // solo-silenced buses included. The flat case (one bus under Master) is unchanged because
        // Master contributes unity.
        const float groupVolume = m_impl->mixerGroups.GetEffectiveVolume(state.mixerGroup);
        const float groupPitch = m_impl->mixerGroups.GetEffectivePitch(state.mixerGroup);
        float attenuation = 1.0F;
        float pan = 0.0F;
        float dopplerPitch = 1.0F;
        float lowPassCutoffHz = 0.0F;
        if (state.desc.spatial) {
            const Math::Vector3 listenerDelta = state.position - m_impl->listenerPosition;
            const float distanceSquared = Math::LengthSquared(listenerDelta);
            if (!std::isfinite(distanceSquared) || distanceSquared < 0.0F) {
                attenuation = 0.0F;
            } else {
                const float distance = std::sqrt(distanceSquared);
                attenuation = std::isfinite(distance)
                    ? ComputeDistanceAttenuation(distance, state.desc.minDistance,
                                                     state.desc.maxDistance, state.desc.attenuationModel)
                    : 0.0F;
            }
            pan = ComputeAudioPan(listenerDelta, listenerRight);
            // The cone is measured from the emitter's axis to the direction it is being heard from;
            // its gain multiplies the distance attenuation rather than replacing it.
            const float coneAngle =
                ComputeAudioConeAngleDegrees(state.desc.coneDirection, -listenerDelta);
            attenuation *= ComputeAudioConeAttenuation(coneAngle, state.desc.coneInnerAngleDegrees,
                                                           state.desc.coneOuterAngleDegrees,
                                                           state.desc.coneOuterGain);
            dopplerPitch = ComputeAudioDopplerPitch(listenerVelocity, sourceVelocity, listenerDelta,
                                                        state.desc.dopplerFactor);

            // Occlusion: query at a bounded rate, smooth every frame, then fold the answer into the
            // attenuation the mixer already understands. A source with no provider, a 2D source or
            // one that opted out decays back to clear rather than freezing at its last blocked
            // value, so removing the provider mid-scene fades out instead of sticking.
            const bool occludable = m_impl->occlusionProvider != nullptr && state.desc.occlusionEnabled;
            if (occludable) {
                state.occlusionQueryTimer -= stepSeconds;
                if (state.occlusionQueryTimer <= 0.0F) {
                    AudioOcclusionQuery query;
                    query.listenerPosition = m_impl->listenerPosition;
                    query.sourcePosition = state.position;
                    query.listenerRight = listenerRight;
                    query.sourceEntity = state.desc.ownerEntity;
                    query.layerMask = state.desc.occlusionLayerMask;
                    const float answer = m_impl->occlusionProvider->QueryOcclusion(query);
                    state.occlusionTarget =
                        std::isfinite(answer) ? std::clamp(answer, 0.0F, 1.0F) : 0.0F;
                    state.occlusionQueryTimer = kAudioOcclusionQueryIntervalSeconds;
                }
            } else {
                state.occlusionTarget = 0.0F;
                state.occlusionQueryTimer = 0.0F;
            }
            state.occlusion = SmoothAudioOcclusion(state.occlusion, state.occlusionTarget, deltaSeconds);
            attenuation *= ComputeAudioOcclusionGain(state.occlusion, state.desc.occlusionGain);
            lowPassCutoffHz =
                ComputeAudioOcclusionCutoff(state.occlusion, state.desc.occlusionLowPassHz);
        }
        AudioMixParameters parameters;
        if (!EvaluateAudioMixParameters(state.volume, state.pitch * dopplerPitch, groupVolume, groupPitch,
                                            attenuation, parameters)) {
            UVE_ERROR("AudioSystem: invalid mix parameters for source {}", handleValue);
            parameters = AudioMixParameters{0.0F, 1.0F};
        }
        static_cast<void>(m_impl->audioDevice.SetVoiceParams(
            VoiceHandle{handleValue},
            AudioVoiceParams{state.position, parameters.gain, parameters.pitch, pan, lowPassCutoffHz}));
    }
}

} // namespace UVE::Audio
