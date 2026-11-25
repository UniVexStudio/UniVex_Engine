// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "i_audio_device.h"
#include "recorded_audio_call.h"

namespace UVE::Audio {

/// NullAudioDevice is the silent IAudioDevice backend: it performs zero real audio output
/// and instead validates and bookkeeps every call, recording the exact sequence of
/// order-sensitive calls (Play/Stop/SetVoiceParams) a real backend would have received. Its two
/// roles: the call-recording test double AudioSystem/AudioSourceSystem unit tests assert
/// against, and the automatic fallback for machines with no usable output device (EngineCore
/// prefers MiniaudioAudioDevice - the real hardware backend - and swaps this in when audio
/// initialization legitimately fails, e.g. headless CI). Unlike a real backend it has no playback
/// clock: state changes only through explicit Play/Stop calls.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main
/// engine/audio thread, matching AudioSystem's own single-threaded frame contract.
class NullAudioDevice final : public IAudioDevice {
public:
    NullAudioDevice();
    ~NullAudioDevice() override;

    NullAudioDevice(const NullAudioDevice&) = delete;
    NullAudioDevice& operator=(const NullAudioDevice&) = delete;

    [[nodiscard]] VoiceHandle CreateVoice(const AudioVoiceDesc& desc) override;
    void DestroyVoice(VoiceHandle voice) override;
    [[nodiscard]] bool Play(VoiceHandle voice) override;
    [[nodiscard]] bool Stop(VoiceHandle voice) override;
    [[nodiscard]] bool SetVoiceParams(VoiceHandle voice, const AudioVoiceParams& params) override;
    [[nodiscard]] VoicePlaybackState GetVoiceState(VoiceHandle voice) const override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    [[nodiscard]] VoiceHandle CreateStreamVoice(const AudioStreamFormat& format) override;
    [[nodiscard]] std::size_t PushStreamFrames(VoiceHandle voice,
                                                  std::span<const float> interleavedFrames) override;
    [[nodiscard]] bool EndStream(VoiceHandle voice) override;
    [[nodiscard]] std::size_t GetStreamBufferedFrames(VoiceHandle voice) const override;
    [[nodiscard]] std::size_t GetStreamFreeFrames(VoiceHandle voice) const override;

    /// Test-only hook (not part of IAudioDevice): drops `frames` from a streaming voice's ring,
    /// standing in for a real backend's audio callback so tests can drive the pump without a sound
    /// card. A null device has no playback clock, so consuming on demand is stated here rather than
    /// pretended in the interface. Stops the voice once an ended stream has fully drained.
    void ConsumeStreamFrames(VoiceHandle voice, std::size_t frames);

    /// Test-only hook (not part of IAudioDevice): every Play/Stop/SetVoiceParams call this
    /// device has recorded, in call order, since construction (or the last
    /// ClearRecordedCalls()). There is no command-buffer-style "submit" boundary for audio —
    /// calls accumulate for the device's whole lifetime.
    [[nodiscard]] const std::vector<RecordedAudioCall>& GetRecordedCalls() const noexcept;

    /// Test-only hook: clears the recorded-call log without touching live-voice bookkeeping — lets
    /// a test isolate assertions to calls made after a known point.
    void ClearRecordedCalls() noexcept;

    /// Test-only hook: how many voices are currently alive (created but not yet destroyed).
    [[nodiscard]] std::size_t GetLiveVoiceCount() const noexcept;

    /// Reads the meter this device was told to report for `voice`, which is silence unless
    /// SetVoiceMeter() states otherwise. The null backend has no output, so silence is its
    /// honest reading; this keeps the interface's own contract rather than special-casing it.
    [[nodiscard]] AudioMeterReading ReadVoiceMeter(VoiceHandle voice) const override;

    /// Test/headless hook (not part of IAudioDevice): states what a voice's meter reports.
    /// Headless editor and system tests run on this backend, so without it a mixer panel could only
    /// ever be exercised against real hardware.
    void SetVoiceMeter(VoiceHandle voice, AudioMeterReading reading);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Audio
