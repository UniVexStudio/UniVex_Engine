// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "i_audio_device.h"

namespace UVE::Audio {

/// MiniaudioAudioDevice is the engine's first real IAudioDevice backend (the second class
/// implementing the interface, after NullAudioDevice). It drives actual audio hardware through
/// the vendored single-header miniaudio library (details in Audio/CMakeLists.txt), while keeping
/// every miniaudio type confined to the .cpp — matching this codebase's established
/// third-party-header confinement discipline (this header, like IAudioDevice itself, names no
/// miniaudio symbol).
///
/// Construction goes through Create(): audio output can legitimately be unavailable at
/// runtime (headless machines, no sound card, driver hiccups), so Create returns nullptr after
/// logging the failure instead of throwing or half-initializing — callers fall back to
/// NullAudioDevice (see EngineCore's device selection). The null *testing* backend of
/// miniaudio itself can be forced through Options, which is how the unit tests exercise the
/// full real-backend code path on machines without any audio hardware (including CI).
///
/// Playback behavior matches the IAudioDevice contract with one addition NullAudioDevice
/// does not have: a non-looping voice that reaches the end of its clip transitions itself to
/// VoicePlaybackState::Stopped (the null device only changes state through explicit
/// Play/Stop because it has no playback clock). Deliberate v1 scope decisions, all
/// documented at use sites: gain applies, pitch applies (fractional stepping with linear
/// interpolation), looping applies, position is stored but not spatialized (panning/attenuation
/// is computed above the RHI today — AudioSourceSystem feeds distance-attenuated gain through
/// SetVoiceParams already), and mixing is a plain sum into the device buffer (no master
/// limiter stage yet).
/// Thread-safety: SetVoiceParams/Play/Stop may be called from the engine thread while
/// miniaudio's audio thread is running; parameter exchange is guarded by a short mutex. As with
/// the rest of the interface, prefer driving it from the main engine/audio thread.
class MiniaudioAudioDevice final : public IAudioDevice {
public:
    struct Options {
        /// When true, miniaudio's own timer-driven null backend is forced instead of probing real
        /// output devices. For tests and headless CI only — the full device/mixer/callback code
        /// path still runs, it just consumes the mixed audio instead of routing it to hardware.
        bool useNullBackendForTesting = false;
        /// Output mix sample rate requested from the device. 48 kHz is the desktop default.
        std::uint32_t outputSampleRate = 48000U;
    };

    /// Creates and fully initializes a device (context + output device + started stream).
    /// Returns nullptr, logging the reason, when no usable output device exists.
    /// (Overload pair, not a default argument: a nested struct with default member initializers
    /// is not usable in a default argument on a member declared inside the enclosing class.)
    [[nodiscard]] static std::unique_ptr<MiniaudioAudioDevice> Create();
    [[nodiscard]] static std::unique_ptr<MiniaudioAudioDevice> Create(const Options& options);

    ~MiniaudioAudioDevice() override;

    MiniaudioAudioDevice(const MiniaudioAudioDevice&) = delete;
    MiniaudioAudioDevice& operator=(const MiniaudioAudioDevice&) = delete;

    [[nodiscard]] VoiceHandle CreateVoice(const AudioVoiceDesc& desc) override;
    void DestroyVoice(VoiceHandle voice) override;
    [[nodiscard]] bool Play(VoiceHandle voice) override;
    [[nodiscard]] bool Stop(VoiceHandle voice) override;
    [[nodiscard]] bool SetVoiceParams(VoiceHandle voice, const AudioVoiceParams& params) override;
    [[nodiscard]] VoicePlaybackState GetVoiceState(VoiceHandle voice) const override;
    /// Reads the voice's measured contribution to the output, updated on the audio thread once per
    /// mixed block. Takes the same voices mutex every other voice accessor takes; an unknown voice
    /// reads silence.
    [[nodiscard]] AudioMeterReading ReadVoiceMeter(VoiceHandle voice) const override;

    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    // Streaming: the ring is consumed by miniaudio's callback and filled from the engine thread,
    // both under the same short mutex that already guards voice parameters.
    [[nodiscard]] VoiceHandle CreateStreamVoice(const AudioStreamFormat& format) override;
    [[nodiscard]] std::size_t PushStreamFrames(VoiceHandle voice,
                                                  std::span<const float> interleavedFrames) override;
    [[nodiscard]] bool EndStream(VoiceHandle voice) override;
    [[nodiscard]] std::size_t GetStreamBufferedFrames(VoiceHandle voice) const override;
    [[nodiscard]] std::size_t GetStreamFreeFrames(VoiceHandle voice) const override;

private:
    struct Impl;
    explicit MiniaudioAudioDevice(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Audio
