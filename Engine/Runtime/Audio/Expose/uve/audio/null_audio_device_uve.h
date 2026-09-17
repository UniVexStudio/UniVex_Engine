// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "uve/audio/i_audio_device_uve.h"
#include "uve/audio/recorded_audio_call_uve.h"

namespace UVE::Audio {

/// NullAudioDeviceUVE is the silent IAudioDeviceUVE backend: it performs zero real audio output
/// and instead validates and bookkeeps every call, recording the exact sequence of
/// order-sensitive calls (Play/Stop/SetVoiceParams) a real backend would have received. Its two
/// roles: the call-recording test double AudioSystemUVE/AudioSourceSystemUVE unit tests assert
/// against, and the automatic fallback for machines with no usable output device (EngineCoreUVE
/// prefers MiniaudioAudioDeviceUVE - the real hardware backend - and swaps this in when audio
/// initialization legitimately fails, e.g. headless CI). Unlike a real backend it has no playback
/// clock: state changes only through explicit PlayUVE/StopUVE calls.
/// Thread-safety: not thread-safe. Every method is intended to be called only from the main
/// engine/audio thread, matching AudioSystemUVE's own single-threaded frame contract.
class NullAudioDeviceUVE final : public IAudioDeviceUVE {
public:
    NullAudioDeviceUVE();
    ~NullAudioDeviceUVE() override;

    NullAudioDeviceUVE(const NullAudioDeviceUVE&) = delete;
    NullAudioDeviceUVE& operator=(const NullAudioDeviceUVE&) = delete;

    [[nodiscard]] VoiceHandleUVE CreateVoiceUVE(const AudioVoiceDescUVE& desc) override;
    void DestroyVoiceUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool PlayUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool StopUVE(VoiceHandleUVE voice) override;
    [[nodiscard]] bool SetVoiceParamsUVE(VoiceHandleUVE voice, const AudioVoiceParamsUVE& params) override;
    [[nodiscard]] VoicePlaybackStateUVE GetVoiceStateUVE(VoiceHandleUVE voice) const override;
    [[nodiscard]] std::string_view GetBackendNameUVE() const noexcept override;

    /// Test-only hook (not part of IAudioDeviceUVE): every Play/Stop/SetVoiceParams call this
    /// device has recorded, in call order, since construction (or the last
    /// ClearRecordedCallsUVE()). There is no command-buffer-style "submit" boundary for audio —
    /// calls accumulate for the device's whole lifetime.
    [[nodiscard]] const std::vector<RecordedAudioCallUVE>& GetRecordedCallsUVE() const noexcept;

    /// Test-only hook: clears the recorded-call log without touching live-voice bookkeeping — lets
    /// a test isolate assertions to calls made after a known point.
    void ClearRecordedCallsUVE() noexcept;

    /// Test-only hook: how many voices are currently alive (created but not yet destroyed).
    [[nodiscard]] std::size_t GetLiveVoiceCountUVE() const noexcept;

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Audio
