// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_audio_clip_resolver.h"
#include "i_audio_device.h"
#include "i_audio_system.h"

namespace UVE::Audio {

/// AudioSystem is the concrete, engine-standard implementation of IAudioSystem. Takes its
/// IAudioDevice& by dependency injection (same shape as Render::RenderSystem's
/// IRenderDevice&, Input::InputSystem's IEventSystem&) — it never constructs or owns an
/// audio device itself, so tests and future backend swaps can pass in whichever IAudioDevice&
/// they need.
class AudioSystem final : public IAudioSystem {
public:
    explicit AudioSystem(IAudioDevice& audioDevice,
                            IAudioClipResolver* clipResolver = nullptr);
    ~AudioSystem() override;

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void SetListenerPosition(Math::Vector3 position) override;
    void SetListenerOrientation(Math::Vector3 forward, Math::Vector3 up) override;
    [[nodiscard]] Math::Vector3 GetListenerPosition() const noexcept override;

    [[nodiscard]] VoiceHandle CreateSource(const AudioSourceDesc& desc) override;
    void DestroySource(VoiceHandle source) override;
    [[nodiscard]] bool Play(VoiceHandle source) override;
    [[nodiscard]] bool Stop(VoiceHandle source) override;
    [[nodiscard]] VoicePlaybackState GetSourceState(VoiceHandle source) const override;

    void SetSourcePosition(VoiceHandle source, Math::Vector3 position) override;
    void SetSourceVolume(VoiceHandle source, float volume) override;
    void SetSourcePitch(VoiceHandle source, float pitch) override;

    bool RegisterMixerGroup(std::string_view name, std::string_view parentName,
                               float volumeMultiplier = 1.0F, float pitchMultiplier = 1.0F) override;
    bool RegisterMixerGroup(std::string_view name, float volumeMultiplier = 1.0F,
                               float pitchMultiplier = 1.0F) override;
    bool RemoveMixerGroup(std::string_view name) override;
    bool SetMixerGroupParent(std::string_view name, std::string_view parentName) override;
    bool SetMixerGroupVolume(std::string_view name, float volumeMultiplier) override;
    bool SetMixerGroupPitch(std::string_view name, float pitchMultiplier) override;
    bool SetMixerGroupMuted(std::string_view name, bool muted) override;
    bool SetMixerGroupSoloed(std::string_view name, bool soloed) override;
    bool SetMixerInsert(std::string_view name, std::size_t slot,
                           const AudioInsertDesc& desc) override;
    bool ClearMixerInsert(std::string_view name, std::size_t slot) override;
    bool SetMixerSampleRate(float sampleRateHz) override;
    bool SetSourceMixerGroup(VoiceHandle source, std::string_view name) override;
    [[nodiscard]] AudioMixerDiagnostics GetMixerDiagnostics(
        std::size_t maximumGroups = kMaximumAudioMixerGroups) const override;

    [[nodiscard]] bool ResetSourceStream(
        VoiceHandle source, std::size_t totalSamples, bool loop, std::size_t cursorSample = 0U,
        std::size_t maximumSamples = kMaximumWavPcm16Samples) override;
    [[nodiscard]] bool ScheduleSourceStreamWindow(VoiceHandle source,
                                                       std::size_t requestedSamples) override;
    [[nodiscard]] bool PopSourceStreamWindow(VoiceHandle source,
                                                 Pcm16StreamWindowPlan& outPlan) override;
    [[nodiscard]] bool ScheduleSourcePcmGainWindow(
        VoiceHandle source, const PcmGainEffectWindow& window) override;
    [[nodiscard]] bool ApplySourcePcmGainEffects(
        VoiceHandle source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) override;
    [[nodiscard]] bool ApplySourceBusChain(
        VoiceHandle source, const std::vector<float>& inputSamples,
        std::vector<float>& outputSamples) override;

    [[nodiscard]] VoiceHandle CreateStreamingSource(const AudioSourceDesc& desc,
                                                          IAudioStreamSource& source) override;
    [[nodiscard]] std::size_t PumpStreamingSource(VoiceHandle source) override;
    [[nodiscard]] std::size_t GetStreamingSourceBufferedFrames(VoiceHandle source) const override;

    [[nodiscard]] AudioMeterReading GetSourceMeter(VoiceHandle source) const override;
    [[nodiscard]] AudioMeterReading GetMixerGroupMeter(std::string_view name) const override;


    void SetOcclusionProvider(const IAudioOcclusionProvider* provider) override;

    void Update(float deltaSeconds) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Audio
