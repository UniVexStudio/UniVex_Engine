// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <variant>

#include "audio_voice_desc.h"
#include "voice_handle.h"

namespace UVE::Audio {

/// One order-sensitive call recorded by NullAudioDevice — deliberately only the three calls a
/// real backend would need to receive in exact sequence to reproduce this voice's behavior
/// (Play/Stop/SetVoiceParams). CreateVoice/DestroyVoice are pure resource-lifecycle
/// bookkeeping (tested via NullAudioDevice::GetLiveVoiceCount(), like
/// Render::NullRenderDevice's own Create/Destroy calls) and are deliberately NOT part of this
/// sequence — audio has no command-buffer/submission concept at all (a real backend issues
/// alSourcePlay/alSourcefv-style calls directly, not via a batched command list), so calls
/// accumulate for the device's whole lifetime instead of being grouped by a "submit" boundary.
struct PlayVoiceCall {
    VoiceHandle voice;
};
struct StopVoiceCall {
    VoiceHandle voice;
};
struct SetVoiceParamsCall {
    VoiceHandle voice;
    AudioVoiceParams params;
};

/// A single recorded IAudioDevice call, tagged by which method it came from.
using RecordedAudioCall = std::variant<PlayVoiceCall, StopVoiceCall, SetVoiceParamsCall>;

} // namespace UVE::Audio
