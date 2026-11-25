// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <vector>

#include "audio_meter.h"
#include "audio_mixer_group.h"
#include "audio_occlusion.h"
#include "audio_stream.h"
#include "pcm_gain_effect.h"
#include "wav_pcm16_decoder.h"
#include "audio_source_desc.h"
#include "voice_handle.h"
#include "voice_playback_state.h"
#include "vector3.h"

namespace UVE::Audio {

/// IAudioSystem is the engine's top-level 3D positional audio orchestrator (the spec's
/// `AudioSystem`, Part 7.6): owns the live source registry and the listener transform, and
/// drives IAudioDevice via computed (never raw) gain values. AudioSourceSystem is the
/// ECS-facing caller; nothing below IAudioSystem knows about entities or components.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main engine
/// thread, matching RenderSystem's/InputSystem's own single-threaded frame contract.
class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    // --- Listener (the spec's AudioListener — "Attached to Camera3D by default"; see
    // EngineCore::LateUpdate() for how the active camera drives these each frame). ---
    virtual void SetListenerPosition(Math::Vector3 position) = 0;
    virtual void SetListenerOrientation(Math::Vector3 forward, Math::Vector3 up) = 0;
    [[nodiscard]] virtual Math::Vector3 GetListenerPosition() const noexcept = 0;

    // --- Source lifecycle. ---
    /// Creates a source per `desc` (forwarding an AudioVoiceDesc subset to the underlying
    /// IAudioDevice). Never returns kInvalidVoiceHandle on success.
    [[nodiscard]] virtual VoiceHandle CreateSource(const AudioSourceDesc& desc) = 0;
    /// Destroys `source`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroySource(VoiceHandle source) = 0;

    /// Starts (or restarts) `source`. Returns false (logging the reason) if `source` is unknown.
    [[nodiscard]] virtual bool Play(VoiceHandle source) = 0;
    /// Stops `source`. Returns false (logging the reason) if `source` is unknown.
    [[nodiscard]] virtual bool Stop(VoiceHandle source) = 0;
    /// `source`'s current playback state; Stopped (logged) if `source` is unknown.
    [[nodiscard]] virtual VoicePlaybackState GetSourceState(VoiceHandle source) const = 0;

    // --- Per-source runtime params: void + logged on an unknown handle (not [[nodiscard]] bool) —
    // these are expected to be called once per entity per frame from AudioSourceSystem's ECS
    // sync, where forcing a bool check at every call site would be excessive ceremony. ---
    virtual void SetSourcePosition(VoiceHandle source, Math::Vector3 position) = 0;
    virtual void SetSourceVolume(VoiceHandle source, float volume) = 0;
    virtual void SetSourcePitch(VoiceHandle source, float pitch) = 0;

    // --- Bounded mixer bus graph and routing. ---
    /// Registers a bus under `parentName` (empty means Master). False on an invalid or duplicate
    /// name, an out-of-range multiplier, an unknown parent, or a full bus table.
    virtual bool RegisterMixerGroup(std::string_view name, std::string_view parentName,
                                       float volumeMultiplier = 1.0F,
                                       float pitchMultiplier = 1.0F) = 0;
    /// Registers the bus directly under Master.
    virtual bool RegisterMixerGroup(std::string_view name, float volumeMultiplier = 1.0F,
                                       float pitchMultiplier = 1.0F) = 0;
    /// Refuses Master, unknown buses, and buses that still have sources routed to them.
    virtual bool RemoveMixerGroup(std::string_view name) = 0;
    /// Reparents a bus; refused when the move would create a cycle or either name is unknown.
    virtual bool SetMixerGroupParent(std::string_view name, std::string_view parentName) = 0;
    virtual bool SetMixerGroupVolume(std::string_view name, float volumeMultiplier) = 0;
    virtual bool SetMixerGroupPitch(std::string_view name, float pitchMultiplier) = 0;
    /// Silences a bus and every bus under it without disturbing the authored multipliers.
    virtual bool SetMixerGroupMuted(std::string_view name, bool muted) = 0;
    /// While any bus is soloed, only its chain stays audible; see AudioMixerGroup's own contract.
    virtual bool SetMixerGroupSoloed(std::string_view name, bool soloed) = 0;
    /// Installs one typed DSP insert (gain, filter, dynamics, reverb) in a bus's fixed slot. This is
    /// configuration only: the description is validated and any memory it needs is taken here, so the
    /// audio path never allocates.
    virtual bool SetMixerInsert(std::string_view name, std::size_t slot,
                                   const AudioInsertDesc& desc) = 0;
    virtual bool ClearMixerInsert(std::string_view name, std::size_t slot) = 0;
    /// The device rate every bus builds its filter coefficients and reverb delays for.
    virtual bool SetMixerSampleRate(float sampleRateHz) = 0;
    virtual bool SetSourceMixerGroup(VoiceHandle source, std::string_view name) = 0;
    [[nodiscard]] virtual AudioMixerDiagnostics GetMixerDiagnostics(
        std::size_t maximumGroups = kMaximumAudioMixerGroups) const = 0;

    // --- Caller-owned bounded stream/effect preparation. ---
    /// Resets one source's bounded PCM16 refill cursor. The source retains only cursor/scheduler
    /// state; decoded samples remain caller-owned and are never copied into AudioSystem.
    [[nodiscard]] virtual bool ResetSourceStream(
        VoiceHandle source, std::size_t totalSamples, bool loop, std::size_t cursorSample = 0U,
        std::size_t maximumSamples = kMaximumWavPcm16Samples) = 0;
    /// Plans and queues one bounded contiguous refill window, advancing only the source-owned cursor.
    [[nodiscard]] virtual bool ScheduleSourceStreamWindow(VoiceHandle source,
                                                               std::size_t requestedSamples) = 0;
    /// Pops the oldest queued refill plan for caller-owned decoding/publication.
    [[nodiscard]] virtual bool PopSourceStreamWindow(VoiceHandle source,
                                                         Pcm16StreamWindowPlan& outPlan) = 0;
    /// Queues one bounded PCM gain window for the source's next caller-owned sample-buffer pass.
    [[nodiscard]] virtual bool ScheduleSourcePcmGainWindow(
        VoiceHandle source, const PcmGainEffectWindow& window) = 0;
    /// Applies queued gain windows atomically to copied caller-owned samples and clears them only
    /// after successful output publication. No device submission is performed by this method.
    [[nodiscard]] virtual bool ApplySourcePcmGainEffects(
        VoiceHandle source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) = 0;
    /// Runs copied caller-owned PCM through the bus chain of the bus `source` is routed to: the bus's
    /// effective volume (ancestors, mute and solo included) then its fixed-order inserts. This is the
    /// submix step; device submission and sample ownership stay with the caller.
    [[nodiscard]] virtual bool ApplySourceBusChain(
        VoiceHandle source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) = 0;

    // --- Streaming sources: bounded memory instead of a fully decoded clip. ---
    /// Creates a source whose samples arrive over time from `source` rather than from a decoded
    /// `.uvaudio` clip. `desc.audioAssetPath` is ignored - the stream owns the media - while every
    /// other field keeps its meaning: spatialisation, cone, Doppler, volume, pitch, looping and the
    /// mixer group all behave exactly as they do for a clip source.
    /// The source is borrowed, not owned: it must outlive the voice.
    [[nodiscard]] virtual VoiceHandle CreateStreamingSource(const AudioSourceDesc& desc,
                                                                  IAudioStreamSource& source) = 0;
    /// Moves as many frames as the device ring will take from the source into the voice, returning
    /// how many were accepted. `Update()` already pumps every streaming source once a frame; this
    /// exists for a caller that wants to pre-buffer before `Play()`, and for tests that drive the
    /// pump without a frame loop. Returns 0 for an unknown handle, a clip source, a full ring, or a
    /// stream that has ended.
    [[nodiscard]] virtual std::size_t PumpStreamingSource(VoiceHandle source) = 0;
    /// Frames buffered in the device for `source` - the headroom a pre-buffer loop waits on and the
    /// number an underrun test watches. 0 for an unknown handle or a clip source.
    [[nodiscard]] virtual std::size_t GetStreamingSourceBufferedFrames(VoiceHandle source) const = 0;

    // --- Metering. Both reads are idempotent picks of the current reading: reading a source row and
    // the bus above it in the same frame shows the same measurement twice, not zero the second time.
    // ---

    /// What `source` contributed to the output, measured by the device after its gain, pan and
    /// muffling. Silence for an unknown source, a stopped one, or a backend that does not meter.
    [[nodiscard]] virtual AudioMeterReading GetSourceMeter(VoiceHandle source) const = 0;

    /// The level of one bus, assembled by power-summing every source routed to it or to any bus
    /// under it - the same set whose gain already carries this bus's effective volume. Unknown
    /// names, including a bus that exists but carries nothing, read silence.
    ///
    /// This is a level indication, not a bus-buffer sum: the device mixes straight to the output and
    /// never builds per-bus accumulators, so the number is the energy of the contributing voices
    /// rather than the samples of a summed bus. For uncorrelated sources - which is what a game's
    /// mix is - the two agree, and it costs no per-bus memory on the audio thread.
    [[nodiscard]] virtual AudioMeterReading GetMixerGroupMeter(std::string_view name) const = 0;

    /// Installs (or clears, with nullptr) the occlusion provider. The pointer is borrowed, not
    /// owned, and a provider must outlive the system. With none installed every source stays clear,
    /// so an engine that never wires physics-driven occlusion behaves exactly as before.
    /// A provider is queried at most once per source per kAudioOcclusionQueryIntervalSeconds and
    /// its answer is smoothed, so swapping one in never turns the mixer into a raycast loop.
    virtual void SetOcclusionProvider(const IAudioOcclusionProvider* provider) = 0;

    // --- Frame advance. ---
    /// Recomputes ComputeDistanceAttenuation() for every live spatial source against the
    /// current listener position (non-spatial sources use `volume` directly), derives the stereo pan,
    /// the directional cone gain and the Doppler pitch shift, and pushes the resulting
    /// AudioVoiceParams to the underlying IAudioDevice via SetVoiceParams().
    /// Must be called once per frame, main-thread-only, after this frame's listener/source
    /// positions are settled. `deltaSeconds` is the frame delta used to turn position changes into
    /// velocities: 0 (or a non-finite value) disables Doppler for that frame, which is exactly what
    /// a caller that does not track time wants.
    virtual void Update(float deltaSeconds) = 0;
};

} // namespace UVE::Audio
