// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/miniaudio_audio_device_uve.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <miniaudio.h>

#include "uve/asset/audio_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Audio {

constexpr std::uint32_t kOutputChannelsUVE = 2U;

struct MiniaudioAudioDeviceUVE::ImplUVE {
    struct VoiceUVE {
        std::string assetPath;
        std::vector<float> samples;   // interleaved normalized PCM, `channels` wide
        std::uint32_t channels = 0U;  // 1 (mono, upmixed on the fly) or 2 (stereo)
        std::uint32_t sampleRate = 0U;
        double cursorFrames = 0.0;    // fractional position in clip frames
        double stepPerOutputFrame = 0.0; // (clipRate / outputRate) * pitch, recomputed on param set
        float gain = 1.0F;
        float pitch = 1.0F;
        float position[3] = {0.0F, 0.0F, 0.0F}; // stored; panning is a documented v1 non-goal
        bool looping = false;
    };

    ma_context context{};
    ma_device device{};
    bool contextInitialized = false;
    bool deviceInitialized = false;
    ma_uint32 outputSampleRate = 0U;

    mutable std::mutex voicesMutex;
    std::unordered_map<std::uint32_t, VoiceUVE> voices;
    std::unordered_map<std::uint32_t, VoicePlaybackStateUVE> voiceStates;
    std::uint32_t nextVoiceHandle = 1U;

    std::string backendName;

    // Runs on miniaudio's audio thread. Fills the whole output frame (miniaudio does not promise
    // a zeroed buffer), accumulating every playing voice with linear-interpolated, fractional-
    // stepped resampling. The critical section is deliberately short and allocation-free: all
    // buffers are owned per voice and only mapped in/out under this same mutex.
    static void OnAudioFramesUVE(ma_device* device, void* output, const void* /*input*/,
                                 ma_uint32 frameCount) {
        auto* impl = static_cast<ImplUVE*>(device->pUserData);
        float* out = static_cast<float*>(output);
        std::memset(out, 0, frameCount * kOutputChannelsUVE * sizeof(float));

        std::lock_guard<std::mutex> lock(impl->voicesMutex);
        for (auto& [handle, voice] : impl->voices) {
            const auto stateIterator = impl->voiceStates.find(handle);
            if (stateIterator == impl->voiceStates.end() ||
                stateIterator->second != VoicePlaybackStateUVE::Playing) {
                continue;
            }
            if (voice.channels == 0U) {
                continue; // clip-less source shell: inaudible, stays Playing until stopped
            }
            const std::size_t totalFrames = voice.samples.size() / voice.channels;
            for (ma_uint32 frame = 0U; frame < frameCount; ++frame) {
                const std::size_t frameIndex = static_cast<std::size_t>(voice.cursorFrames);
                if (frameIndex >= totalFrames) {
                    break; // clipped below by the end-of-clip transition
                }
                const float fraction =
                    static_cast<float>(voice.cursorFrames - static_cast<double>(frameIndex));
                const std::size_t nextIndex =
                    voice.looping ? ((frameIndex + 1U) % totalFrames)
                                  : std::min(frameIndex + 1U, totalFrames - 1U);
                for (std::uint32_t channel = 0U; channel < kOutputChannelsUVE; ++channel) {
                    const std::uint32_t sourceChannel =
                        voice.channels == 1U ? 0U : std::min(channel, voice.channels - 1U);
                    const float sampleA = voice.samples[frameIndex * voice.channels + sourceChannel];
                    const float sampleB = voice.samples[nextIndex * voice.channels + sourceChannel];
                    out[frame * kOutputChannelsUVE + channel] +=
                        (sampleA + (sampleB - sampleA) * fraction) * voice.gain;
                }
                voice.cursorFrames += voice.stepPerOutputFrame;
                if (voice.cursorFrames >= static_cast<double>(totalFrames)) {
                    if (voice.looping) {
                        voice.cursorFrames = std::fmod(voice.cursorFrames, static_cast<double>(totalFrames));
                    } else {
                        voice.cursorFrames = static_cast<double>(totalFrames);
                        stateIterator->second = VoicePlaybackStateUVE::Stopped;
                        break; // finishes naturally mid-buffer; the rest stays silent
                    }
                }
            }
        }
    }
};

MiniaudioAudioDeviceUVE::MiniaudioAudioDeviceUVE(std::unique_ptr<ImplUVE> impl) noexcept
    : m_impl(std::move(impl)) {}

MiniaudioAudioDeviceUVE::~MiniaudioAudioDeviceUVE() {
    if (m_impl->deviceInitialized) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
    }
    if (m_impl->contextInitialized) {
        ma_context_uninit(&m_impl->context);
    }
}

std::unique_ptr<MiniaudioAudioDeviceUVE> MiniaudioAudioDeviceUVE::CreateUVE() {
    return CreateUVE(OptionsUVE{});
}

std::unique_ptr<MiniaudioAudioDeviceUVE> MiniaudioAudioDeviceUVE::CreateUVE(const OptionsUVE& options) {
    auto impl = std::make_unique<ImplUVE>();

    const ma_backend forcedBackend = ma_backend_null;
    const ma_backend* backends = options.useNullBackendForTesting ? &forcedBackend : nullptr;
    const ma_uint32 backendCount = options.useNullBackendForTesting ? 1U : 0U;

    if (ma_context_init(backends, backendCount, nullptr, &impl->context) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: ma_context_init failed (no usable audio backend found)");
        return nullptr;
    }
    impl->contextInitialized = true;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = kOutputChannelsUVE;
    deviceConfig.sampleRate = options.outputSampleRate;
    deviceConfig.dataCallback = &ImplUVE::OnAudioFramesUVE;
    deviceConfig.pUserData = impl.get();
    // No fixed period settings: let the backend pick comfortable latencies; playback correctness
    // does not depend on the callback frame quantum here.

    if (ma_device_init(&impl->context, &deviceConfig, &impl->device) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: ma_device_init failed (no usable output device on backend '{}')",
                  ma_get_backend_name(impl->context.backend));
        ma_context_uninit(&impl->context);
        return nullptr;
    }
    impl->deviceInitialized = true;
    impl->outputSampleRate = impl->device.sampleRate;

    if (ma_device_start(&impl->device) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: ma_device_start failed on backend '{}'",
                  ma_get_backend_name(impl->context.backend));
        ma_device_uninit(&impl->device);
        ma_context_uninit(&impl->context);
        return nullptr;
    }

    impl->backendName = std::string{"miniaudio/"} + ma_get_backend_name(impl->context.backend);
    UVE_INFO("MiniaudioAudioDeviceUVE: started on backend '{}', {} Hz, {} channel(s)", impl->backendName,
             impl->outputSampleRate, kOutputChannelsUVE);
    return std::unique_ptr<MiniaudioAudioDeviceUVE>(new MiniaudioAudioDeviceUVE(std::move(impl)));
}

VoiceHandleUVE MiniaudioAudioDeviceUVE::CreateVoiceUVE(const AudioVoiceDescUVE& desc) {
    // Empty path = the engine's clip-less source contract (AudioSystemUVE relies on it for
    // stream-scheduled and PCM-effect sources, which never submit sample data through this
    // interface): create a valid, inaudible voice with no clip attached. The null device honored
    // this implicitly by ignoring paths entirely; the real device honors it explicitly. An
    // empty-clip voice never auto-stops (there is no clip end to reach) - it plays silence until
    // told to stop.
    if (desc.audioAssetPath.empty()) {
        ImplUVE::VoiceUVE emptyVoice;
        emptyVoice.channels = 0U;
        emptyVoice.sampleRate = m_impl->outputSampleRate; // only feeds the pitch-step formula
        emptyVoice.stepPerOutputFrame = static_cast<double>(emptyVoice.sampleRate) /
                                        static_cast<double>(m_impl->outputSampleRate);
        emptyVoice.looping = desc.looping;
        std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
        const std::uint32_t handleValue = m_impl->nextVoiceHandle++;
        m_impl->voices.emplace(handleValue, std::move(emptyVoice));
        m_impl->voiceStates.emplace(handleValue, VoicePlaybackStateUVE::Stopped);
        return VoiceHandleUVE{handleValue};
    }

    Asset::AudioAssetUVE asset;
    if (!Asset::LoadAudioAssetUVE(desc.audioAssetPath, asset)) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: CreateVoiceUVE could not load audio asset '{}' - returning "
                  "an invalid handle",
                  desc.audioAssetPath);
        return kInvalidVoiceHandleUVE;
    }
    if ((asset.channels != 1U && asset.channels != 2U) || asset.sampleRate == 0U ||
        asset.samples.empty() || (asset.samples.size() % asset.channels) != 0U) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: CreateVoiceUVE rejected malformed audio asset '{}' "
                  "(channels={}, sampleRate={}, samples={})",
                  desc.audioAssetPath, asset.channels, asset.sampleRate, asset.samples.size());
        return kInvalidVoiceHandleUVE;
    }

    ImplUVE::VoiceUVE voice;
    voice.assetPath = desc.audioAssetPath;
    voice.samples = std::move(asset.samples);
    voice.channels = asset.channels;
    voice.sampleRate = asset.sampleRate;
    voice.looping = desc.looping;
    voice.stepPerOutputFrame = static_cast<double>(asset.sampleRate) /
                               static_cast<double>(m_impl->outputSampleRate);

    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const std::uint32_t handleValue = m_impl->nextVoiceHandle++;
    m_impl->voices.emplace(handleValue, std::move(voice));
    m_impl->voiceStates.emplace(handleValue, VoicePlaybackStateUVE::Stopped);
    return VoiceHandleUVE{handleValue};
}

void MiniaudioAudioDeviceUVE::DestroyVoiceUVE(VoiceHandleUVE voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const bool erased = (m_impl->voices.erase(voice.value) != 0U) | (m_impl->voiceStates.erase(voice.value) != 0U);
    if (!erased) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: DestroyVoiceUVE called with an unknown or already-destroyed "
                  "handle ({})",
                  voice.value);
    }
}

bool MiniaudioAudioDeviceUVE::PlayUVE(VoiceHandleUVE voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const auto stateIterator = m_impl->voiceStates.find(voice.value);
    if (stateIterator == m_impl->voiceStates.end()) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: PlayUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    // Restart semantics (matching the interface contract): a voice that already finished starts
    // over from the beginning.
    if (stateIterator->second == VoicePlaybackStateUVE::Stopped) {
        if (const auto voiceIterator = m_impl->voices.find(voice.value); voiceIterator != m_impl->voices.end()) {
            voiceIterator->second.cursorFrames = 0.0;
        }
    }
    stateIterator->second = VoicePlaybackStateUVE::Playing;
    return true;
}

bool MiniaudioAudioDeviceUVE::StopUVE(VoiceHandleUVE voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const auto stateIterator = m_impl->voiceStates.find(voice.value);
    if (stateIterator == m_impl->voiceStates.end()) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: StopUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    stateIterator->second = VoicePlaybackStateUVE::Stopped;
    return true;
}

bool MiniaudioAudioDeviceUVE::SetVoiceParamsUVE(VoiceHandleUVE voice, const AudioVoiceParamsUVE& params) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const auto iterator = m_impl->voices.find(voice.value);
    if (iterator == m_impl->voices.end()) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: SetVoiceParamsUVE called with an unknown handle ({})", voice.value);
        return false;
    }
    if (!ValidateAudioVoiceParamsUVE(params)) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: SetVoiceParamsUVE received invalid parameters for handle ({})",
                  voice.value);
        return false;
    }
    iterator->second.position[0] = params.position.x;
    iterator->second.position[1] = params.position.y;
    iterator->second.position[2] = params.position.z;
    iterator->second.gain = params.gain;
    iterator->second.pitch = params.pitch;
    iterator->second.stepPerOutputFrame =
        (static_cast<double>(iterator->second.sampleRate) / static_cast<double>(m_impl->outputSampleRate)) *
        static_cast<double>(params.pitch);
    return true;
}

VoicePlaybackStateUVE MiniaudioAudioDeviceUVE::GetVoiceStateUVE(VoiceHandleUVE voice) const {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const auto iterator = m_impl->voiceStates.find(voice.value);
    if (iterator == m_impl->voiceStates.end()) {
        UVE_ERROR("MiniaudioAudioDeviceUVE: GetVoiceStateUVE called with an unknown handle ({})", voice.value);
        return VoicePlaybackStateUVE::Stopped;
    }
    return iterator->second;
}

std::string_view MiniaudioAudioDeviceUVE::GetBackendNameUVE() const noexcept {
    return m_impl->backendName;
}

} // namespace UVE::Audio
