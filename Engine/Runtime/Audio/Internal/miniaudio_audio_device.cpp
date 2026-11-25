// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "miniaudio_audio_device.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>

#include <miniaudio.h>

#include "audio_asset.h"
#include "audio_occlusion.h"
#include "audio_spatialization.h"
#include "handle_table.h"
#include "logging_macros.h"

namespace UVE::Audio {

constexpr std::uint32_t kOutputChannels = 2U;

namespace {

/// One voice's measurement for one mixed block. Accumulated frame by frame as the voice is summed
/// (so it is what the voice really added, not what its settings say it should have), then folded
/// into the voice's held reading once per block.
struct BlockMeter final {
    float peak = 0.0F;
    float sumSquares = 0.0F;
    std::uint32_t samples = 0U;

    void Add(const float left, const float right) noexcept {
        peak = std::max({peak, std::abs(left), std::abs(right)});
        sumSquares += left * left + right * right;
        samples += 2U;
    }

    [[nodiscard]] float MeanSquare() const noexcept {
        return samples == 0U ? 0.0F : sumSquares / static_cast<float>(samples);
    }
};

} // namespace

struct MiniaudioAudioDevice::Impl {
    struct Voice {
        std::string assetPath;
        std::vector<float> samples;   // interleaved normalized PCM, `channels` wide
        std::uint32_t channels = 0U;  // 1 (mono, upmixed on the fly) or 2 (stereo)
        std::uint32_t sampleRate = 0U;
        double cursorFrames = 0.0;    // fractional position in clip frames
        double stepPerOutputFrame = 0.0; // (clipRate / outputRate) * pitch, recomputed on param set
        float gain = 1.0F;
        float pitch = 1.0F;
        float position[3] = {0.0F, 0.0F, 0.0F}; // stored; the stereo image comes from `pan`
        float pan = 0.0F;                       // -1 left .. +1 right, already derived by the system
        bool looping = false;
        /// Configured only for streaming voices: the clip fields above stay unused for those, and
        /// `cursorFrames` indexes this ring instead of `samples`.
        AudioStreamRingBuffer stream;
        bool streamEnded = false;
        /// Occlusion muffling. Bypassed (touching nothing) whenever the system reports cutoff 0.
        AudioVoiceLowPass lowPass;
        float lastLowPassCutoffHz = 0.0F;
        /// What this voice actually contributed to the output, kept as a held peak plus a windowed
        /// average. Owned by the audio thread; every read takes voicesMutex.
        AudioMeterReading meter{};

        [[nodiscard]] bool IsStreaming() const noexcept { return stream.IsConfigured(); }
    };

    ma_context context{};
    ma_device device{};
    bool contextInitialized = false;
    bool deviceInitialized = false;
    ma_uint32 outputSampleRate = 0U;

    // Voice slots: one generational table holding each voice together with its playback state
    // (the two used to live in parallel maps keyed by a monotonic counter — always in sync, so
    // unifying them changes no behavior while making single-lookup access possible).
    struct VoiceEntry {
        Voice voice;
        VoicePlaybackState state = VoicePlaybackState::Stopped;
    };

    mutable std::mutex voicesMutex;
    Containers::HandleTable<VoiceEntry, VoiceHandle> voices;

    /// Mixes a streaming voice for one callback block, consuming exactly the frames the fractional
    /// step passed. A starved ring yields silence for the rest of the block and, once the stream has
    /// been ended and fully drained, stops the voice - so an entity playing a finished stream reads
    /// as stopped rather than as silently still playing.
    static void MixStream(Voice& voice, VoicePlaybackState& state, float* out,
                             ma_uint32 frameCount, float blockSeconds) noexcept;

    std::string backendName;

    // Runs on miniaudio's audio thread. Fills the whole output frame (miniaudio does not promise
    // a zeroed buffer), accumulating every playing voice with linear-interpolated, fractional-
    // stepped resampling. The critical section is deliberately short and allocation-free: all
    // buffers are owned per voice and only mapped in/out under this same mutex.
    static void OnAudioFrames(ma_device* device, void* output, const void* /*input*/,
                                 ma_uint32 frameCount) {
        auto* impl = static_cast<Impl*>(device->pUserData);
        float* out = static_cast<float*>(output);
        std::memset(out, 0, frameCount * kOutputChannels * sizeof(float));

        std::lock_guard<std::mutex> lock(impl->voicesMutex);
        const float blockSeconds =
            static_cast<float>(frameCount) /
            static_cast<float>(impl->outputSampleRate == 0U ? 48000U : impl->outputSampleRate);
        impl->voices.ForEach([&](VoiceHandle /*handle*/, VoiceEntry& entry) {
            Voice& voice = entry.voice;
            if (entry.state != VoicePlaybackState::Playing) {
                // A voice that is not mixing still has a meter to release, or the panel would
                // freeze the last block it ever played forever.
                voice.meter = IntegrateAudioMeter(voice.meter, 0.0F, 0.0F, blockSeconds);
                return;
            }
            if (voice.channels == 0U) {
                // Clip-less source shell: inaudible, stays Playing until stopped. Its meter still
                // releases so a shell that was never audible reads as silence, not as frozen.
                voice.meter = IntegrateAudioMeter(voice.meter, 0.0F, 0.0F, blockSeconds);
                return;
            }
            if (voice.IsStreaming()) {
                MixStream(voice, entry.state, out, frameCount, blockSeconds);
                return;
            }
            const std::size_t totalFrames = voice.samples.size() / voice.channels;
            // Constant-power pan, computed once per block: the law is unity on both sides at centre,
            // so a centred voice mixes exactly as it did before panning existed.
            const AudioPanGains panGains = ComputeAudioPanGains(voice.pan);
            BlockMeter blockMeter;
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
                float mixedFrame[kOutputChannels] = {0.0F, 0.0F};
                for (std::uint32_t channel = 0U; channel < kOutputChannels; ++channel) {
                    const std::uint32_t sourceChannel =
                        voice.channels == 1U ? 0U : std::min(channel, voice.channels - 1U);
                    const float sampleA = voice.samples[frameIndex * voice.channels + sourceChannel];
                    const float sampleB = voice.samples[nextIndex * voice.channels + sourceChannel];
                    mixedFrame[channel] = sampleA + (sampleB - sampleA) * fraction;
                }
                // Filtered before gain and pan, so the muffling never depends on how loud the voice
                // happens to be, and skipped entirely when the voice is unoccluded.
                voice.lowPass.Process(mixedFrame[0], mixedFrame[1]);
                float contribution[kOutputChannels] = {0.0F, 0.0F};
                for (std::uint32_t channel = 0U; channel < kOutputChannels; ++channel) {
                    const float panGain = channel == 0U ? panGains.left : panGains.right;
                    contribution[channel] = mixedFrame[channel] * voice.gain * panGain;
                    out[frame * kOutputChannels + channel] += contribution[channel];
                }
                blockMeter.Add(contribution[0], contribution[1]);
                voice.cursorFrames += voice.stepPerOutputFrame;
                if (voice.cursorFrames >= static_cast<double>(totalFrames)) {
                    if (voice.looping) {
                        voice.cursorFrames = std::fmod(voice.cursorFrames, static_cast<double>(totalFrames));
                    } else {
                        voice.cursorFrames = static_cast<double>(totalFrames);
                        entry.state = VoicePlaybackState::Stopped;
                        break; // finishes naturally mid-buffer; the rest stays silent
                    }
                }
            }
            voice.meter = IntegrateAudioMeter(voice.meter, blockMeter.peak, blockMeter.MeanSquare(),
                                                 blockSeconds);
        });
    }
};

void MiniaudioAudioDevice::Impl::MixStream(Voice& voice, VoicePlaybackState& state,
                                                    float* const out, const ma_uint32 frameCount,
                                                    const float blockSeconds) noexcept {
    const std::size_t channels = static_cast<std::size_t>(voice.stream.GetChannels());
    if (channels == 0U) {
        return;
    }
    const AudioPanGains panGains = ComputeAudioPanGains(voice.pan);
    float frameA[kMaximumAudioStreamChannels] = {0.0F, 0.0F};
    float frameB[kMaximumAudioStreamChannels] = {0.0F, 0.0F};
    BlockMeter blockMeter;
    for (ma_uint32 frame = 0U; frame < frameCount; ++frame) {
        const std::size_t index = static_cast<std::size_t>(voice.cursorFrames);
        // Linear interpolation needs the frame under the cursor and the one after it, exactly like
        // the clip path does with its resident buffer.
        if (!voice.stream.PeekFrame(index, frameA)) {
            if (voice.streamEnded) {
                state = VoicePlaybackState::Stopped;
                voice.cursorFrames = 0.0;
            }
            break; // starved: the rest of this block stays silent
        }
        if (!voice.stream.PeekFrame(index + 1U, frameB)) {
            if (!voice.streamEnded) {
                break; // mid-stream starvation: wait for the pump rather than guessing an end
            }
            // The stream is over and this is the final frame: play it on its own rather than
            // dropping it for want of a successor, then let the drain below stop the voice.
            std::copy(frameA, frameA + channels, frameB);
        }
        const float fraction = static_cast<float>(voice.cursorFrames - static_cast<double>(index));
        float mixedFrame[kOutputChannels] = {0.0F, 0.0F};
        for (std::uint32_t channel = 0U; channel < kOutputChannels; ++channel) {
            const std::size_t sourceChannel =
                channels == 1U ? 0U : std::min(static_cast<std::size_t>(channel), channels - 1U);
            mixedFrame[channel] =
                frameA[sourceChannel] + (frameB[sourceChannel] - frameA[sourceChannel]) * fraction;
        }
        voice.lowPass.Process(mixedFrame[0], mixedFrame[1]);
        float contribution[kOutputChannels] = {0.0F, 0.0F};
        for (std::uint32_t channel = 0U; channel < kOutputChannels; ++channel) {
            const float panGain = channel == 0U ? panGains.left : panGains.right;
            contribution[channel] = mixedFrame[channel] * voice.gain * panGain;
            out[frame * kOutputChannels + channel] += contribution[channel];
        }
        blockMeter.Add(contribution[0], contribution[1]);
        voice.cursorFrames += voice.stepPerOutputFrame;
        const std::size_t consumed = static_cast<std::size_t>(voice.cursorFrames);
        if (consumed > 0U) {
            voice.stream.Consume(consumed);
            voice.cursorFrames -= static_cast<double>(consumed);
        }
        if (voice.streamEnded && voice.stream.GetBufferedFrames() == 0U) {
            state = VoicePlaybackState::Stopped;
            voice.cursorFrames = 0.0;
            break;
        }
    }
    voice.meter = IntegrateAudioMeter(voice.meter, blockMeter.peak, blockMeter.MeanSquare(), blockSeconds);
}

MiniaudioAudioDevice::MiniaudioAudioDevice(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

MiniaudioAudioDevice::~MiniaudioAudioDevice() {
    if (m_impl->deviceInitialized) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
    }
    if (m_impl->contextInitialized) {
        ma_context_uninit(&m_impl->context);
    }
}

std::unique_ptr<MiniaudioAudioDevice> MiniaudioAudioDevice::Create() {
    return Create(Options{});
}

std::unique_ptr<MiniaudioAudioDevice> MiniaudioAudioDevice::Create(const Options& options) {
    auto impl = std::make_unique<Impl>();

    const ma_backend forcedBackend = ma_backend_null;
    const ma_backend* backends = options.useNullBackendForTesting ? &forcedBackend : nullptr;
    const ma_uint32 backendCount = options.useNullBackendForTesting ? 1U : 0U;

    if (ma_context_init(backends, backendCount, nullptr, &impl->context) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDevice: ma_context_init failed (no usable audio backend found)");
        return nullptr;
    }
    impl->contextInitialized = true;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = kOutputChannels;
    deviceConfig.sampleRate = options.outputSampleRate;
    deviceConfig.dataCallback = &Impl::OnAudioFrames;
    deviceConfig.pUserData = impl.get();
    // No fixed period settings: let the backend pick comfortable latencies; playback correctness
    // does not depend on the callback frame quantum here.

    if (ma_device_init(&impl->context, &deviceConfig, &impl->device) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDevice: ma_device_init failed (no usable output device on backend '{}')",
                  ma_get_backend_name(impl->context.backend));
        ma_context_uninit(&impl->context);
        return nullptr;
    }
    impl->deviceInitialized = true;
    impl->outputSampleRate = impl->device.sampleRate;

    if (ma_device_start(&impl->device) != MA_SUCCESS) {
        UVE_ERROR("MiniaudioAudioDevice: ma_device_start failed on backend '{}'",
                  ma_get_backend_name(impl->context.backend));
        ma_device_uninit(&impl->device);
        ma_context_uninit(&impl->context);
        return nullptr;
    }

    impl->backendName = std::string{"miniaudio/"} + ma_get_backend_name(impl->context.backend);
    UVE_INFO("MiniaudioAudioDevice: started on backend '{}', {} Hz, {} channel(s)", impl->backendName,
             impl->outputSampleRate, kOutputChannels);
    return std::unique_ptr<MiniaudioAudioDevice>(new MiniaudioAudioDevice(std::move(impl)));
}

VoiceHandle MiniaudioAudioDevice::CreateVoice(const AudioVoiceDesc& desc) {
    // Empty path = the engine's clip-less source contract (AudioSystem relies on it for
    // stream-scheduled and PCM-effect sources, which never submit sample data through this
    // interface): create a valid, inaudible voice with no clip attached. The null device honored
    // this implicitly by ignoring paths entirely; the real device honors it explicitly. An
    // empty-clip voice never auto-stops (there is no clip end to reach) - it plays silence until
    // told to stop.
    if (desc.audioAssetPath.empty()) {
        Impl::Voice emptyVoice;
        emptyVoice.channels = 0U;
        emptyVoice.sampleRate = m_impl->outputSampleRate; // only feeds the pitch-step formula
        emptyVoice.stepPerOutputFrame = static_cast<double>(emptyVoice.sampleRate) /
                                        static_cast<double>(m_impl->outputSampleRate);
        emptyVoice.looping = desc.looping;
        std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
        return m_impl->voices.Acquire(
            Impl::VoiceEntry{std::move(emptyVoice), VoicePlaybackState::Stopped});
    }

    Asset::AudioAsset asset;
    if (!Asset::LoadAudioAsset(desc.audioAssetPath, asset)) {
        UVE_ERROR("MiniaudioAudioDevice: CreateVoice could not load audio asset '{}' - returning "
                  "an invalid handle",
                  desc.audioAssetPath);
        return kInvalidVoiceHandle;
    }
    if ((asset.channels != 1U && asset.channels != 2U) || asset.sampleRate == 0U ||
        asset.samples.empty() || (asset.samples.size() % asset.channels) != 0U) {
        UVE_ERROR("MiniaudioAudioDevice: CreateVoice rejected malformed audio asset '{}' "
                  "(channels={}, sampleRate={}, samples={})",
                  desc.audioAssetPath, asset.channels, asset.sampleRate, asset.samples.size());
        return kInvalidVoiceHandle;
    }

    Impl::Voice voice;
    voice.assetPath = desc.audioAssetPath;
    voice.samples = std::move(asset.samples);
    voice.channels = asset.channels;
    voice.sampleRate = asset.sampleRate;
    voice.looping = desc.looping;
    voice.stepPerOutputFrame = static_cast<double>(asset.sampleRate) /
                               static_cast<double>(m_impl->outputSampleRate);

    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    return m_impl->voices.Acquire(
        Impl::VoiceEntry{std::move(voice), VoicePlaybackState::Stopped});
}

void MiniaudioAudioDevice::DestroyVoice(VoiceHandle voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const bool erased = m_impl->voices.Release(voice);
    if (!erased) {
        UVE_ERROR("MiniaudioAudioDevice: DestroyVoice called with an unknown or already-destroyed "
                  "handle ({})",
                  voice.value);
    }
}

bool MiniaudioAudioDevice::Play(VoiceHandle voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("MiniaudioAudioDevice: Play called with an unknown handle ({})", voice.value);
        return false;
    }
    // Restart semantics (matching the interface contract): a voice that already finished starts
    // over from the beginning.
    if (entry->state == VoicePlaybackState::Stopped) {
        entry->voice.cursorFrames = 0.0;
    }
    entry->state = VoicePlaybackState::Playing;
    return true;
}

bool MiniaudioAudioDevice::Stop(VoiceHandle voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("MiniaudioAudioDevice: Stop called with an unknown handle ({})", voice.value);
        return false;
    }
    entry->state = VoicePlaybackState::Stopped;
    return true;
}

bool MiniaudioAudioDevice::SetVoiceParams(VoiceHandle voice, const AudioVoiceParams& params) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("MiniaudioAudioDevice: SetVoiceParams called with an unknown handle ({})", voice.value);
        return false;
    }
    if (!ValidateAudioVoiceParams(params)) {
        UVE_ERROR("MiniaudioAudioDevice: SetVoiceParams received invalid parameters for handle ({})",
                  voice.value);
        return false;
    }
    entry->voice.position[0] = params.position.x;
    entry->voice.position[1] = params.position.y;
    entry->voice.position[2] = params.position.z;
    entry->voice.pan = params.pan;
    entry->voice.gain = params.gain;
    entry->voice.pitch = params.pitch;
    entry->voice.stepPerOutputFrame =
        (static_cast<double>(entry->voice.sampleRate) / static_cast<double>(m_impl->outputSampleRate)) *
        static_cast<double>(params.pitch);
    // Only touch the filter state when the corner actually moves: reconfiguring every frame would
    // reset nothing but would still cost a log/exp per voice per frame for no audible gain.
    if (params.lowPassCutoffHz != entry->voice.lastLowPassCutoffHz) {
        const bool wasBypassed = entry->voice.lastLowPassCutoffHz <= 0.0F;
        const bool nowBypassed = params.lowPassCutoffHz <= 0.0F;
        entry->voice.lowPass.Configure(params.lowPassCutoffHz,
                                          static_cast<float>(m_impl->outputSampleRate));
        if (wasBypassed || nowBypassed) {
            entry->voice.lowPass.Reset();
        }
        entry->voice.lastLowPassCutoffHz = params.lowPassCutoffHz;
    }
    return true;
}

VoiceHandle MiniaudioAudioDevice::CreateStreamVoice(const AudioStreamFormat& format) {
    if (!IsAudioStreamFormatValid(format)) {
        UVE_ERROR("MiniaudioAudioDevice: CreateStreamVoice rejected an invalid format ({} Hz, {} channels)",
                  format.sampleRate, format.channels);
        return kInvalidVoiceHandle;
    }
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const VoiceHandle voice = m_impl->voices.Acquire();
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        return kInvalidVoiceHandle;
    }
    entry->voice.stream.Configure(kAudioStreamRingFrames, format.channels);
    entry->voice.channels = format.channels;
    entry->voice.sampleRate = format.sampleRate;
    entry->voice.stepPerOutputFrame =
        (static_cast<double>(format.sampleRate) / static_cast<double>(m_impl->outputSampleRate)) *
        static_cast<double>(entry->voice.pitch);
    return voice;
}

std::size_t MiniaudioAudioDevice::PushStreamFrames(const VoiceHandle voice,
                                                         const std::span<const float> interleavedFrames) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("MiniaudioAudioDevice: PushStreamFrames called with an unknown handle ({})", voice.value);
        return 0U;
    }
    Impl::Voice& streamVoice = entry->voice;
    if (!streamVoice.IsStreaming()) {
        UVE_ERROR("MiniaudioAudioDevice: PushStreamFrames called on a clip voice ({})", voice.value);
        return 0U;
    }
    if (streamVoice.streamEnded) {
        UVE_ERROR("MiniaudioAudioDevice: PushStreamFrames called after EndStream ({})", voice.value);
        return 0U;
    }
    const std::size_t channels = static_cast<std::size_t>(streamVoice.stream.GetChannels());
    if (channels == 0U || interleavedFrames.size() % channels != 0U) {
        UVE_ERROR("MiniaudioAudioDevice: PushStreamFrames received {} samples, not a whole number of frames",
                  interleavedFrames.size());
        return 0U;
    }
    return streamVoice.stream.Write(interleavedFrames.data(), interleavedFrames.size() / channels);
}

bool MiniaudioAudioDevice::EndStream(const VoiceHandle voice) {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr || !entry->voice.IsStreaming()) {
        UVE_ERROR("MiniaudioAudioDevice: EndStream called with an unknown handle or a clip voice ({})",
                  voice.value);
        return false;
    }
    entry->voice.streamEnded = true;
    if (entry->voice.stream.GetBufferedFrames() == 0U) {
        entry->state = VoicePlaybackState::Stopped;
    }
    return true;
}

std::size_t MiniaudioAudioDevice::GetStreamBufferedFrames(const VoiceHandle voice) const {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    return entry != nullptr && entry->voice.IsStreaming() ? entry->voice.stream.GetBufferedFrames() : 0U;
}

std::size_t MiniaudioAudioDevice::GetStreamFreeFrames(const VoiceHandle voice) const {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    return entry != nullptr && entry->voice.IsStreaming() ? entry->voice.stream.GetFreeFrames() : 0U;
}

VoicePlaybackState MiniaudioAudioDevice::GetVoiceState(VoiceHandle voice) const {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    if (entry == nullptr) {
        UVE_ERROR("MiniaudioAudioDevice: GetVoiceState called with an unknown handle ({})", voice.value);
        return VoicePlaybackState::Stopped;
    }
    return entry->state;
}

AudioMeterReading MiniaudioAudioDevice::ReadVoiceMeter(const VoiceHandle voice) const {
    std::lock_guard<std::mutex> lock(m_impl->voicesMutex);
    const Impl::VoiceEntry* const entry = m_impl->voices.Find(voice);
    return entry != nullptr ? entry->voice.meter : kSilentAudioMeter;
}

std::string_view MiniaudioAudioDevice::GetBackendName() const noexcept {
    return m_impl->backendName;
}

} // namespace UVE::Audio
