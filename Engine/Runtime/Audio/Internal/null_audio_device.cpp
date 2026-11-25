// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_audio_device.h"

#include <algorithm>
#include <utility>

#include "handle_table.h"
#include "logging_macros.h"

namespace UVE::Audio {

struct NullAudioDevice::Impl {
    /// One voice slot. A streaming voice additionally owns a bounded ring; a clip voice leaves it
    /// unconfigured, which is exactly what every stream entry point checks for.
    struct Voice final {
        VoicePlaybackState state = VoicePlaybackState::Stopped;
        AudioStreamRingBuffer stream;
        bool endRequested = false;
        AudioMeterReading meter{};
        [[nodiscard]] bool IsStreaming() const noexcept { return stream.IsConfigured(); }
    };

    Containers::HandleTable<Voice, VoiceHandle> voices;
    std::vector<RecordedAudioCall> recordedCalls;
    /// Reused so draining never allocates: the null device stands in for a callback that must not.
    std::vector<float> drainScratch;
};

NullAudioDevice::NullAudioDevice() : m_impl(std::make_unique<Impl>()) {}

NullAudioDevice::~NullAudioDevice() = default;

AudioMeterReading NullAudioDevice::ReadVoiceMeter(const VoiceHandle voice) const {
    const Impl::Voice* const entry = m_impl->voices.Find(voice);
    return entry != nullptr ? entry->meter : kSilentAudioMeter;
}

void NullAudioDevice::SetVoiceMeter(const VoiceHandle voice, const AudioMeterReading reading) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry != nullptr) {
        entry->meter = reading;
    }
}

VoiceHandle NullAudioDevice::CreateVoice(const AudioVoiceDesc& desc) {
    static_cast<void>(desc); // NullAudioDevice performs no real audio output, bookkeeping only.
    return m_impl->voices.Acquire();
}

void NullAudioDevice::DestroyVoice(VoiceHandle voice) {
    if (!m_impl->voices.Release(voice)) {
        UVE_ERROR("NullAudioDevice: DestroyVoice called with an unknown or already-destroyed handle ({})",
                   voice.value);
    }
}

bool NullAudioDevice::Play(VoiceHandle voice) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDevice: Play called with an unknown handle ({})", voice.value);
        return false;
    }
    entry->state = VoicePlaybackState::Playing;
    m_impl->recordedCalls.emplace_back(PlayVoiceCall{voice});
    return true;
}

bool NullAudioDevice::Stop(VoiceHandle voice) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDevice: Stop called with an unknown handle ({})", voice.value);
        return false;
    }
    entry->state = VoicePlaybackState::Stopped;
    m_impl->recordedCalls.emplace_back(StopVoiceCall{voice});
    return true;
}

bool NullAudioDevice::SetVoiceParams(VoiceHandle voice, const AudioVoiceParams& params) {
    if (!m_impl->voices.Contains(voice)) {
        UVE_ERROR("NullAudioDevice: SetVoiceParams called with an unknown handle ({})", voice.value);
        return false;
    }
    if (!ValidateAudioVoiceParams(params)) {
        UVE_ERROR("NullAudioDevice: SetVoiceParams received invalid parameters for handle ({})", voice.value);
        return false;
    }
    m_impl->recordedCalls.emplace_back(SetVoiceParamsCall{voice, params});
    return true;
}

VoicePlaybackState NullAudioDevice::GetVoiceState(VoiceHandle voice) const {
    const Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDevice: GetVoiceState called with an unknown handle ({})", voice.value);
        return VoicePlaybackState::Stopped;
    }
    return entry->state;
}

VoiceHandle NullAudioDevice::CreateStreamVoice(const AudioStreamFormat& format) {
    if (!IsAudioStreamFormatValid(format)) {
        UVE_ERROR("NullAudioDevice: CreateStreamVoice rejected an invalid format ({} Hz, {} channels)",
                  format.sampleRate, format.channels);
        return kInvalidVoiceHandle;
    }
    const VoiceHandle voice = m_impl->voices.Acquire();
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry != nullptr) {
        entry->stream.Configure(kAudioStreamRingFrames, format.channels);
    }
    return voice;
}

std::size_t NullAudioDevice::PushStreamFrames(const VoiceHandle voice,
                                                    const std::span<const float> interleavedFrames) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("NullAudioDevice: PushStreamFrames called with an unknown handle ({})", voice.value);
        return 0U;
    }
    if (!entry->IsStreaming()) {
        UVE_ERROR("NullAudioDevice: PushStreamFrames called on a clip voice ({})", voice.value);
        return 0U;
    }
    if (entry->endRequested) {
        UVE_ERROR("NullAudioDevice: PushStreamFrames called after EndStream ({})", voice.value);
        return 0U;
    }
    const std::size_t channels = static_cast<std::size_t>(entry->stream.GetChannels());
    if (channels == 0U || interleavedFrames.size() % channels != 0U) {
        UVE_ERROR("NullAudioDevice: PushStreamFrames received {} samples, not a whole number of frames",
                  interleavedFrames.size());
        return 0U;
    }
    return entry->stream.Write(interleavedFrames.data(), interleavedFrames.size() / channels);
}

bool NullAudioDevice::EndStream(const VoiceHandle voice) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr || !entry->IsStreaming()) {
        UVE_ERROR("NullAudioDevice: EndStream called with an unknown handle or a clip voice ({})",
                  voice.value);
        return false;
    }
    entry->endRequested = true;
    if (entry->stream.GetBufferedFrames() == 0U) {
        entry->state = VoicePlaybackState::Stopped;
    }
    return true;
}

std::size_t NullAudioDevice::GetStreamBufferedFrames(const VoiceHandle voice) const {
    const Impl::Voice* const entry = m_impl->voices.Find(voice);
    return entry != nullptr && entry->IsStreaming() ? entry->stream.GetBufferedFrames() : 0U;
}

std::size_t NullAudioDevice::GetStreamFreeFrames(const VoiceHandle voice) const {
    const Impl::Voice* const entry = m_impl->voices.Find(voice);
    return entry != nullptr && entry->IsStreaming() ? entry->stream.GetFreeFrames() : 0U;
}

void NullAudioDevice::ConsumeStreamFrames(const VoiceHandle voice, const std::size_t frames) {
    Impl::Voice* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr || !entry->IsStreaming() || frames == 0U) {
        return;
    }
    m_impl->drainScratch.resize(std::min(frames, entry->stream.GetBufferedFrames()) *
                                static_cast<std::size_t>(entry->stream.GetChannels()));
    static_cast<void>(entry->stream.Read(m_impl->drainScratch.data(), frames));
    if (entry->endRequested && entry->stream.GetBufferedFrames() == 0U) {
        entry->state = VoicePlaybackState::Stopped;
    }
}

std::string_view NullAudioDevice::GetBackendName() const noexcept {
    return "Null";
}

const std::vector<RecordedAudioCall>& NullAudioDevice::GetRecordedCalls() const noexcept {
    return m_impl->recordedCalls;
}

void NullAudioDevice::ClearRecordedCalls() noexcept {
    m_impl->recordedCalls.clear();
}

std::size_t NullAudioDevice::GetLiveVoiceCount() const noexcept {
    return m_impl->voices.GetLiveCount();
}

} // namespace UVE::Audio
