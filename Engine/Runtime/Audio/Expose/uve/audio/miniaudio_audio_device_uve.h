// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "uve/audio/i_audio_device_uve.h"

namespace UVE::Audio {

/// MiniaudioAudioDeviceUVE is the engine's first real IAudioDeviceUVE backend (the second class
/// implementing the interface, after NullAudioDeviceUVE). It drives actual audio hardware through
/// the vendored single-header miniaudio library (details in Audio/CMakeLists.txt), while keeping
/// every miniaudio type confined to the .cpp — matching this codebase's established
/// third-party-header confinement discipline (this header, like IAudioDeviceUVE itself, names no
/// miniaudio symbol).
///
/// Construction goes through CreateUVE(): audio output can legitimately be unavailable at
/// runtime (headless machines, no sound card, driver hiccups), so CreateUVE returns nullptr after
/// logging the failure instead of throwing or half-initializing — callers fall back to
/// NullAudioDeviceUVE (see EngineCoreUVE's device selection). The null *testing* backend of
/// miniaudio itself can be forced through OptionsUVE, which is how the unit tests exercise the
/// full real-backend code path on machines without any audio hardware (including CI).
///
/// Playback behavior matches the IAudioDeviceUVE contract with one addition NullAudioDeviceUVE
/// does not have: a non-looping voice that reaches the end of its clip transitions itself to
/// VoicePlaybackStateUVE::Stopped (the null device only changes state through explicit
/// PlayUVE/StopUVE because it has no playback clock). Deliberate v1 scope decisions, all
/// documented at use sites: gain applies, pitch applies (fractional stepping with linear
/// interpolation), looping applies, position is stored but not spatialized (panning/attenuation
/// is computed above the RHI today — AudioSourceSystemUVE feeds distance-attenuated gain through
/// SetVoiceParamsUVE already), and mixing is a plain sum into the device buffer (no master
/// limiter stage yet).
/// Thread-safety: SetVoiceParamsUVE/PlayUVE/StopUVE may be called from the engine thread while
/// miniaudio's audio thread is running; parameter exchange is guarded by a short mutex. As with
/// the rest of the interface, prefer driving it from the main engine/audio thread.
class MiniaudioAudioDeviceUVE final : public IAudioDeviceUVE {
public:
    struct OptionsUVE {
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
    [[nodiscard]] static std::unique_ptr<MiniaudioAudioDeviceUVE> CreateUVE();
    [[nodiscard]] static std::unique_ptr<MiniaudioAudioDeviceUVE> CreateUVE(const OptionsUVE& options);

    ~MiniaudioAudioDeviceUVE() override;

    MiniaudioAudioDeviceUVE(const MiniaudioAudioDeviceUVE&) = delete;
    MiniaudioAudioDeviceUVE& operator=(const MiniaudioAudioDeviceUVE&) = delete;

    [[nodiscard]] VoiceHandleUVE CreateVoiceUVE(const AudioVoiceDescUVE& desc) override;
    void DestroyVoiceUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool PlayUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool StopUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool SetVoiceParamsUVE(VoiceHandleUVE voice, const AudioVoiceParamsUVE& params) override;
    [[nodiscard]] VoicePlaybackStateUVE GetVoiceStateUVE(VoiceHandleUVE voice) const override;
    [[nodiscard]] std::string_view GetBackendNameUVE() const noexcept override;

private:
    struct ImplUVE;
    explicit MiniaudioAudioDeviceUVE(std::unique_ptr<ImplUVE> impl) noexcept;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Audio
