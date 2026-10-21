// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <vector>

#include "uve/audio/audio_meter_uve.h"
#include "uve/audio/audio_mixer_group_uve.h"
#include "uve/audio/audio_occlusion_uve.h"
#include "uve/audio/audio_stream_uve.h"
#include "uve/audio/pcm_gain_effect_uve.h"
#include "uve/audio/wav_pcm16_decoder_uve.h"
#include "uve/audio/audio_source_desc_uve.h"
#include "uve/audio/voice_handle_uve.h"
#include "uve/audio/voice_playback_state_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Audio {

/// IAudioSystemUVE is the engine's top-level 3D positional audio orchestrator (the spec's
/// `AudioSystemUVE`, Part 7.6): owns the live source registry and the listener transform, and
/// drives IAudioDeviceUVE via computed (never raw) gain values. AudioSourceSystemUVE is the
/// ECS-facing caller; nothing below IAudioSystemUVE knows about entities or components.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main engine
/// thread, matching RenderSystemUVE's/InputSystemUVE's own single-threaded frame contract.
class IAudioSystemUVE {
public:
    virtual ~IAudioSystemUVE() = default;

    // --- Listener (the spec's AudioListenerUVE — "Attached to Camera3D by default"; see
    // EngineCoreUVE::LateUpdate() for how the active camera drives these each frame). ---
    virtual void SetListenerPositionUVE(Math::Vector3UVE position) = 0;
    virtual void SetListenerOrientationUVE(Math::Vector3UVE forward, Math::Vector3UVE up) = 0;
    [[nodiscard]] virtual Math::Vector3UVE GetListenerPositionUVE() const noexcept = 0;

    // --- Source lifecycle. ---
    /// Creates a source per `desc` (forwarding an AudioVoiceDescUVE subset to the underlying
    /// IAudioDeviceUVE). Never returns kInvalidVoiceHandleUVE on success.
    [[nodiscard]] virtual VoiceHandleUVE CreateSourceUVE(const AudioSourceDescUVE& desc) = 0;
    /// Destroys `source`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroySourceUVE(VoiceHandleUVE source) = 0;

    /// Starts (or restarts) `source`. Returns false (logging the reason) if `source` is unknown.
    [[nodiscard]] virtual bool PlayUVE(VoiceHandleUVE source) = 0;
    /// Stops `source`. Returns false (logging the reason) if `source` is unknown.
    [[nodiscard]] virtual bool StopUVE(VoiceHandleUVE source) = 0;
    /// `source`'s current playback state; Stopped (logged) if `source` is unknown.
    [[nodiscard]] virtual VoicePlaybackStateUVE GetSourceStateUVE(VoiceHandleUVE source) const = 0;

    // --- Per-source runtime params: void + logged on an unknown handle (not [[nodiscard]] bool) —
    // these are expected to be called once per entity per frame from AudioSourceSystemUVE's ECS
    // sync, where forcing a bool check at every call site would be excessive ceremony. ---
    virtual void SetSourcePositionUVE(VoiceHandleUVE source, Math::Vector3UVE position) = 0;
    virtual void SetSourceVolumeUVE(VoiceHandleUVE source, float volume) = 0;
    virtual void SetSourcePitchUVE(VoiceHandleUVE source, float pitch) = 0;

    // --- Bounded mixer bus graph and routing. ---
    /// Registers a bus under `parentName` (empty means Master). False on an invalid or duplicate
    /// name, an out-of-range multiplier, an unknown parent, or a full bus table.
    virtual bool RegisterMixerGroupUVE(std::string_view name, std::string_view parentName,
                                       float volumeMultiplier = 1.0F,
                                       float pitchMultiplier = 1.0F) = 0;
    /// Registers the bus directly under Master.
    virtual bool RegisterMixerGroupUVE(std::string_view name, float volumeMultiplier = 1.0F,
                                       float pitchMultiplier = 1.0F) = 0;
    /// Refuses Master, unknown buses, and buses that still have sources routed to them.
    virtual bool RemoveMixerGroupUVE(std::string_view name) = 0;
    /// Reparents a bus; refused when the move would create a cycle or either name is unknown.
    virtual bool SetMixerGroupParentUVE(std::string_view name, std::string_view parentName) = 0;
    virtual bool SetMixerGroupVolumeUVE(std::string_view name, float volumeMultiplier) = 0;
    virtual bool SetMixerGroupPitchUVE(std::string_view name, float pitchMultiplier) = 0;
    /// Silences a bus and every bus under it without disturbing the authored multipliers.
    virtual bool SetMixerGroupMutedUVE(std::string_view name, bool muted) = 0;
    /// While any bus is soloed, only its chain stays audible; see AudioMixerGroupUVE's own contract.
    virtual bool SetMixerGroupSoloedUVE(std::string_view name, bool soloed) = 0;
    /// Installs one typed DSP insert (gain, filter, dynamics, reverb) in a bus's fixed slot. This is
    /// configuration only: the description is validated and any memory it needs is taken here, so the
    /// audio path never allocates.
    virtual bool SetMixerInsertUVE(std::string_view name, std::size_t slot,
                                   const AudioInsertDescUVE& desc) = 0;
    virtual bool ClearMixerInsertUVE(std::string_view name, std::size_t slot) = 0;
    /// The device rate every bus builds its filter coefficients and reverb delays for.
    virtual bool SetMixerSampleRateUVE(float sampleRateHz) = 0;
    virtual bool SetSourceMixerGroupUVE(VoiceHandleUVE source, std::string_view name) = 0;
    [[nodiscard]] virtual AudioMixerDiagnosticsUVE GetMixerDiagnosticsUVE(
        std::size_t maximumGroups = kMaximumAudioMixerGroupsUVE) const = 0;

    // --- Caller-owned bounded stream/effect preparation. ---
    /// Resets one source's bounded PCM16 refill cursor. The source retains only cursor/scheduler
    /// state; decoded samples remain caller-owned and are never copied into AudioSystemUVE.
    [[nodiscard]] virtual bool ResetSourceStreamUVE(
        VoiceHandleUVE source, std::size_t totalSamples, bool loop, std::size_t cursorSample = 0U,
        std::size_t maximumSamples = kMaximumWavPcm16SamplesUVE) = 0;
    /// Plans and queues one bounded contiguous refill window, advancing only the source-owned cursor.
    [[nodiscard]] virtual bool ScheduleSourceStreamWindowUVE(VoiceHandleUVE source,
                                                               std::size_t requestedSamples) = 0;
    /// Pops the oldest queued refill plan for caller-owned decoding/publication.
    [[nodiscard]] virtual bool PopSourceStreamWindowUVE(VoiceHandleUVE source,
                                                         Pcm16StreamWindowPlanUVE& outPlan) = 0;
    /// Queues one bounded PCM gain window for the source's next caller-owned sample-buffer pass.
    [[nodiscard]] virtual bool ScheduleSourcePcmGainWindowUVE(
        VoiceHandleUVE source, const PcmGainEffectWindowUVE& window) = 0;
    /// Applies queued gain windows atomically to copied caller-owned samples and clears them only
    /// after successful output publication. No device submission is performed by this method.
    [[nodiscard]] virtual bool ApplySourcePcmGainEffectsUVE(
        VoiceHandleUVE source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) = 0;
    /// Runs copied caller-owned PCM through the bus chain of the bus `source` is routed to: the bus's
    /// effective volume (ancestors, mute and solo included) then its fixed-order inserts. This is the
    /// submix step; device submission and sample ownership stay with the caller.
    [[nodiscard]] virtual bool ApplySourceBusChainUVE(
        VoiceHandleUVE source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) = 0;

    // --- Streaming sources: bounded memory instead of a fully decoded clip. ---
    /// Creates a source whose samples arrive over time from `source` rather than from a decoded
    /// `.uvaudio` clip. `desc.audioAssetPath` is ignored - the stream owns the media - while every
    /// other field keeps its meaning: spatialisation, cone, Doppler, volume, pitch, looping and the
    /// mixer group all behave exactly as they do for a clip source.
    /// The source is borrowed, not owned: it must outlive the voice.
    [[nodiscard]] virtual VoiceHandleUVE CreateStreamingSourceUVE(const AudioSourceDescUVE& desc,
                                                                  IAudioStreamSourceUVE& source) = 0;
    /// Moves as many frames as the device ring will take from the source into the voice, returning
    /// how many were accepted. `UpdateUVE()` already pumps every streaming source once a frame; this
    /// exists for a caller that wants to pre-buffer before `PlayUVE()`, and for tests that drive the
    /// pump without a frame loop. Returns 0 for an unknown handle, a clip source, a full ring, or a
    /// stream that has ended.
    [[nodiscard]] virtual std::size_t PumpStreamingSourceUVE(VoiceHandleUVE source) = 0;
    /// Frames buffered in the device for `source` - the headroom a pre-buffer loop waits on and the
    /// number an underrun test watches. 0 for an unknown handle or a clip source.
    [[nodiscard]] virtual std::size_t GetStreamingSourceBufferedFramesUVE(VoiceHandleUVE source) const = 0;

    // --- Metering. Both reads are idempotent picks of the current reading: reading a source row and
    // the bus above it in the same frame shows the same measurement twice, not zero the second time.
    // ---

    /// What `source` contributed to the output, measured by the device after its gain, pan and
    /// muffling. Silence for an unknown source, a stopped one, or a backend that does not meter.
    [[nodiscard]] virtual AudioMeterReadingUVE GetSourceMeterUVE(VoiceHandleUVE source) const = 0;

    /// The level of one bus, assembled by power-summing every source routed to it or to any bus
    /// under it - the same set whose gain already carries this bus's effective volume. Unknown
    /// names, including a bus that exists but carries nothing, read silence.
    ///
    /// This is a level indication, not a bus-buffer sum: the device mixes straight to the output and
    /// never builds per-bus accumulators, so the number is the energy of the contributing voices
    /// rather than the samples of a summed bus. For uncorrelated sources - which is what a game's
    /// mix is - the two agree, and it costs no per-bus memory on the audio thread.
    [[nodiscard]] virtual AudioMeterReadingUVE GetMixerGroupMeterUVE(std::string_view name) const = 0;

    /// Installs (or clears, with nullptr) the occlusion provider. The pointer is borrowed, not
    /// owned, and a provider must outlive the system. With none installed every source stays clear,
    /// so an engine that never wires physics-driven occlusion behaves exactly as before.
    /// A provider is queried at most once per source per kAudioOcclusionQueryIntervalSecondsUVE and
    /// its answer is smoothed, so swapping one in never turns the mixer into a raycast loop.
    virtual void SetOcclusionProviderUVE(const IAudioOcclusionProviderUVE* provider) = 0;

    // --- Frame advance. ---
    /// Recomputes ComputeDistanceAttenuationUVE() for every live spatial source against the
    /// current listener position (non-spatial sources use `volume` directly), derives the stereo pan,
    /// the directional cone gain and the Doppler pitch shift, and pushes the resulting
    /// AudioVoiceParamsUVE to the underlying IAudioDeviceUVE via SetVoiceParamsUVE().
    /// Must be called once per frame, main-thread-only, after this frame's listener/source
    /// positions are settled. `deltaSeconds` is the frame delta used to turn position changes into
    /// velocities: 0 (or a non-finite value) disables Doppler for that frame, which is exactly what
    /// a caller that does not track time wants.
    virtual void UpdateUVE(float deltaSeconds) = 0;
};

} // namespace UVE::Audio
