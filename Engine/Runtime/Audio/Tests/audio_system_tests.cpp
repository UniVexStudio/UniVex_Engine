// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_system.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "null_audio_device.h"
#include "log_sink.h"
#include "logger.h"

namespace UVE::Audio::Tests {
namespace {

class AudioSystemTest : public ::testing::Test {
protected:
    NullAudioDevice device;
    AudioSystem audioSystem{device};
};

[[nodiscard]] bool LoggedAnError(const Debug::MemorySink& sink) {
    const std::vector<Debug::LogMessage> messages = sink.GetMessages();
    return std::any_of(messages.begin(), messages.end(),
                        [](const Debug::LogMessage& message) { return message.level == Debug::LogLevel::Error; });
}

class RecordingAudioDevice final : public IAudioDevice {
public:
    [[nodiscard]] VoiceHandle CreateVoice(const AudioVoiceDesc& desc) override {
        lastDescription = desc;
        ++createCount;
        return nextHandle;
    }
    void DestroyVoice(VoiceHandle voice) override {
        lastHandle = voice;
        ++destroyCount;
    }
    [[nodiscard]] bool Play(VoiceHandle voice) override {
        lastHandle = voice;
        ++playCount;
        return true;
    }
    [[nodiscard]] bool Stop(VoiceHandle voice) override {
        lastHandle = voice;
        ++stopCount;
        return true;
    }
    [[nodiscard]] bool SetVoiceParams(VoiceHandle, const AudioVoiceParams&) override { return true; }
    [[nodiscard]] VoicePlaybackState GetVoiceState(VoiceHandle voice) const override {
        lastHandle = voice;
        ++stateCount;
        return VoicePlaybackState::Stopped;
    }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Recording"; }

    [[nodiscard]] VoiceHandle CreateStreamVoice(const AudioStreamFormat&) override {
        return kInvalidVoiceHandle;
    }
    [[nodiscard]] std::size_t PushStreamFrames(VoiceHandle, std::span<const float>) override { return 0U; }
    [[nodiscard]] bool EndStream(VoiceHandle) override { return false; }
    [[nodiscard]] std::size_t GetStreamBufferedFrames(VoiceHandle) const override { return 0U; }
    [[nodiscard]] std::size_t GetStreamFreeFrames(VoiceHandle) const override { return 0U; }

    /// The recorder has no audio thread, so the test states what each voice "contributed"; every
    /// read is idempotent, matching the interface contract a real backend has to satisfy.
    [[nodiscard]] AudioMeterReading ReadVoiceMeter(const VoiceHandle voice) const override {
        ++meterReadCount;
        const auto match = meterByHandle.find(voice.value);
        return match == meterByHandle.end() ? kSilentAudioMeter : match->second;
    }

    AudioVoiceDesc lastDescription{};
    mutable VoiceHandle lastHandle = kInvalidVoiceHandle;
    VoiceHandle nextHandle{1U};
    int createCount = 0;
    mutable int destroyCount = 0;
    mutable int playCount = 0;
    mutable int stopCount = 0;
    mutable int stateCount = 0;
    mutable int meterReadCount = 0;
    std::unordered_map<std::uint32_t, AudioMeterReading> meterByHandle;
};

class TestAudioClipResolver final : public IAudioClipResolver {
public:
    [[nodiscard]] AudioClipResolution ResolveAudioClip(std::string_view) const override {
        ++resolveCount;
        if (throwException) {
            throw std::runtime_error("injected resolver exception");
        }
        return AudioClipResolution{accepted, resolvedPath, diagnostic};
    }

    bool accepted = true;
    bool throwException = false;
    std::string resolvedPath;
    std::string diagnostic;
    mutable int resolveCount = 0;
};

TEST(AudioClipResolutionTest, AcceptedResolverPathReachesDeviceAndCopiesIntoSourceState) {
    RecordingAudioDevice device;
    TestAudioClipResolver resolver;
    resolver.resolvedPath = "audio/derived/explosion.clip";
    AudioSystem audioSystem{device, &resolver};

    AudioSourceDesc desc;
    desc.audioAssetPath = "sounds/explosion.wav";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    EXPECT_NE(source, kInvalidVoiceHandle);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 1);
    EXPECT_EQ(device.lastDescription.audioAssetPath, resolver.resolvedPath);
}

TEST(AudioSystemDeviceFailureTest, InvalidDeviceVoiceHandleFailsBeforeSourcePublication) {
    RecordingAudioDevice device;
    device.nextHandle = kInvalidVoiceHandle;
    AudioSystem audioSystem{device};

    const VoiceHandle source = audioSystem.CreateSource(AudioSourceDesc{});

    EXPECT_EQ(source, kInvalidVoiceHandle);
    EXPECT_EQ(device.createCount, 1);
    EXPECT_EQ(audioSystem.GetMixerDiagnostics().routedSourceCount, 0U);
}

TEST(AudioSystemDeviceFailureTest, DuplicateDeviceVoiceHandleFailsBeforeSecondSourcePublication) {
    RecordingAudioDevice device;
    AudioSystem audioSystem{device};

    const VoiceHandle first = audioSystem.CreateSource(AudioSourceDesc{});
    const VoiceHandle duplicate = audioSystem.CreateSource(AudioSourceDesc{});

    EXPECT_EQ(first, VoiceHandle{1U});
    EXPECT_EQ(duplicate, kInvalidVoiceHandle);
    EXPECT_EQ(device.createCount, 2);
    EXPECT_EQ(device.destroyCount, 1);
    EXPECT_EQ(device.lastHandle, VoiceHandle{1U});
    EXPECT_EQ(audioSystem.GetMixerDiagnostics().routedSourceCount, 1U);
}

TEST(AudioClipResolutionTest, ResolverExceptionFailsAtomicallyBeforeDeviceCreation) {
    RecordingAudioDevice device;
    TestAudioClipResolver resolver;
    resolver.throwException = true;
    AudioSystem audioSystem{device, &resolver};

    AudioSourceDesc desc;
    desc.audioAssetPath = "sounds/throwing.wav";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    EXPECT_EQ(source, kInvalidVoiceHandle);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 0);
    EXPECT_EQ(audioSystem.GetMixerDiagnostics().routedSourceCount, 0U);
}

TEST(AudioClipResolutionTest, RejectedResolverPathFailsAtomicallyBeforeDeviceCreation) {
    RecordingAudioDevice device;
    TestAudioClipResolver resolver;
    resolver.accepted = false;
    resolver.diagnostic = "clip is missing";
    AudioSystem audioSystem{device, &resolver};

    AudioSourceDesc desc;
    desc.audioAssetPath = "sounds/missing.wav";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    EXPECT_EQ(source, kInvalidVoiceHandle);
    EXPECT_EQ(resolver.resolveCount, 1);
    EXPECT_EQ(device.createCount, 0);
}

TEST(AudioSystemSourceValidationTest, CreateSource_RejectsInvalidDescriptorBeforeResolverOrDevice) {
    RecordingAudioDevice device;
    TestAudioClipResolver resolver;
    AudioSystem audioSystem{device, &resolver};

    AudioSourceDesc descriptor;
    descriptor.audioAssetPath = "sounds/invalid.wav";
    descriptor.minDistance = 5.0F;
    descriptor.maxDistance = 5.0F;
    EXPECT_EQ(audioSystem.CreateSource(descriptor), kInvalidVoiceHandle);

    descriptor = AudioSourceDesc{};
    descriptor.pitch = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(audioSystem.CreateSource(descriptor), kInvalidVoiceHandle);
    EXPECT_EQ(resolver.resolveCount, 0);
    EXPECT_EQ(device.createCount, 0);
}

TEST_F(AudioSystemTest, CreateSource_ForwardsCorrectlyShapedVoiceDescToDevice) {
    AudioSourceDesc desc;
    desc.audioAssetPath = "sounds/explosion.wav";
    desc.looping = true;

    const VoiceHandle source = audioSystem.CreateSource(desc);
    EXPECT_NE(source, kInvalidVoiceHandle);
    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
}

TEST(AudioSystemLifecycleValidationTest, UnknownAndInvalidSourceHandlesFailClosedBeforeDeviceCallbacks) {
    RecordingAudioDevice device;
    AudioSystem audioSystem{device};
    ASSERT_EQ(audioSystem.CreateSource(AudioSourceDesc{}), VoiceHandle{1U});
    const VoiceHandle unknown{99U};

    EXPECT_FALSE(audioSystem.Play(unknown));
    EXPECT_FALSE(audioSystem.Stop(unknown));
    EXPECT_EQ(audioSystem.GetSourceState(unknown), VoicePlaybackState::Stopped);
    audioSystem.DestroySource(unknown);
    EXPECT_FALSE(audioSystem.Play(kInvalidVoiceHandle));
    EXPECT_FALSE(audioSystem.Stop(kInvalidVoiceHandle));
    EXPECT_EQ(audioSystem.GetSourceState(kInvalidVoiceHandle), VoicePlaybackState::Stopped);
    audioSystem.DestroySource(kInvalidVoiceHandle);

    EXPECT_EQ(device.playCount, 0);
    EXPECT_EQ(device.stopCount, 0);
    EXPECT_EQ(device.stateCount, 0);
    EXPECT_EQ(device.destroyCount, 0);
}

TEST_F(AudioSystemTest, PlayStopGetSourceState_RoundTripThroughDevice) {
    const VoiceHandle source = audioSystem.CreateSource(AudioSourceDesc{});

    EXPECT_EQ(audioSystem.GetSourceState(source), VoicePlaybackState::Stopped);
    EXPECT_TRUE(audioSystem.Play(source));
    EXPECT_EQ(audioSystem.GetSourceState(source), VoicePlaybackState::Playing);
    EXPECT_TRUE(audioSystem.Stop(source));
    EXPECT_EQ(audioSystem.GetSourceState(source), VoicePlaybackState::Stopped);
}

TEST_F(AudioSystemTest, NonSpatialSource_GainEqualsVolumeRegardlessOfPosition) {
    AudioSourceDesc desc;
    desc.spatial = false;
    desc.volume = 0.75F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{1000.0F, 0.0F, 0.0F});
    audioSystem.SetListenerPosition(Math::Vector3{});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCall>(recorded[0]).params.gain, 0.75F);
}

TEST_F(AudioSystemTest, SpatialSource_GainMatchesHandComputedLinearAttenuation) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.volume = 1.0F;
    desc.minDistance = 1.0F;
    desc.maxDistance = 9.0F;
    desc.attenuationModel = AudioAttenuationModel::Linear;
    const VoiceHandle source = audioSystem.CreateSource(desc);

    audioSystem.SetListenerPosition(Math::Vector3{0.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePosition(source, Math::Vector3{5.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    // distance=5, minDistance=1, maxDistance=9 -> exact midpoint -> gain 0.5.
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCall>(recorded[0]).params.gain, 0.5F);
}

TEST_F(AudioSystemTest, SpatialSource_OverflowedListenerDistanceFailsClosedToFiniteSilence) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = std::numeric_limits<float>::max();
    const VoiceHandle source = audioSystem.CreateSource(desc);
    const float maximumFloat = std::numeric_limits<float>::max();

    audioSystem.SetListenerPosition(Math::Vector3{-maximumFloat, 0.0F, 0.0F});
    audioSystem.SetSourcePosition(source, Math::Vector3{maximumFloat, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    const AudioVoiceParams& params = std::get<SetVoiceParamsCall>(recorded[0]).params;
    EXPECT_TRUE(ValidateAudioVoiceParams(params));
    EXPECT_FLOAT_EQ(params.gain, 0.0F);
}

TEST_F(AudioSystemTest, SpatialSource_GainMatchesHandComputedInverseSquareAttenuation) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.volume = 1.0F;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    desc.attenuationModel = AudioAttenuationModel::InverseSquare;
    const VoiceHandle source = audioSystem.CreateSource(desc);

    audioSystem.SetListenerPosition(Math::Vector3{0.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePosition(source, Math::Vector3{2.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    // distance=2, minDistance=1 -> (1/2)^2 = 0.25.
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCall>(recorded[0]).params.gain, 0.25F);
}

TEST_F(AudioSystemTest, MixerGroup_ScalesFinalGainAndPitchBeforeDeviceSubmission) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("SFX", 0.5F, 1.5F));

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.volume = 0.8F;
    desc.pitch = 1.2F;
    desc.mixerGroup = "SFX";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCall>(recorded[0]).params.gain, 0.4F);
    EXPECT_FLOAT_EQ(std::get<SetVoiceParamsCall>(recorded[0]).params.pitch, 1.8F);

    const AudioMixerDiagnostics diagnostics = audioSystem.GetMixerDiagnostics();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    ASSERT_EQ(diagnostics.groups.back().name, "SFX");
    EXPECT_EQ(diagnostics.groups.back().sourceCount, 1U);
    EXPECT_EQ(source, VoiceHandle::FromIndexAndGeneration(0U, 1U));
}

TEST_F(AudioSystemTest, MixerGroup_RerouteAndDestroyKeepsCopiedCountsConsistent) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music"));
    const VoiceHandle source = audioSystem.CreateSource(AudioSourceDesc{});

    ASSERT_TRUE(audioSystem.SetSourceMixerGroup(source, "Music"));
    AudioMixerDiagnostics diagnostics = audioSystem.GetMixerDiagnostics();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    ASSERT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupName);
    EXPECT_EQ(diagnostics.groups[0].sourceCount, 0U);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[1].sourceCount, 1U);
    EXPECT_FALSE(audioSystem.RemoveMixerGroup("Music"));

    audioSystem.DestroySource(source);
    diagnostics = audioSystem.GetMixerDiagnostics();
    EXPECT_EQ(diagnostics.routedSourceCount, 0U);
    EXPECT_TRUE(audioSystem.RemoveMixerGroup("Music"));
}

TEST_F(AudioSystemTest, MixerGroup_UnknownSourceGroupFallsBackToMaster) {
    AudioSourceDesc desc;
    desc.mixerGroup = "Missing";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    const AudioMixerDiagnostics diagnostics = audioSystem.GetMixerDiagnostics();
    ASSERT_EQ(diagnostics.routedSourceCount, 1U);
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupName);
    EXPECT_EQ(diagnostics.groups[0].sourceCount, 1U);
    EXPECT_FALSE(audioSystem.SetSourceMixerGroup(source, "Missing"));
}

TEST_F(AudioSystemTest, SetListenerPositionAndGetListenerPosition_RoundTrip) {
    audioSystem.SetListenerPosition(Math::Vector3{1.0F, 2.0F, 3.0F});
    EXPECT_EQ(audioSystem.GetListenerPosition(), (Math::Vector3{1.0F, 2.0F, 3.0F}));
}

TEST_F(AudioSystemTest, SetListenerPosition_RejectsNonFiniteAndPreservesLastValidState) {
    const Math::Vector3 validPosition{1.0F, 2.0F, 3.0F};
    audioSystem.SetListenerPosition(validPosition);
    audioSystem.SetListenerPosition(
        Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F});

    EXPECT_EQ(audioSystem.GetListenerPosition(), validPosition);
}

TEST_F(AudioSystemTest, SetSourcePositionVolumePitch_UnknownHandle_LogsErrorAndIsNoOp) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    audioSystem.SetSourcePosition(VoiceHandle{999}, Math::Vector3{});
    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));

    logger.Shutdown();
}

TEST_F(AudioSystemTest, InvalidSourceRuntimeParameters_PreserveLastValidState) {
    AudioSourceDesc desc;
    desc.spatial = false;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{2.0F, 3.0F, 4.0F});
    audioSystem.SetSourceVolume(source, 0.5F);
    audioSystem.SetSourcePitch(source, 1.5F);

    audioSystem.SetSourcePosition(
        source, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    audioSystem.SetSourceVolume(source, std::numeric_limits<float>::infinity());
    audioSystem.SetSourcePitch(source, -1.0F);

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 1U);
    ASSERT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[0]));
    const AudioVoiceParams& params = std::get<SetVoiceParamsCall>(recorded[0]).params;
    EXPECT_EQ(params.position, (Math::Vector3{2.0F, 3.0F, 4.0F}));
    EXPECT_FLOAT_EQ(params.gain, 0.5F);
    EXPECT_FLOAT_EQ(params.pitch, 1.5F);
}

TEST_F(AudioSystemTest, SourceStreamAndPcmEffects_UseBoundedCallerOwnedRuntimeState) {
    AudioSourceDesc desc;
    desc.spatial = false;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    ASSERT_NE(source, kInvalidVoiceHandle);

    ASSERT_TRUE(audioSystem.ResetSourceStream(source, 10U, true, 2U));
    ASSERT_TRUE(audioSystem.ScheduleSourceStreamWindow(source, 4U));
    Pcm16StreamWindowPlan plan;
    ASSERT_TRUE(audioSystem.PopSourceStreamWindow(source, plan));
    EXPECT_EQ(plan, (Pcm16StreamWindowPlan{2U, 4U, 6U, false, false}));

    ASSERT_TRUE(audioSystem.ScheduleSourcePcmGainWindow(source, PcmGainEffectWindow{1U, 2U, 0.5F}));
    const std::vector<float> input{0.2F, 0.8F, -0.4F, 1.0F};
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourcePcmGainEffects(source, input, output));
    EXPECT_EQ(output, (std::vector<float>{0.2F, 0.4F, -0.2F, 1.0F}));

    std::vector<float> retained{9.0F};
    EXPECT_FALSE(audioSystem.ApplySourcePcmGainEffects(VoiceHandle{999U}, input, retained));
    EXPECT_EQ(retained, (std::vector<float>{9.0F}));
}

TEST_F(AudioSystemTest, Update_WithZeroSources_IsSafeNoOp) {
    EXPECT_NO_FATAL_FAILURE(audioSystem.Update(0.0F));
}


[[nodiscard]] const AudioVoiceParams* FindRecordedParams(
    const std::vector<RecordedAudioCall>& recordedCalls, const VoiceHandle voice) {
    for (const RecordedAudioCall& call : recordedCalls) {
        const auto* paramsCall = std::get_if<SetVoiceParamsCall>(&call);
        if (paramsCall != nullptr && paramsCall->voice == voice) return &paramsCall->params;
    }
    return nullptr;
}


// --- Runtime submixes: the bus tree reaches the device through Update(). ---

TEST_F(AudioSystemTest, MixerBusGraph_ParentChainScalesTheDeviceGain) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", kMasterAudioMixerGroupName, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("MusicStem", "Music", 0.5F, 1.0F));

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.volume = 1.0F;
    desc.mixerGroup = "MusicStem";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const AudioVoiceParams* params = FindRecordedParams(recorded, source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->gain, 0.25F);
    EXPECT_TRUE(ValidateAudioVoiceParams(*params));
}

TEST_F(AudioSystemTest, MixerBusGraph_MutingABusSilencesItsSourcesAndUnmuteRestores) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", kMasterAudioMixerGroupName, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("MusicStem", "Music", 0.5F, 1.0F));

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.mixerGroup = "MusicStem";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    ASSERT_TRUE(audioSystem.SetMixerGroupMuted("Music", true));
    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const AudioVoiceParams* muted = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(muted, nullptr);
    EXPECT_FLOAT_EQ(muted->gain, 0.0F);

    ASSERT_TRUE(audioSystem.SetMixerGroupMuted("Music", false));
    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const AudioVoiceParams* unmuted = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(unmuted, nullptr);
    EXPECT_FLOAT_EQ(unmuted->gain, 0.25F);
}

TEST_F(AudioSystemTest, MixerBusGraph_SoloDucksEveryOtherBus) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("SFX", 1.0F, 1.0F));

    AudioSourceDesc musicDesc;
    musicDesc.spatial = false;
    musicDesc.mixerGroup = "Music";
    const VoiceHandle music = audioSystem.CreateSource(musicDesc);

    AudioSourceDesc sfxDesc;
    sfxDesc.spatial = false;
    sfxDesc.mixerGroup = "SFX";
    const VoiceHandle sfx = audioSystem.CreateSource(sfxDesc);

    ASSERT_TRUE(audioSystem.SetMixerGroupSoloed("Music", true));
    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const AudioVoiceParams* musicParams = FindRecordedParams(recorded, music);
    const AudioVoiceParams* sfxParams = FindRecordedParams(recorded, sfx);
    ASSERT_NE(musicParams, nullptr);
    ASSERT_NE(sfxParams, nullptr);
    EXPECT_FLOAT_EQ(musicParams->gain, 0.5F);
    EXPECT_FLOAT_EQ(sfxParams->gain, 0.0F);

    const AudioMixerDiagnostics diagnostics = audioSystem.GetMixerDiagnostics();
    EXPECT_TRUE(diagnostics.anySoloed);
}

TEST_F(AudioSystemTest, MixerBusGraph_ReparentAndRemovalKeepLiveRoutingConsistent) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Stem", "Music", 0.5F, 1.0F));

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.mixerGroup = "Stem";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    // A legal move re-composes the source's chain without touching the voice.
    ASSERT_TRUE(audioSystem.SetMixerGroupParent("Stem", kMasterAudioMixerGroupName));
    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const AudioVoiceParams* moved = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(moved, nullptr);
    EXPECT_FLOAT_EQ(moved->gain, 0.5F);

    // Removing the bus that still holds a routed source is refused.
    EXPECT_FALSE(audioSystem.RemoveMixerGroup("Stem"));
    audioSystem.DestroySource(source);
    EXPECT_TRUE(audioSystem.RemoveMixerGroup("Stem"));
}

TEST_F(AudioSystemTest, ApplySourceBusChain_RunsTheRoutedBusChain) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", kMasterAudioMixerGroupName, 0.5F, 1.0F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Stem", "Music", 0.5F, 1.0F));

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.mixerGroup = "Stem";
    const VoiceHandle source = audioSystem.CreateSource(desc);

    const std::vector<float> input{0.8F};
    std::vector<float> output;
    ASSERT_TRUE(audioSystem.ApplySourceBusChain(source, input, output));
    ASSERT_EQ(output.size(), 1U);
    // Both buses compose on the sample path: 0.8 * 0.5 (Music) * 0.5 (Stem) = 0.2.
    EXPECT_FLOAT_EQ(output[0], 0.2F);

    ASSERT_TRUE(audioSystem.SetMixerGroupMuted("Music", true));
    ASSERT_TRUE(audioSystem.ApplySourceBusChain(source, input, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
}

TEST_F(AudioSystemTest, ApplySourceBusChain_RejectsUnknownHandleAtomically) {
    const std::vector<float> sentinel{0.5F};
    std::vector<float> output = sentinel;
    EXPECT_FALSE(audioSystem.ApplySourceBusChain(VoiceHandle::FromIndexAndGeneration(9U, 9U),
                                                         std::vector<float>{1.0F}, output));
    EXPECT_EQ(output, sentinel);
}

// --- 3D spatialization: stereo pan, directional cone, Doppler. ---

TEST_F(AudioSystemTest, SpatialSource_RightOfTheListenerPansRight) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);

    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(source, Math::Vector3{4.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_NEAR(params->pan, 1.0F, 1.0e-5F);
    EXPECT_TRUE(ValidateAudioVoiceParams(*params));
}

TEST_F(AudioSystemTest, SpatialSource_LeftOfTheListenerPansLeftAndDirectlyBehindIsCentred) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;

    const VoiceHandle left = audioSystem.CreateSource(desc);
    const VoiceHandle behind = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{});
    // Default listener orientation is forward (0,0,-1), so +Z is behind the listener.
    audioSystem.SetSourcePosition(left, Math::Vector3{-3.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePosition(behind, Math::Vector3{0.0F, 0.0F, 3.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const AudioVoiceParams* leftParams = FindRecordedParams(recorded, left);
    const AudioVoiceParams* behindParams = FindRecordedParams(recorded, behind);
    ASSERT_NE(leftParams, nullptr);
    ASSERT_NE(behindParams, nullptr);
    EXPECT_NEAR(leftParams->pan, -1.0F, 1.0e-5F);
    EXPECT_NEAR(behindParams->pan, 0.0F, 1.0e-5F);
}

TEST_F(AudioSystemTest, NonSpatialSource_IsAlwaysCentred) {
    AudioSourceDesc desc;
    desc.spatial = false;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{5.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->pan, 0.0F);
}

TEST_F(AudioSystemTest, Cone_TurnsDownASourceHeardFromBehindTheEmitter) {
    AudioSourceDesc facingListener;
    facingListener.spatial = true;
    facingListener.minDistance = 1.0F;
    facingListener.maxDistance = 100.0F;
    facingListener.coneInnerAngleDegrees = 30.0F;
    facingListener.coneOuterAngleDegrees = 60.0F;
    facingListener.coneOuterGain = 0.1F;
    facingListener.coneDirection = Math::Vector3{-1.0F, 0.0F, 0.0F}; // toward the listener

    AudioSourceDesc facingAway = facingListener;
    facingAway.coneDirection = Math::Vector3{1.0F, 0.0F, 0.0F}; // 180 degrees from the listener

    const VoiceHandle near = audioSystem.CreateSource(facingListener);
    const VoiceHandle far = audioSystem.CreateSource(facingAway);
    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(near, Math::Vector3{6.0F, 0.0F, 0.0F});
    audioSystem.SetSourcePosition(far, Math::Vector3{6.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const AudioVoiceParams* nearParams = FindRecordedParams(recorded, near);
    const AudioVoiceParams* farParams = FindRecordedParams(recorded, far);
    ASSERT_NE(nearParams, nullptr);
    ASSERT_NE(farParams, nullptr);
    EXPECT_GT(nearParams->gain, 0.0F);
    // Same distance, same everything except the cone: the behind-the-emitter source is 10x quieter.
    EXPECT_NEAR(farParams->gain / nearParams->gain, 0.1F, 1.0e-4F);
}

TEST_F(AudioSystemTest, Doppler_ApproachingSourceRaisesThePitch) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(source, Math::Vector3{20.0F, 0.0F, 0.0F});

    const float frameSeconds = 1.0F / 60.0F;
    audioSystem.Update(frameSeconds); // seeds the previous position, no velocity yet
    // 20 m/s straight at the listener: 1/3 m over one frame.
    audioSystem.SetSourcePosition(source, Math::Vector3{20.0F - 20.0F * frameSeconds, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(frameSeconds);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    // 343 / (343 - 20) = 1.0619.
    EXPECT_NEAR(params->pitch, 343.0F / 323.0F, 1.0e-3F);
}

TEST_F(AudioSystemTest, Doppler_FirstFrameAfterSpawnDoesNotInventAVelocity) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    // Spawned far from the origin: the very first frame must NOT read that as a displacement.
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(source, Math::Vector3{200.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(1.0F / 60.0F);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->pitch, 1.0F);
}

TEST_F(AudioSystemTest, Doppler_RecedingSourceLowersThePitchAndZeroDeltaDisablesIt) {
    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 1000.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(source, Math::Vector3{20.0F, 0.0F, 0.0F});

    const float frameSeconds = 1.0F / 60.0F;
    audioSystem.Update(frameSeconds);
    audioSystem.SetSourcePosition(source, Math::Vector3{20.0F + 20.0F * frameSeconds, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(frameSeconds);
    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_NEAR(params->pitch, 343.0F / 363.0F, 1.0e-3F);

    // Same motion, but a caller that does not track time gets no Doppler at all.
    audioSystem.SetSourcePosition(source, Math::Vector3{21.0F, 0.0F, 0.0F});
    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const AudioVoiceParams* still = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(still, nullptr);
    EXPECT_FLOAT_EQ(still->pitch, 1.0F);
}

TEST_F(AudioSystemTest, SourceDescriptor_RejectsAnInvertedConeOrAnOutOfRangeDopplerFactor) {
    AudioSourceDesc desc;
    desc.coneInnerAngleDegrees = 90.0F;
    desc.coneOuterAngleDegrees = 45.0F;
    EXPECT_EQ(audioSystem.CreateSource(desc), kInvalidVoiceHandle);

    AudioSourceDesc badDoppler;
    badDoppler.dopplerFactor = 2.0F;
    EXPECT_EQ(audioSystem.CreateSource(badDoppler), kInvalidVoiceHandle);

    AudioSourceDesc badAxis;
    badAxis.coneDirection = Math::Vector3{};
    EXPECT_EQ(audioSystem.CreateSource(badAxis), kInvalidVoiceHandle);
}

// --- Streaming sources: bounded memory, pumped once per frame. ---

class TestStreamSource final : public IAudioStreamSource {
public:
    [[nodiscard]] AudioStreamFormat GetFormat() const noexcept override { return format; }

    [[nodiscard]] std::size_t GetFrameCount() const noexcept override {
        return format.channels == 0U ? 0U : frames.size() / static_cast<std::size_t>(format.channels);
    }

    [[nodiscard]] std::size_t ReadFrames(const std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override {
        outFrames.clear();
        const std::size_t channels = static_cast<std::size_t>(format.channels);
        const std::size_t total = channels == 0U ? 0U : frames.size() / channels;
        const std::size_t available = total > cursor ? total - cursor : 0U;
        const std::size_t count = std::min(requestedFrames, available);
        outFrames.insert(outFrames.end(),
                         frames.begin() + static_cast<std::ptrdiff_t>(cursor * channels),
                         frames.begin() + static_cast<std::ptrdiff_t>((cursor + count) * channels));
        cursor += count;
        ++readCount;
        return count;
    }

    [[nodiscard]] bool SeekToFrame(const std::size_t frame) override {
        if (!seekable || frame > GetFrameCount()) {
            return false;
        }
        cursor = frame;
        ++seekCount;
        return true;
    }

    AudioStreamFormat format{48000U, 1U};
    std::vector<float> frames;
    std::size_t cursor = 0U;
    bool seekable = true;
    int readCount = 0;
    int seekCount = 0;
};

TEST_F(AudioSystemTest, StreamingSource_PumpsIntoTheDeviceRingOncePerFrame) {
    TestStreamSource source;
    source.frames.assign(48000U, 0.1F);

    AudioSourceDesc desc;
    desc.spatial = false;
    const VoiceHandle voice = audioSystem.CreateStreamingSource(desc, source);
    ASSERT_NE(voice, kInvalidVoiceHandle);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), 0U);

    audioSystem.Update(0.0F);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), kAudioStreamRingFrames);
    EXPECT_EQ(source.readCount, 1);

    // The ring is full, so the next frame has nothing to take - and the source is not asked again.
    audioSystem.Update(0.0F);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), kAudioStreamRingFrames);
    EXPECT_EQ(source.readCount, 1);
}

TEST_F(AudioSystemTest, StreamingSource_LoopsBackToTheFirstFrameWhenAsked) {
    TestStreamSource source;
    source.frames = {0.1F, 0.2F, 0.3F, 0.4F};

    AudioSourceDesc desc;
    desc.spatial = false;
    desc.looping = true;
    const VoiceHandle voice = audioSystem.CreateStreamingSource(desc, source);
    ASSERT_NE(voice, kInvalidVoiceHandle);

    audioSystem.Update(0.0F); // the four frames go in
    audioSystem.Update(0.0F); // the source is spent, so it is rewound instead of ended

    EXPECT_EQ(source.seekCount, 1);
    EXPECT_EQ(source.cursor, 4U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), 8U);
}

TEST_F(AudioSystemTest, StreamingSource_EndsAndTheVoiceStopsOnlyAfterItDrains) {
    TestStreamSource source;
    source.frames = {0.1F, 0.2F, 0.3F, 0.4F};

    AudioSourceDesc desc;
    desc.spatial = false;
    const VoiceHandle voice = audioSystem.CreateStreamingSource(desc, source);
    ASSERT_NE(voice, kInvalidVoiceHandle);

    ASSERT_TRUE(audioSystem.Play(voice));
    audioSystem.Update(0.0F); // four frames pushed
    audioSystem.Update(0.0F); // source spent and not looping: the stream ends

    EXPECT_EQ(audioSystem.GetSourceState(voice), VoicePlaybackState::Playing);
    device.ConsumeStreamFrames(voice, 4U);
    EXPECT_EQ(audioSystem.GetSourceState(voice), VoicePlaybackState::Stopped);
    // An ended stream stays ended: pumping again is a no-op rather than a silent restart.
    EXPECT_EQ(audioSystem.PumpStreamingSource(voice), 0U);
}

TEST_F(AudioSystemTest, ClipSourcesAreNotStreams) {
    const VoiceHandle voice = audioSystem.CreateSource(AudioSourceDesc{});
    ASSERT_NE(voice, kInvalidVoiceHandle);
    EXPECT_EQ(audioSystem.PumpStreamingSource(voice), 0U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), 0U);
}

TEST_F(AudioSystemTest, StreamingSource_StillGetsPanAndDistanceGain) {
    TestStreamSource source;
    source.frames.assign(1000U, 0.5F);

    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    const VoiceHandle voice = audioSystem.CreateStreamingSource(desc, source);
    ASSERT_NE(voice, kInvalidVoiceHandle);

    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(voice, Math::Vector3{4.0F, 0.0F, 0.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), voice);
    ASSERT_NE(params, nullptr);
    EXPECT_NEAR(params->pan, 1.0F, 1.0e-5F);
    EXPECT_LT(params->gain, 1.0F);
    EXPECT_GT(params->gain, 0.0F);
}


// --- Physics-driven occlusion: the provider, the throttle, and what reaches the device. ---

class TestOcclusionProvider final : public IAudioOcclusionProvider {
public:
    [[nodiscard]] float QueryOcclusion(const AudioOcclusionQuery& query) const override {
        ++queryCount;
        lastQuery = query;
        return answer;
    }

    mutable int queryCount = 0;
    mutable AudioOcclusionQuery lastQuery{};
    float answer = 0.0F;
};

TEST_F(AudioSystemTest, Occlusion_BlocksGainAndMufflesTheVoice) {
    TestOcclusionProvider provider;
    provider.answer = 1.0F;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    desc.occlusionGain = 0.25F;
    desc.occlusionLowPassHz = 600.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{});
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 20.0F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F); // a zero delta snaps to the quoted target

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    // Gain 1 at 20 m of a 100 m linear ramp is (1 - 19/99); fully blocked multiplies by 0.25.
    EXPECT_NEAR(params->gain, (1.0F - 19.0F / 99.0F) * 0.25F, 1.0e-4F);
    EXPECT_NEAR(params->lowPassCutoffHz, 600.0F, 1.0e-2F);
    EXPECT_TRUE(ValidateAudioVoiceParams(*params));
}

TEST_F(AudioSystemTest, Occlusion_AClearPathSendsTheBypassSentinel) {
    TestOcclusionProvider provider;
    provider.answer = 0.0F;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 0.5F}); // inside minDistance

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);

    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->lowPassCutoffHz, 0.0F); // bit-identical device path
    EXPECT_FLOAT_EQ(params->gain, 1.0F);            // full distance attenuation and no occlusion
}

TEST_F(AudioSystemTest, Occlusion_QueriesTheProviderAtATenthOfTheFramerate) {
    TestOcclusionProvider provider;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 5.0F});

    // One second of 60 Hz frames: the 0.1 s interval allows at most eleven queries, not sixty.
    constexpr float kFrame = 1.0F / 60.0F;
    for (int frame = 0; frame < 60; ++frame) {
        audioSystem.Update(kFrame);
    }
    EXPECT_GE(provider.queryCount, 9);
    EXPECT_LE(provider.queryCount, 11);
}

TEST_F(AudioSystemTest, Occlusion_SmoothingRampsInsteadOfSwitching) {
    TestOcclusionProvider provider;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    desc.minDistance = 1.0F;
    desc.maxDistance = 100.0F;
    desc.occlusionLowPassHz = 2000.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 10.0F});

    constexpr float kFrame = 1.0F / 60.0F;
    provider.answer = 1.0F;

    device.ClearRecordedCalls();
    audioSystem.Update(kFrame);
    const AudioVoiceParams* first = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(first, nullptr);
    const float firstCutoff = first->lowPassCutoffHz;
    ASSERT_GT(firstCutoff, 0.0F);

    for (int frame = 0; frame < 40; ++frame) {
        audioSystem.Update(kFrame);
    }
    device.ClearRecordedCalls();
    audioSystem.Update(kFrame);
    const AudioVoiceParams* settled = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(settled, nullptr);
    // The filter closes over time: the first frame is nowhere near the fully blocked corner.
    EXPECT_GT(firstCutoff, settled->lowPassCutoffHz);
    EXPECT_NEAR(settled->lowPassCutoffHz, 2000.0F, 1.0F);
}

TEST_F(AudioSystemTest, Occlusion_RemovingTheProviderFadesBackToClear) {
    TestOcclusionProvider provider;
    provider.answer = 1.0F;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    desc.occlusionLowPassHz = 500.0F;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 4.0F});

    constexpr float kFrame = 1.0F / 60.0F;
    for (int frame = 0; frame < 30; ++frame) {
        audioSystem.Update(kFrame);
    }
    device.ClearRecordedCalls();
    audioSystem.Update(kFrame);
    const AudioVoiceParams* blocked = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(blocked, nullptr);
    EXPECT_NEAR(blocked->lowPassCutoffHz, 500.0F, 5.0F);

    // No provider: the source must open back up rather than freeze at its last blocked value.
    // Release is deliberately slower than attack (0.25 s), so this is three seconds of frames.
    audioSystem.SetOcclusionProvider(nullptr);
    for (int frame = 0; frame < 180; ++frame) {
        audioSystem.Update(kFrame);
    }
    device.ClearRecordedCalls();
    audioSystem.Update(kFrame);
    const AudioVoiceParams* cleared = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(cleared, nullptr);
    EXPECT_FLOAT_EQ(cleared->lowPassCutoffHz, 0.0F);
}

TEST_F(AudioSystemTest, Occlusion_NonSpatialAndOptedOutSourcesAreNeverQueried) {
    TestOcclusionProvider provider;
    provider.answer = 1.0F;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc flat;
    flat.spatial = false;
    const VoiceHandle flatSource = audioSystem.CreateSource(flat);

    AudioSourceDesc optedOut;
    optedOut.spatial = true;
    optedOut.occlusionEnabled = false;
    const VoiceHandle optedOutSource = audioSystem.CreateSource(optedOut);

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    EXPECT_EQ(provider.queryCount, 0);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const AudioVoiceParams* flatParams = FindRecordedParams(recorded, flatSource);
    const AudioVoiceParams* optedOutParams = FindRecordedParams(recorded, optedOutSource);
    ASSERT_NE(flatParams, nullptr);
    ASSERT_NE(optedOutParams, nullptr);
    EXPECT_FLOAT_EQ(flatParams->lowPassCutoffHz, 0.0F);
    EXPECT_FLOAT_EQ(optedOutParams->lowPassCutoffHz, 0.0F);
    EXPECT_FLOAT_EQ(optedOutParams->gain, flatParams->gain);
}

TEST_F(AudioSystemTest, Occlusion_TheQueryCarriesTheListenerBasisAndTheOwningEntity) {
    TestOcclusionProvider provider;
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    desc.occlusionLayerMask = 0x4U;
    desc.ownerEntity = Scene::Entity{7U, 3U};
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetListenerPosition(Math::Vector3{1.0F, 2.0F, 3.0F});
    audioSystem.SetSourcePosition(source, Math::Vector3{4.0F, 5.0F, 6.0F});

    audioSystem.Update(0.0F);
    EXPECT_EQ(provider.queryCount, 1);
    EXPECT_FLOAT_EQ(provider.lastQuery.listenerPosition.x, 1.0F);
    EXPECT_FLOAT_EQ(provider.lastQuery.sourcePosition.z, 6.0F);
    EXPECT_FLOAT_EQ(provider.lastQuery.listenerRight.x, 1.0F); // default listener faces -Z
    EXPECT_EQ(provider.lastQuery.sourceEntity, (Scene::Entity{7U, 3U}));
    EXPECT_EQ(provider.lastQuery.layerMask, 0x4U);
}

TEST_F(AudioSystemTest, Occlusion_ANonFiniteAnswerFailsOpen) {
    TestOcclusionProvider provider;
    provider.answer = std::nanf("");
    audioSystem.SetOcclusionProvider(&provider);

    AudioSourceDesc desc;
    desc.spatial = true;
    const VoiceHandle source = audioSystem.CreateSource(desc);
    audioSystem.SetSourcePosition(source, Math::Vector3{0.0F, 0.0F, 0.5F});

    device.ClearRecordedCalls();
    audioSystem.Update(0.0F);
    const AudioVoiceParams* params = FindRecordedParams(device.GetRecordedCalls(), source);
    ASSERT_NE(params, nullptr);
    EXPECT_FLOAT_EQ(params->lowPassCutoffHz, 0.0F);
    EXPECT_FLOAT_EQ(params->gain, 1.0F);
}

TEST_F(AudioSystemTest, Occlusion_DescriptorRejectsAnUnusableFloor) {
    AudioSourceDesc badGain;
    badGain.occlusionGain = 1.5F;
    EXPECT_EQ(audioSystem.CreateSource(badGain), kInvalidVoiceHandle);

    AudioSourceDesc badCutoff;
    badCutoff.occlusionLowPassHz = 5.0F;
    EXPECT_EQ(audioSystem.CreateSource(badCutoff), kInvalidVoiceHandle);

    // ...but a 2D source ignores them, exactly like the cone and Doppler fields.
    AudioSourceDesc flat;
    flat.spatial = false;
    flat.occlusionGain = 1.5F;
    flat.occlusionLowPassHz = 5.0F;
    EXPECT_NE(audioSystem.CreateSource(flat), kInvalidVoiceHandle);
}


// --- Metering: what a mixer panel reads, assembled from what the device measured. ---

TEST_F(AudioSystemTest, Meter_ASourceReportsWhatTheDeviceMeasured) {
    const VoiceHandle source = audioSystem.CreateSource(AudioSourceDesc{});
    ASSERT_NE(source, kInvalidVoiceHandle);
    device.SetVoiceMeter(source, AudioMeterReading{0.5F, 0.25F});

    const AudioMeterReading meter = audioSystem.GetSourceMeter(source);
    EXPECT_FLOAT_EQ(meter.peak, 0.5F);
    EXPECT_FLOAT_EQ(meter.rms, 0.25F);

    // Idempotent: reading again shows the same measurement, so one frame can draw a source row and
    // the bus above it without the second read seeing zero.
    EXPECT_EQ(audioSystem.GetSourceMeter(source), meter);
}

TEST_F(AudioSystemTest, Meter_UnknownSourcesAndBusesReadSilence) {
    EXPECT_EQ(audioSystem.GetSourceMeter(VoiceHandle{999U}), kSilentAudioMeter);
    EXPECT_EQ(audioSystem.GetMixerGroupMeter("NoSuchBus"), kSilentAudioMeter);
    EXPECT_EQ(audioSystem.GetMixerGroupMeter("Music"), kSilentAudioMeter); // exists, carries nothing
}

TEST_F(AudioSystemTest, Meter_ABusSumsTheVoicesRoutedToIt) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("SFX", 0.5F));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Footsteps", "SFX", 1.0F));

    const VoiceHandle inSfx = audioSystem.CreateSource(AudioSourceDesc{});
    const VoiceHandle inFootsteps = audioSystem.CreateSource(AudioSourceDesc{});
    const VoiceHandle onMaster = audioSystem.CreateSource(AudioSourceDesc{});
    ASSERT_NE(inSfx, kInvalidVoiceHandle);
    ASSERT_TRUE(audioSystem.SetSourceMixerGroup(inSfx, "SFX"));
    ASSERT_TRUE(audioSystem.SetSourceMixerGroup(inFootsteps, "Footsteps"));

    device.SetVoiceMeter(inSfx, AudioMeterReading{0.3F, 0.3F});
    device.SetVoiceMeter(inFootsteps, AudioMeterReading{0.4F, 0.4F});
    device.SetVoiceMeter(onMaster, AudioMeterReading{0.5F, 0.5F});

    // A bus includes everything under it: Footsteps' voice flows into SFX.
    const AudioMeterReading sfx = audioSystem.GetMixerGroupMeter("SFX");
    EXPECT_NEAR(sfx.peak, std::sqrt(0.3F * 0.3F + 0.4F * 0.4F), 1.0e-5F);
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Footsteps").peak, 0.4F, 1.0e-6F);

    // Master carries every voice, including the sub-buses.
    const float expectedMaster = std::sqrt(0.3F * 0.3F + 0.4F * 0.4F + 0.5F * 0.5F);
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Master").peak, expectedMaster, 1.0e-5F);
}

TEST_F(AudioSystemTest, Meter_ABusMeterDoesNotDependOnReadOrder) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music", 1.0F));
    const VoiceHandle music = audioSystem.CreateSource(AudioSourceDesc{});
    ASSERT_TRUE(audioSystem.SetSourceMixerGroup(music, "Music"));
    device.SetVoiceMeter(music, AudioMeterReading{0.8F, 0.8F});

    // Reading the bus first, then Master, still reports both: a meter read consumes nothing.
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Music").peak, 0.8F, 1.0e-6F);
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Master").peak, 0.8F, 1.0e-6F);
}

TEST_F(AudioSystemTest, Meter_AFullyMutedBusStillReportsTheLevelItWouldHave) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Voice", 1.0F));
    const VoiceHandle voice = audioSystem.CreateSource(AudioSourceDesc{});
    ASSERT_TRUE(audioSystem.SetSourceMixerGroup(voice, "Voice"));
    device.SetVoiceMeter(voice, AudioMeterReading{0.6F, 0.6F});
    ASSERT_TRUE(audioSystem.SetMixerGroupMuted("Voice", true));

    // The device would have mixed nothing, so a real backend reports silence - but the meter path
    // must not double-apply the mute either. What the device says is what is shown.
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Voice").peak, 0.6F, 1.0e-6F);
}

TEST_F(AudioSystemTest, Meter_ASourceShellWithNoRoutingStillBelongsToMaster) {
    const VoiceHandle source = audioSystem.CreateSource(AudioSourceDesc{});
    device.SetVoiceMeter(source, AudioMeterReading{0.2F, 0.1F});
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Master").peak, 0.2F, 1.0e-6F);
    EXPECT_NEAR(audioSystem.GetMixerGroupMeter("Master").rms, 0.1F, 1.0e-6F);
}


} // namespace
} // namespace UVE::Audio::Tests
