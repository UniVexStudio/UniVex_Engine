// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/null_audio_device_uve.h"

#include <algorithm>
#include <utility>

#include "uve/containers/handle_table_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Audio {

struct NullAudioDeviceUVE::ImplUVE {
    /// One voice slot. A streaming voice additionally owns a bounded ring; a clip voice leaves it
    /// unconfigured, which is exactly what every stream entry point checks for.
    struct VoiceUVE final {
        VoicePlaybackStateUVE state = VoicePlaybackStateUVE::Stopped;
        AudioStreamRingBufferUVE stream;
        bool endRequested = false;
        AudioMeterReadingUVE meter{};
        [[nodiscard]] bool IsStreamingUVE() const noexcept { return stream.IsConfiguredUVE(); }
    };

    Containers::HandleTableUVE<VoiceUVE, VoiceHandleUVE> voices;
    std::vector<RecordedAudioCallUVE> recordedCalls;
    /// Reused so draining never allocates: the null device stands in for a callback that must not.
    std::vector<float> drainScratch;
};

NullAudioDeviceUVE::NullAudioDeviceUVE() : m_impl(std::make_unique<ImplUVE>()) {}

NullAudioDeviceUVE::~NullAudioDeviceUVE() = default;

AudioMeterReadingUVE NullAudioDeviceUVE::ReadVoiceMeterUVE(const VoiceHandleUVE voice) const {
    const ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    return entry != nullptr ? entry->meter : kSilentAudioMeterUVE;
}

void NullAudioDeviceUVE::SetVoiceMeterUVE(const VoiceHandleUVE voice, const AudioMeterReadingUVE reading) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry != nullptr) {
        entry->meter = reading;
    }
}

VoiceHandleUVE NullAudioDeviceUVE::CreateVoiceUVE(const AudioVoiceDescUVE& desc) {
    static_cast<void>(desc); // NullAudioDeviceUVE performs no real audio output, bookkeeping only.
    return m_impl->voices.AcquireUVE();
}

void NullAudioDeviceUVE::DestroyVoiceUVE(VoiceHandleUVE voice) {
    if (!m_impl->voices.ReleaseUVE(voice)) {
        UVE_ERROR("NullAudioDeviceUVE: DestroyVoiceUVE called with an unknown or already-destroyed handle ({})",
                   voice.value);
    }
}

bool NullAudioDeviceUVE::PlayUVE(VoiceHandleUVE voice) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: PlayUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    entry->state = VoicePlaybackStateUVE::Playing;
    m_impl->recordedCalls.emplace_back(PlayVoiceCallUVE{voice});
    return true;
}

bool NullAudioDeviceUVE::StopUVE(VoiceHandleUVE voice) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: StopUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    entry->state = VoicePlaybackStateUVE::Stopped;
    m_impl->recordedCalls.emplace_back(StopVoiceCallUVE{voice});
    return true;
}

bool NullAudioDeviceUVE::SetVoiceParamsUVE(VoiceHandleUVE voice, const AudioVoiceParamsUVE& params) {
    if (!m_impl->voices.ContainsUVE(voice)) {
        UVE_ERROR("NullAudioDeviceUVE: SetVoiceParamsUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    if (!ValidateAudioVoiceParamsUVE(params)) {
        UVE_ERROR("NullAudioDeviceUVE: SetVoiceParamsUVE received invalid parameters for handle ({})", voice.value);
        return false;
    }
    m_impl->recordedCalls.emplace_back(SetVoiceParamsCallUVE{voice, params});
    return true;
}

VoicePlaybackStateUVE NullAudioDeviceUVE::GetVoiceStateUVE(VoiceHandleUVE voice) const {
    const ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: GetVoiceStateUVE called with an unknown handle ({})", voice.value);
        return VoicePlaybackStateUVE::Stopped;
    }
    return entry->state;
}

VoiceHandleUVE NullAudioDeviceUVE::CreateStreamVoiceUVE(const AudioStreamFormatUVE& format) {
    if (!IsAudioStreamFormatValidUVE(format)) {
        UVE_ERROR("NullAudioDeviceUVE: CreateStreamVoiceUVE rejected an invalid format ({} Hz, {} channels)",
                  format.sampleRate, format.channels);
        return kInvalidVoiceHandleUVE;
    }
    const VoiceHandleUVE voice = m_impl->voices.AcquireUVE();
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry != nullptr) {
        entry->stream.ConfigureUVE(kAudioStreamRingFramesUVE, format.channels);
    }
    return voice;
}

std::size_t NullAudioDeviceUVE::PushStreamFramesUVE(const VoiceHandleUVE voice,
                                                    const std::span<const float> interleavedFrames) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: PushStreamFramesUVE called with an unknown handle ({})", voice.value);
        return 0U;
    }
    if (!entry->IsStreamingUVE()) {
        UVE_ERROR("NullAudioDeviceUVE: PushStreamFramesUVE called on a clip voice ({})", voice.value);
        return 0U;
    }
    if (entry->endRequested) {
        UVE_ERROR("NullAudioDeviceUVE: PushStreamFramesUVE called after EndStreamUVE ({})", voice.value);
        return 0U;
    }
    const std::size_t channels = static_cast<std::size_t>(entry->stream.GetChannelsUVE());
    if (channels == 0U || interleavedFrames.size() % channels != 0U) {
        UVE_ERROR("NullAudioDeviceUVE: PushStreamFramesUVE received {} samples, not a whole number of frames",
                  interleavedFrames.size());
        return 0U;
    }
    return entry->stream.WriteUVE(interleavedFrames.data(), interleavedFrames.size() / channels);
}

bool NullAudioDeviceUVE::EndStreamUVE(const VoiceHandleUVE voice) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr || !entry->IsStreamingUVE()) {
        UVE_ERROR("NullAudioDeviceUVE: EndStreamUVE called with an unknown handle or a clip voice ({})",
                  voice.value);
        return false;
    }
    entry->endRequested = true;
    if (entry->stream.GetBufferedFramesUVE() == 0U) {
        entry->state = VoicePlaybackStateUVE::Stopped;
    }
    return true;
}

std::size_t NullAudioDeviceUVE::GetStreamBufferedFramesUVE(const VoiceHandleUVE voice) const {
    const ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    return entry != nullptr && entry->IsStreamingUVE() ? entry->stream.GetBufferedFramesUVE() : 0U;
}

std::size_t NullAudioDeviceUVE::GetStreamFreeFramesUVE(const VoiceHandleUVE voice) const {
    const ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    return entry != nullptr && entry->IsStreamingUVE() ? entry->stream.GetFreeFramesUVE() : 0U;
}

void NullAudioDeviceUVE::ConsumeStreamFramesUVE(const VoiceHandleUVE voice, const std::size_t frames) {
    ImplUVE::VoiceUVE* const entry = m_impl->voices.FindUVE(voice);
    if (entry == nullptr || !entry->IsStreamingUVE() || frames == 0U) {
        return;
    }
    m_impl->drainScratch.resize(std::min(frames, entry->stream.GetBufferedFramesUVE()) *
                                static_cast<std::size_t>(entry->stream.GetChannelsUVE()));
    static_cast<void>(entry->stream.ReadUVE(m_impl->drainScratch.data(), frames));
    if (entry->endRequested && entry->stream.GetBufferedFramesUVE() == 0U) {
        entry->state = VoicePlaybackStateUVE::Stopped;
    }
}

std::string_view NullAudioDeviceUVE::GetBackendNameUVE() const noexcept {
    return "Null";
}

const std::vector<RecordedAudioCallUVE>& NullAudioDeviceUVE::GetRecordedCallsUVE() const noexcept {
    return m_impl->recordedCalls;
}

void NullAudioDeviceUVE::ClearRecordedCallsUVE() noexcept {
    m_impl->recordedCalls.clear();
}

std::size_t NullAudioDeviceUVE::GetLiveVoiceCountUVE() const noexcept {
    return m_impl->voices.GetLiveCountUVE();
}

} // namespace UVE::Audio
