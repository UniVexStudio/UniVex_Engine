// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/audio_system_uve.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/audio/null_audio_device_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"

namespace UVE::Audio::Tests {
namespace {

class AudioSystemUVETest : public ::testing::Test {
protected:
    NullAudioDeviceUVE device;
    AudioSystemUVE audioSystem{device};
};

[[nodiscard]] bool LoggedAnErrorUVE(const Debug::MemorySinkUVE& sink) {
    const std::vector<Debug::LogMessageUVE> messages = sink.GetMessagesUVE();
    return std::any_of(messages.begin(), messages.end(),
                        [](const Debug::LogMessageUVE& message) { return message.level == Debug::LogLevelUVE::Error; });
}

class RecordingAudioDeviceUVE final : public IAudioDeviceUVE {
public:
    [[nodiscard]] VoiceHandleUVE CreateVoiceUVE(const AudioVoiceDescUVE& desc) override {
        lastDescription = desc;
        ++createCount;
        return nextHandle;
    }
    void DestroyVoiceUVE(VoiceHandleUVE voice) override {
        lastHandle = voice;
        ++destroyCount;
    }
    [[nodiscard]] bool PlayUVE(VoiceHandleUVE voice) override {
        lastHandle = voice;
        ++playCount;
        return true;
    }
    [[nodiscard]] bool StopUVE(VoiceHandleUVE voice) override {
        lastHandle = voice;
        ++stopCount;
        return true;
    }
    [[nodiscard]] bool SetVoiceParamsUVE(VoiceHandleUVE, const AudioVoiceParamsUVE&) override { return true; }
    [[nodiscard]] VoicePlaybackStateUVE GetVoiceStateUVE(VoiceHandleUVE voice) const override {
        lastHandle = voice;
        ++stateCount;
        return VoicePlaybackStateUVE::Stopped;
    }
    [[nodiscard]] std::string_view GetBackendNameUVE() const noexcept override { return "Recording"; }

    AudioVoiceDescUVE lastDescription{};
    mutable VoiceHandleUVE lastHandle = kInvalidVoiceHandleUVE;
    VoiceHandleUVE nextHandle{1U};
    int createCount = 0;
    mutable int destroyCount = 0;
    mutable int playCount = 0;
    mutable int stopCount = 0;
    mutable int stateCount = 0;
};

class TestAudioClipResolverUVE final : public IAudioClipResolverUVE {
public:
    [[nodiscard]] AudioClipResolutionUVE ResolveAudioClipUVE(std::string_view) const override {
        ++resolveCount;
        if (throwException) {
            throw std::runtime_error("injected resolver exception");
        }
        return AudioClipResolutionUVE{accepted, resolvedPath, diagnostic};
    }

    bool accepted = true;
    bool throwException = false;
    std::string resolvedPath;
    std::string diagnostic;
    mutable int resolveCount = 0;
};

TEST(AudioClipResolutionUVETest, AcceptedResolverPathReachesDeviceAndCopiesIntoSourceState) {
    RecordingAudioDeviceUVE device;
    TestAudioClipResolverUVE resolver;
    resolver.resolvedPath = "audio/derived/explosion.clip";
    AudioSystemUVE audioSystem{device, &resolver};

    AudioSourceDescUVE desc;
    desc.audioAssetPath = "sounds/explosion.wav";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    EXPECT_NE(source, kInvalidVoiceHandleUVE);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 1);
    EXPECT_EQ(device.lastDescription.audioAssetPath, resolver.resolvedPath);
}

TEST(AudioSystemDeviceFailureUVETest, InvalidDeviceVoiceHandleFailsBeforeSourcePublication) {
    RecordingAudioDeviceUVE device;
    device.nextHandle = kInvalidVoiceHandleUVE;
    AudioSystemUVE audioSystem{device};

    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(AudioSourceDescUVE{});

    EXPECT_EQ(source, kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.createCount, 1);
    EXPECT_EQ(audioSystem.GetMixerDiagnosticsUVE().routedSourceCount, 0U);
}

TEST(AudioSystemDeviceFailureUVETest, DuplicateDeviceVoiceHandleFailsBeforeSecondSourcePublication) {
    RecordingAudioDeviceUVE device;
    AudioSystemUVE audioSystem{device};

    const VoiceHandleUVE first = audioSystem.CreateSourceUVE(AudioSourceDescUVE{});
    const VoiceHandleUVE duplicate = audioSystem.CreateSourceUVE(AudioSourceDescUVE{});

    EXPECT_EQ(first, VoiceHandleUVE{1U});
    EXPECT_EQ(duplicate, kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.createCount, 2);
    EXPECT_EQ(device.destroyCount, 1);
    EXPECT_EQ(device.lastHandle, VoiceHandleUVE{1U});
    EXPECT_EQ(audioSystem.GetMixerDiagnosticsUVE().routedSourceCount, 1U);
}

TEST(AudioClipResolutionUVETest, ResolverExceptionFailsAtomicallyBeforeDeviceCreation) {
    RecordingAudioDeviceUVE device;
    TestAudioClipResolverUVE resolver;
    resolver.throwException = true;
    AudioSystemUVE audioSystem{device, &resolver};

    AudioSourceDescUVE desc;
    desc.audioAssetPath = "sounds/throwing.wav";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    EXPECT_EQ(source, kInvalidVoiceHandleUVE);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 0);
    EXPECT_EQ(audioSystem.GetMixerDiagnosticsUVE().routedSourceCount, 0U);
}

TEST(AudioClipResolutionUVETest, RejectedResolverPathFailsAtomicallyBeforeDeviceCreation) {
    RecordingAudioDeviceUVE device;
    TestAudioClipResolverUVE resolver;
    resolver.accepted = false;
    resolver.diagnostic = "clip is missing";
    AudioSystemUVE audioSystem{device, &resolver};

    AudioSourceDescUVE desc;
    desc.audioAssetPath = "sounds/missing.wav";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    EXPECT_EQ(source, kInvalidVoiceHandleUVE);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 0);
}

TEST(AudioSystemSourceValidationUVETest, CreateSourceUVE_RejectsInvalidDescriptorBeforeResolverOrDevice) {
    RecordingAudioDeviceUVE device;
    TestAudioClipResolverUVE resolver;
    AudioSystemUVE audioSystem{device, &resolver};

    AudioSourceDescUVE descriptor;
    descriptor.audioAssetPath = "sounds/invalid.wav";
    descriptor.minDistance = 5.0F;
    descriptor.maxDistance = 5.0F;
    EXPECT_EQ(audioSystem.CreateSourceUVE(descriptor), kInvalidVoiceHandleUVE);

    descriptor = AudioSourceDescUVE{};
    descriptor.pitch = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(audioSystem.CreateSourceUVE(descriptor), kInvalidVoiceHandleUVE);
    EXPECT_EQ(resolver.resolveCount, 0);
    EXPECT_EQ(device.createCount, 0);
}

TEST_F(AudioSystemUVETest, CreateSourceUVE_ForwardsCorrectlyShapedVoiceDescToDevice) {
    AudioSourceDescUVE desc;
    desc.audioAssetPath = "sounds/explosion.wav";
    desc.looping = true;

    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    EXPECT_NE(source, kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 1U);
}

TEST(AudioSystemLifecycleValidationUVETest, UnknownAndInvalidSourceHandlesFailClosedBeforeDeviceCallbacks) {
    RecordingAudioDeviceUVE device;
    AudioSystemUVE audioSystem{device};
    ASSERT_EQ(audioSystem.CreateSourceUVE(AudioSourceDescUVE{}), VoiceHandleUVE{1U});
    const VoiceHandleUVE unknown{99U};

    EXPECT_FALSE(audioSystem.PlayUVE(unknown));
    EXPECT_FALSE(audioSystem.StopUVE(unknown));
    EXPECT_EQ(audioSystem.GetSourceStateUVE(unknown), VoicePlaybackStateUVE::Stopped);
    audioSystem.DestroySourceUVE(unknown);
    EXPECT_FALSE(audioSystem.PlayUVE(kInvalidVoiceHandleUVE));
    EXPECT_FALSE(audioSystem.StopUVE(kInvalidVoiceHandleUVE));
    EXPECT_EQ(audioSystem.GetSourceStateUVE(kInvalidVoiceHandleUVE), VoicePlaybackStateUVE::Stopped);
    audioSystem.DestroySourceUVE(kInvalidVoiceHandleUVE);

    EXPECT_EQ(device.playCount, 0);
    EXPECT_EQ(device.stopCount, 0);
    EXPECT_EQ(device.stateCount, 0);
    EXPECT_EQ(device.destroyCount, 0);
}

TEST_F(AudioSystemUVETest, PlayStopGetSourceStateUVE_RoundTripThroughDevice) {
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(AudioSourceDescUVE{});

    EXPECT_EQ(audioSystem.GetSourceStateUVE(source), VoicePlaybackStateUVE::Stopped);
    EXPECT_TRUE(audioSystem.PlayUVE(source));
    EXPECT_EQ(audioSystem.GetSourceStateUVE(source), VoicePlaybackStateUVE::Playing);
    EXPECT_TRUE(audioSystem.StopUVE(source));
    EXPECT_EQ(audioSystem.GetSourceStateUVE(source), VoicePlaybackStateUVE::Stopped);
}

TEST_F(AudioSystemUVETest, NonSpatialSource_GainEqualsVolumeRegardlessOfPosition) {
    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.volume = 0.75F;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{1000.0F, 0.0F, 0.0F});
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[0]).params.gain, 0.75F);
}

TEST_F(AudioSystemUVETest, SpatialSource_GainMatchesHandComputedLinearAttenuation) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.volume = 1.0F;
    desc.minDistance = 1.0F;
    desc.maxDistance = 9.0F;
    desc.attenuationModel = AudioAttenuationModelUVE::Linear;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{5.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    // distance=5, minDistance=1, maxDistance=9 -> exact midpoint -> gain 0.5.
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[0]).params.gain, 0.5F);
}

TEST_F(AudioSystemUVETest, SpatialSource_OverflowedListenerDistanceFailsClosedToFiniteSilence) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = std::numeric_limits<float>::max();
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    const float maximumFloat = std::numeric_limits<float>::max();

    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{-maximumFloat, 0.0F, 0.0F});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{maximumFloat, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    const AudioVoiceParamsUVE& params = std::get<SetVoiceParamsCallUVE>(recorded[0]).params;
    EXPECT_TRUE(ValidateAudioVoiceParamsUVE(params));
    EXPECT_FLOAT_EQ(params.gain, 0.0F);
}

TEST_F(AudioSystemUVETest, SpatialSource_GainMatchesHandComputedInverseSquareAttenuation) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.volume = 1.0F;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    desc.attenuationModel = AudioAttenuationModelUVE::InverseSquare;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{2.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    // distance=2, minDistance=1 -> (1/2)^2 = 0.25.
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[0]).params.gain, 0.25F);
}

TEST_F(AudioSystemUVETest, MixerGroup_ScalesFinalGainAndPitchBeforeDeviceSubmission) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("SFX", 0.5F, 1.5F));

    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.volume = 0.8F;
    desc.pitch = 1.2F;
    desc.mixerGroup = "SFX";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[0]).params.gain, 0.4F);
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[0]).params.pitch, 1.8F);

    const AudioMixerDiagnosticsUVE diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    ASSERT_EQ(diagnostics.groups.back().name, "SFX");
    EXPECT_EQ(diagnostics.groups.back().sourceCount, 1U);
    EXPECT_EQ(source, VoiceHandleUVE::FromIndexAndGenerationUVE(0U, 1U));
}

TEST_F(AudioSystemUVETest, MixerGroup_RerouteAndDestroyKeepsCopiedCountsConsistent) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music"));
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(AudioSourceDescUVE{});

    ASSERT_TRUE(audioSystem.SetSourceMixerGroupUVE(source, "Music"));
    AudioMixerDiagnosticsUVE diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    ASSERT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(diagnostics.groups[0].sourceCount, 0U);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[1].sourceCount, 1U);
    EXPECT_FALSE(audioSystem.RemoveMixerGroupUVE("Music"));

    audioSystem.DestroySourceUVE(source);
    diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    EXPECT_EQ(diagnostics.routedSourceCount, 0U);
    EXPECT_TRUE(audioSystem.RemoveMixerGroupUVE("Music"));
}

TEST_F(AudioSystemUVETest, MixerGroup_UnknownSourceGroupFallsBackToMaster) {
    AudioSourceDescUVE desc;
    desc.mixerGroup = "Missing";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    const AudioMixerDiagnosticsUVE diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(diagnostics.groups[0].sourceCount, 1U);
    EXPECT_FALSE(audioSystem.SetSourceMixerGroupUVE(source, "Missing"));
}

TEST_F(AudioSystemUVETest, SetListenerPositionAndGetListenerPositionUVE_RoundTrip) {
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{1.0F, 2.0F, 3.0F});
    EXPECT_EQ(audioSystem.GetListenerPositionUVE(), (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
}

TEST_F(AudioSystemUVETest, SetListenerPositionUVE_RejectsNonFiniteAndPreservesLastValidState) {
    const Math::Vector3UVE validPosition{1.0F, 2.0F, 3.0F};
    audioSystem.SetListenerPositionUVE(validPosition);
    audioSystem.SetListenerPositionUVE(
        Math::Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F});

    EXPECT_EQ(audioSystem.GetListenerPositionUVE(), validPosition);
}

TEST_F(AudioSystemUVETest, SetSourcePositionVolumePitchUVE_UnknownHandle_LogsErrorAndIsNoOp) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    audioSystem.SetSourcePositionUVE(VoiceHandleUVE{999}, Math::Vector3UVE{});
    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));

    logger.Shutdown();
}

TEST_F(AudioSystemUVETest, InvalidSourceRuntimeParameters_PreserveLastValidState) {
    AudioSourceDescUVE desc;
    desc.spatial = false;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{2.0F, 3.0F, 4.0F});
    audioSystem.SetSourceVolumeUVE(source, 0.5F);
    audioSystem.SetSourcePitchUVE(source, 1.5F);

    audioSystem.SetSourcePositionUVE(
        source, Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    audioSystem.SetSourceVolumeUVE(source, std::numeric_limits<float>::infinity());
    audioSystem.SetSourcePitchUVE(source, -1.0F);

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);
    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[0]));
    const AudioVoiceParamsUVE& params = std::get<SetVoiceParamsCallUVE>(recorded[0]).params;
    EXPECT_EQ(params.position, (Math::Vector3UVE{2.0F, 3.0F, 4.0F}));
    EXPECT_FLOAT_EQ(params.gain, 0.5F);
    EXPECT_FLOAT_EQ(params.pitch, 1.5F);
}

TEST_F(AudioSystemUVETest, SourceStreamAndPcmEffects_UseBoundedCallerOwnedRuntimeState) {
    AudioSourceDescUVE desc;
    desc.spatial = false;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    ASSERT_NE(source, kInvalidVoiceHandleUVE);

    ASSERT_TRUE(audioSystem.ResetSourceStreamUVE(source, 10U, true, 2U));
    ASSERT_TRUE(audioSystem.ScheduleSourceStreamWindowUVE(source, 4U));
    Pcm16StreamWindowPlanUVE plan;
    ASSERT_TRUE(audioSystem.PopSourceStreamWindowUVE(source, plan));
    EXPECT_EQ(plan, (Pcm16StreamWindowPlanUVE{2U, 4U, 6U, false, false}));

    ASSERT_TRUE(audioSystem.ScheduleSourcePcmGainWindowUVE(source, PcmGainEffectWindowUVE{1U, 2U, 0.5F}));
    const std::vector<float> input{0.2F, 0.8F, -0.4F, 1.0F};
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourcePcmGainEffectsUVE(source, input, output));
    EXPECT_EQ(output, (std::vector<float>{0.2F, 0.4F, -0.2F, 1.0F}));

    std::vector<float> retained{9.0F};
    EXPECT_FALSE(audioSystem.ApplySourcePcmGainEffectsUVE(VoiceHandleUVE{999U}, input, retained));
    EXPECT_EQ(retained, (std::vector<float>{9.0F}));
}

TEST_F(AudioSystemUVETest, UpdateUVE_WithZeroSources_IsSafeNoOp) {
    EXPECT_NO_FATAL_FAILURE(audioSystem.UpdateUVE(0.0F));
}


[[nodiscard]] const AudioVoiceParamsUVE* FindRecordedParamsUVE(
    const std::vector<RecordedAudioCallUVE>& recordedCalls, const VoiceHandleUVE voice) {
    for (const RecordedAudioCallUVE& call : recordedCalls) {
        const auto* paramsCall = std::get_if<SetVoiceParamsCallUVE>(&call);
        if (paramsCall != nullptr && paramsCall->voice == voice) return &paramsCall->params;
    }
    return nullptr;
}


// --- Runtime submixes: the bus tree reaches the device through UpdateUVE(). ---

TEST_F(AudioSystemUVETest, MixerBusGraph_ParentChainScalesTheDeviceGain) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music", kMasterAudioMixerGroupNameUVE, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("MusicStem", "Music", 0.5F, 1.0F));

    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.volume = 1.0F;
    desc.mixerGroup = "MusicStem";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(recorded, source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->gain, 0.25F);
    EXPECT_TRUE(ValidateAudioVoiceParamsUVE(*params));
}

TEST_F(AudioSystemUVETest, MixerBusGraph_MutingABusSilencesItsSourcesAndUnmuteRestores) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music", kMasterAudioMixerGroupNameUVE, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("MusicStem", "Music", 0.5F, 1.0F));

    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.mixerGroup = "MusicStem";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    ASSERT_TRUE(audioSystem.SetMixerGroupMutedUVE("Music", true));
    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);
    const AudioVoiceParamsUVE* muted = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(muted, nullptr);
    EXPECT_FLOAT_EQ(muted->gain, 0.0F);

    ASSERT_TRUE(audioSystem.SetMixerGroupMutedUVE("Music", false));
    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);
    const AudioVoiceParamsUVE* unmuted = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(unmuted, nullptr);
    EXPECT_FLOAT_EQ(unmuted->gain, 0.25F);
}

TEST_F(AudioSystemUVETest, MixerBusGraph_SoloDucksEveryOtherBus) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music", 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("SFX", 1.0F, 1.0F));

    AudioSourceDescUVE musicDesc;
    musicDesc.spatial = false;
    musicDesc.mixerGroup = "Music";
    const VoiceHandleUVE music = audioSystem.CreateSourceUVE(musicDesc);

    AudioSourceDescUVE sfxDesc;
    sfxDesc.spatial = false;
    sfxDesc.mixerGroup = "SFX";
    const VoiceHandleUVE sfx = audioSystem.CreateSourceUVE(sfxDesc);

    ASSERT_TRUE(audioSystem.SetMixerGroupSoloedUVE("Music", true));
    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    const AudioVoiceParamsUVE* musicParams = FindRecordedParamsUVE(recorded, music);
    const AudioVoiceParamsUVE* sfxParams = FindRecordedParamsUVE(recorded, sfx);
    ASSERT_NE(musicParams, nullptr);
    ASSERT_NE(sfxParams, nullptr);
    EXPECT_FLOAT_EQ(musicParams->gain, 0.5F);
    EXPECT_FLOAT_EQ(sfxParams->gain, 0.0F);

    const AudioMixerDiagnosticsUVE diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    EXPECT_TRUE(diagnostics.anySoloed);
}

TEST_F(AudioSystemUVETest, MixerBusGraph_ReparentAndRemovalKeepLiveRoutingConsistent) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music", 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Stem", "Music", 0.5F, 1.0F));

    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.mixerGroup = "Stem";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    // A legal move re-composes the source's chain without touching the voice.
    ASSERT_TRUE(audioSystem.SetMixerGroupParentUVE("Stem", kMasterAudioMixerGroupNameUVE));
    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);
    const AudioVoiceParamsUVE* moved = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(moved, nullptr);
    EXPECT_FLOAT_EQ(moved->gain, 0.5F);

    // Removing the bus that still holds a routed source is refused.
    EXPECT_FALSE(audioSystem.RemoveMixerGroupUVE("Stem"));
    audioSystem.DestroySourceUVE(source);
    EXPECT_TRUE(audioSystem.RemoveMixerGroupUVE("Stem"));
}

TEST_F(AudioSystemUVETest, ApplySourceBusChainUVE_RunsTheRoutedBusChain) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Music", kMasterAudioMixerGroupNameUVE, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroupUVE("Stem", "Music", 0.5F, 1.0F));

    AudioSourceDescUVE desc;
    desc.spatial = false;
    desc.mixerGroup = "Stem";
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    const std::vector<float> input{0.8F};
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourceBusChainUVE(source, input, output));
    ASSERT_EQ(output.size(), 1U);
    // Both buses compose on the sample path: 0.8 * 0.5 (Music) * 0.5 (Stem) = 0.2.
    EXPECT_FLOAT_EQ(output[0], 0.2F);

    ASSERT_TRUE(audioSystem.SetMixerGroupMutedUVE("Music", true));
    ASSERT_TRUE(audioSystem.ApplySourceBusChainUVE(source, input, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
}

TEST_F(AudioSystemUVETest, ApplySourceBusChainUVE_RejectsUnknownHandleAtomically) {
    const std::vector<float> sentinel{0.5F};
    std::vector<float> output = sentinel;
    EXPECT_FALSE(audioSystem.ApplySourceBusChainUVE(VoiceHandleUVE::FromIndexAndGenerationUVE(9U, 9U),
                                                         std::vector<float>{1.0F}, output));
    EXPECT_EQ(output, sentinel);
}

// --- 3D spatialization: stereo pan, directional cone, Doppler. ---

TEST_F(AudioSystemUVETest, SpatialSource_RightOfTheListenerPansRight) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);

    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{4.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_NEAR(params->pan, 1.0F, 1.0e-5F);
    EXPECT_TRUE(ValidateAudioVoiceParamsUVE(*params));
}

TEST_F(AudioSystemUVETest, SpatialSource_LeftOfTheListenerPansLeftAndDirectlyBehindIsCentred) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;

    const VoiceHandleUVE left = audioSystem.CreateSourceUVE(desc);
    const VoiceHandleUVE behind = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    // Default listener orientation is forward (0,0,-1), so +Z is behind the listener.
    audioSystem.SetSourcePositionUVE(left, Math::Vector3UVE{-3.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePositionUVE(behind, Math::Vector3UVE{0.0F, 0.0F, 3.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    const AudioVoiceParamsUVE* leftParams = FindRecordedParamsUVE(recorded, left);
    const AudioVoiceParamsUVE* behindParams = FindRecordedParamsUVE(recorded, behind);
    ASSERT_NE(leftParams, nullptr);
    ASSERT_NE(behindParams, nullptr);
    EXPECT_NEAR(leftParams->pan, -1.0F, 1.0e-5F);
    EXPECT_NEAR(behindParams->pan, 0.0F, 1.0e-5F);
}

TEST_F(AudioSystemUVETest, NonSpatialSource_IsAlwaysCentred) {
    AudioSourceDescUVE desc;
    desc.spatial = false;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{5.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->pan, 0.0F);
}

TEST_F(AudioSystemUVETest, Cone_TurnsDownASourceHeardFromBehindTheEmitter) {
    AudioSourceDescUVE facingListener;
    facingListener.spatial = true;
    facingListener.minDistance = 1.0F;
    facingListener.maxDistance = 100.0F;
    facingListener.coneInnerAngleDegrees = 30.0F;
    facingListener.coneOuterAngleDegrees = 60.0F;
    facingListener.coneOuterGain = 0.1F;
    facingListener.coneDirection = Math::Vector3UVE{-1.0F, 0.0F, 0.0F}; // toward the listener

    AudioSourceDescUVE facingAway = facingListener;
    facingAway.coneDirection = Math::Vector3UVE{1.0F, 0.0F, 0.0F}; // 180 degrees from the listener

    const VoiceHandleUVE near = audioSystem.CreateSourceUVE(facingListener);
    const VoiceHandleUVE far = audioSystem.CreateSourceUVE(facingAway);
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    audioSystem.SetSourcePositionUVE(near, Math::Vector3UVE{6.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePositionUVE(far, Math::Vector3UVE{6.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    const AudioVoiceParamsUVE* nearParams = FindRecordedParamsUVE(recorded, near);
    const AudioVoiceParamsUVE* farParams = FindRecordedParamsUVE(recorded, far);
    ASSERT_NE(nearParams, nullptr);
    ASSERT_NE(farParams, nullptr);
    EXPECT_GT(nearParams->gain, 0.0F);
    // Same distance, same everything except the cone: the behind-the-emitter source is 10x quieter.
    EXPECT_NEAR(farParams->gain / nearParams->gain, 0.1F, 1.0e-4F);
}

TEST_F(AudioSystemUVETest, Doppler_ApproachingSourceRaisesThePitch) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{20.0F, 0.0F, 0.0F});

    const float frameSeconds = 1.0F / 60.0F;
    audioSystem.UpdateUVE(frameSeconds); // seeds the previous position, no velocity yet
    // 20 m/s straight at the listener: 1/3 m over one frame.
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{20.0F - 20.0F * frameSeconds, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(frameSeconds);

    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(params, nullptr);
    // 343 / (343 - 20) = 1.0619.
    EXPECT_NEAR(params->pitch, 343.0F / 323.0F, 1.0e-3F);
}

TEST_F(AudioSystemUVETest, Doppler_FirstFrameAfterSpawnDoesNotInventAVelocity) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    // Spawned far from the origin: the very first frame must NOT read that as a displacement.
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{200.0F, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(1.0F / 60.0F);

    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->pitch, 1.0F);
}

TEST_F(AudioSystemUVETest, Doppler_RecedingSourceLowersThePitchAndZeroDeltaDisablesIt) {
    AudioSourceDescUVE desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    const VoiceHandleUVE source = audioSystem.CreateSourceUVE(desc);
    audioSystem.SetListenerPositionUVE(Math::Vector3UVE{});
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{20.0F, 0.0F, 0.0F});

    const float frameSeconds = 1.0F / 60.0F;
    audioSystem.UpdateUVE(frameSeconds);
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{20.0F + 20.0F * frameSeconds, 0.0F, 0.0F});

    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(frameSeconds);
    const AudioVoiceParamsUVE* params = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_NEAR(params->pitch, 343.0F / 363.0F, 1.0e-3F);

    // Same motion, but a caller that does not track time gets no Doppler at all.
    audioSystem.SetSourcePositionUVE(source, Math::Vector3UVE{21.0F, 0.0F, 0.0F});
    device.ClearRecordedCallsUVE();
    audioSystem.UpdateUVE(0.0F);
    const AudioVoiceParamsUVE* still = FindRecordedParamsUVE(device.GetRecordedCallsUVE(), source);
    ASSERT_NE(still, nullptr);
    EXPECT_FLOAT_EQ(still->pitch, 1.0F);
}

TEST_F(AudioSystemUVETest, SourceDescriptor_RejectsAnInvertedConeOrAnOutOfRangeDopplerFactor) {
    AudioSourceDescUVE desc;
    desc.coneInnerAngleDegrees = 90.0F;
    desc.coneOuterAngleDegrees = 45.0F;
    EXPECT_EQ(audioSystem.CreateSourceUVE(desc), kInvalidVoiceHandleUVE);

    AudioSourceDescUVE badDoppler;
    badDoppler.dopplerFactor = 2.0F;
    EXPECT_EQ(audioSystem.CreateSourceUVE(badDoppler), kInvalidVoiceHandleUVE);

    AudioSourceDescUVE badAxis;
    badAxis.coneDirection = Math::Vector3UVE{};
    EXPECT_EQ(audioSystem.CreateSourceUVE(badAxis), kInvalidVoiceHandleUVE);
}

} // namespace
} // namespace UVE::Audio::Tests
