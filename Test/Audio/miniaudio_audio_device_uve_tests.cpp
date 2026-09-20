// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/miniaudio_audio_device_uve.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/audio_asset_uve.h"

namespace UVE::Audio::Tests {
namespace {

// Tests exercise the real backend end to end (context, device, audio-thread mixer, callback)
// through miniaudio's own timer-driven null backend, so the whole suite runs on machines without
// any audio hardware - including CI. Device startup is slow-ish (real context + thread), so one
// process-wide device underneath a shared fixture is deliberately not used: each test pays its
// own ~ms-scale setup and gets a pristine voice table instead.
[[nodiscard]] MiniaudioAudioDeviceUVE::OptionsUVE TestOptionsUVE() {
    MiniaudioAudioDeviceUVE::OptionsUVE options;
    options.useNullBackendForTesting = true;
    options.outputSampleRate = 48000U;
    return options;
}

[[nodiscard]] std::filesystem::path ScratchUveAudioUVE(const char* name) {
    return std::filesystem::path{std::string{name} + ".miniaudio_tests.uveaudio"};
}

void RemoveScratchUVE(std::initializer_list<std::filesystem::path> paths) {
    for (const auto& path : paths) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
}

/// Writes a real .uveaudio asset through the engine's own envelope writer - the exact format
/// CreateVoiceUVE() will load back.
[[nodiscard]] std::filesystem::path WriteTestClipUVE(
    const char* name, const std::vector<float>& interleavedSamples, const std::uint16_t channels = 1U,
    const std::uint32_t sampleRate = 8000U) {
    Asset::AudioAssetUVE clip;
    clip.channels = channels;
    clip.sampleRate = sampleRate;
    clip.samples = interleavedSamples;
    const std::filesystem::path path = ScratchUveAudioUVE(name);
    RemoveScratchUVE({path});
    EXPECT_TRUE(Asset::SaveAudioAssetUVE(clip, path));
    return path;
}

[[nodiscard]] std::vector<float> ConstantClipUVE(const std::size_t frames, const float value,
                                                 const std::uint16_t channels = 1U) {
    return std::vector<float>(frames * channels, value);
}

/// Polls a predicate with a generous bounded timeout. The null backend advances on a wall-clock
/// timer, so clip completion is inherently timing-based; 5 s is far beyond any scheduling jitter
/// a loaded CI runner realistically produces for the millisecond-scale clips used here.
template <typename TPredicate>
[[nodiscard]] bool WaitForUVE(TPredicate&& predicate,
                              const std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return predicate();
}

TEST(MiniaudioAudioDeviceUVETest, CreateUVE_StartsRealDeviceStackOnForcedNullBackend) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    EXPECT_TRUE(device->GetBackendNameUVE().starts_with("miniaudio/"));
}

TEST(MiniaudioAudioDeviceUVETest, CreateVoiceUVE_MissingAssetReturnsInvalidHandle) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    AudioVoiceDescUVE desc;
    desc.audioAssetPath = "definitely_missing_clip.miniaudio_tests.uveaudio";
    EXPECT_EQ(device->CreateVoiceUVE(desc), kInvalidVoiceHandleUVE);
}

TEST(MiniaudioAudioDeviceUVETest, VoiceLifecycle_PlacesCorrectStatesAndRejectsUnknownHandles) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    const auto path = WriteTestClipUVE("lifecycle", ConstantClipUVE(64U, 0.25F));
    ASSERT_FALSE(path.empty());

    AudioVoiceDescUVE desc;
    desc.audioAssetPath = path.string();
    const VoiceHandleUVE voice = device->CreateVoiceUVE(desc);
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);

    EXPECT_TRUE(device->PlayUVE(voice));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);

    AudioVoiceParamsUVE params;
    params.gain = 0.5F;
    params.pitch = 1.25F;
    EXPECT_TRUE(device->SetVoiceParamsUVE(voice, params));
    params.gain = 2.0F; // outside the validated [0, 1] contract
    EXPECT_FALSE(device->SetVoiceParamsUVE(voice, params));

    EXPECT_TRUE(device->StopUVE(voice));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);

    device->DestroyVoiceUVE(voice);
    EXPECT_FALSE(device->PlayUVE(voice));
    EXPECT_FALSE(device->StopUVE(voice));
    EXPECT_FALSE(device->SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{}));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped); // logged miss, not a crash
    RemoveScratchUVE({path});
}

TEST(MiniaudioAudioDeviceUVETest, NonLoopingVoice_AutoStopsAtClipEnd) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    // 8 ms of audio at 8 kHz against the 48 kHz output rate: finishes almost immediately.
    const auto path = WriteTestClipUVE("autostop", ConstantClipUVE(64U, 0.5F));
    ASSERT_FALSE(path.empty());

    AudioVoiceDescUVE desc;
    desc.audioAssetPath = path.string();
    desc.looping = false;
    const VoiceHandleUVE voice = device->CreateVoiceUVE(desc);
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    ASSERT_TRUE(device->PlayUVE(voice));

    EXPECT_TRUE(WaitForUVE([&] { return device->GetVoiceStateUVE(voice) == VoicePlaybackStateUVE::Stopped; }))
        << "non-looping voice should transition itself to Stopped at clip end";

    // Restart semantics: PlayUVE on a finished voice starts it over (it will auto-stop again).
    ASSERT_TRUE(device->PlayUVE(voice));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    EXPECT_TRUE(WaitForUVE([&] { return device->GetVoiceStateUVE(voice) == VoicePlaybackStateUVE::Stopped; }));
    device->DestroyVoiceUVE(voice);
    RemoveScratchUVE({path});
}

TEST(MiniaudioAudioDeviceUVETest, LoopingVoice_KeepsPlayingPastMultipleClipDurations) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    const auto path = WriteTestClipUVE("looping", ConstantClipUVE(32U, -0.5F));
    ASSERT_FALSE(path.empty());

    AudioVoiceDescUVE desc;
    desc.audioAssetPath = path.string();
    desc.looping = true;
    const VoiceHandleUVE voice = device->CreateVoiceUVE(desc);
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    ASSERT_TRUE(device->PlayUVE(voice));

    // 32 frames @ 8 kHz = 4 ms per loop; 200 ms spans ~50 loops - comfortably many, short enough
    // to stay cheap on loaded runners.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    EXPECT_TRUE(device->StopUVE(voice));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
    device->DestroyVoiceUVE(voice);
    RemoveScratchUVE({path});
}

TEST(MiniaudioAudioDeviceUVETest, EmptyAssetPath_CreatesCliplessVoiceThatStaysPlayingUntilStopped) {
    // The engine's clip-less source contract: AudioSystemUVE issues empty-path voices for
    // stream-scheduled and PCM-effect sources - the real device must accept them, keep them
    // Playing on demand, and never auto-stop them (no clip end exists to reach).
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);

    AudioVoiceDescUVE desc; // deliberately empty audioAssetPath
    const VoiceHandleUVE voice = device->CreateVoiceUVE(desc);
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    ASSERT_TRUE(device->PlayUVE(voice));
    std::this_thread::sleep_for(std::chrono::milliseconds{100}); // ~hundreds of mixer callbacks
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    EXPECT_TRUE(device->StopUVE(voice));
    EXPECT_EQ(device->GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
    device->DestroyVoiceUVE(voice);
}

TEST(MiniaudioAudioDeviceUVETest, CreateVoiceUVE_RejectsMalformedAssetChannels) {
    const auto device = MiniaudioAudioDeviceUVE::CreateUVE(TestOptionsUVE());
    ASSERT_NE(device, nullptr);
    Asset::AudioAssetUVE weird;
    weird.channels = 4U; // unsupported by the v1 mixer (documented: only 1-2 channels)
    weird.sampleRate = 8000U;
    weird.samples = std::vector<float>(8U, 0.0F);
    const auto path = ScratchUveAudioUVE("malformed");
    RemoveScratchUVE({path});
    ASSERT_TRUE(Asset::SaveAudioAssetUVE(weird, path));

    AudioVoiceDescUVE desc;
    desc.audioAssetPath = path.string();
    EXPECT_EQ(device->CreateVoiceUVE(desc), kInvalidVoiceHandleUVE);
    RemoveScratchUVE({path});
}

} // namespace
} // namespace UVE::Audio::Tests
