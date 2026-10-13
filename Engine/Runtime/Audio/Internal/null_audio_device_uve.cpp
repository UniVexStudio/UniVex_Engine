// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/null_audio_device_uve.h"

#include <utility>

#include "uve/containers/handle_table_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Audio {

struct NullAudioDeviceUVE::ImplUVE {
    Containers::HandleTableUVE<VoicePlaybackStateUVE, VoiceHandleUVE> voices;
    std::vector<RecordedAudioCallUVE> recordedCalls;
};

NullAudioDeviceUVE::NullAudioDeviceUVE() : m_impl(std::make_unique<ImplUVE>()) {}

NullAudioDeviceUVE::~NullAudioDeviceUVE() = default;

VoiceHandleUVE NullAudioDeviceUVE::CreateVoiceUVE(const AudioVoiceDescUVE& desc) {
    static_cast<void>(desc); // NullAudioDeviceUVE performs no real audio output, bookkeeping only.
    return m_impl->voices.AcquireUVE(VoicePlaybackStateUVE::Stopped);
}

void NullAudioDeviceUVE::DestroyVoiceUVE(VoiceHandleUVE voice) {
    if (!m_impl->voices.ReleaseUVE(voice)) {
        UVE_ERROR("NullAudioDeviceUVE: DestroyVoiceUVE called with an unknown or already-destroyed handle ({})",
                   voice.value);
    }
}

bool NullAudioDeviceUVE::PlayUVE(VoiceHandleUVE voice) {
    VoicePlaybackStateUVE* const state = m_impl->voices.FindUVE(voice);
    if (state == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: PlayUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    *state = VoicePlaybackStateUVE::Playing;
    m_impl->recordedCalls.emplace_back(PlayVoiceCallUVE{voice});
    return true;
}

bool NullAudioDeviceUVE::StopUVE(VoiceHandleUVE voice) {
    VoicePlaybackStateUVE* const state = m_impl->voices.FindUVE(voice);
    if (state == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: StopUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    *state = VoicePlaybackStateUVE::Stopped;
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
    const VoicePlaybackStateUVE* const state = m_impl->voices.FindUVE(voice);
    if (state == nullptr) {
        UVE_ERROR("NullAudioDeviceUVE: GetVoiceStateUVE called with an unknown handle ({})", voice.value);
        return VoicePlaybackStateUVE::Stopped;
    }
    return *state;
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
