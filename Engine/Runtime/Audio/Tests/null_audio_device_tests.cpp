// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_audio_device.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"

namespace UVE::Audio::Tests {
namespace {

[[nodiscard]] bool LoggedAnError(const Debug::MemorySink& sink) {
    const std::vector<Debug::LogMessage> messages = sink.GetMessages();
    return std::any_of(messages.begin(), messages.end(),
                        [](const Debug::LogMessage& message) { return message.level == Debug::LogLevel::Error; });
}

TEST(NullAudioDeviceTest, CreateVoice_ReturnsUniqueHandles) {
    NullAudioDevice device;
    const VoiceHandle first = device.CreateVoice(AudioVoiceDesc{});
    const VoiceHandle second = device.CreateVoice(AudioVoiceDesc{});

    EXPECT_NE(first, kInvalidVoiceHandle);
    EXPECT_NE(second, kInvalidVoiceHandle);
    EXPECT_NE(first, second);
}

TEST(NullAudioDeviceTest, DestroyVoice_UnknownHandle_LogsErrorSafely) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDevice device;
    device.DestroyVoice(VoiceHandle{999});

    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceTest, Play_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDevice device;
    EXPECT_FALSE(device.Play(VoiceHandle{999}));

    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceTest, Stop_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDevice device;
    EXPECT_FALSE(device.Stop(VoiceHandle{999}));

    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceTest, SetVoiceParams_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDevice device;
    EXPECT_FALSE(device.SetVoiceParams(VoiceHandle{999}, AudioVoiceParams{}));

    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceTest, SetVoiceParams_RejectsInvalidParametersWithoutRecording) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateVoice(AudioVoiceDesc{});
    const AudioVoiceParams original{Math::Vector3{1.0F, 2.0F, 3.0F}, 0.5F, 1.0F};
    EXPECT_TRUE(device.SetVoiceParams(voice, original));
    ASSERT_EQ(device.GetRecordedCalls().size(), 1U);
    const AudioVoiceParams invalidPosition{
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 0.5F, 1.0F};
    EXPECT_FALSE(device.SetVoiceParams(voice, invalidPosition));
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 1.1F, 1.0F}));
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, -1.0F}));
    // Pan is a finished multiplier in [-1, 1]; anything outside that would drive one channel louder
    // than the mix ever asked for, so the device refuses it like any other bad parameter.
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, 1.5F}));
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, -2.0F}));
    EXPECT_TRUE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, -1.0F}));
    // The low-pass corner is either the 0 bypass sentinel or a real, audible corner.
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, 0.0F, -5.0F}));
    EXPECT_FALSE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, 0.0F, 30000.0F}));
    EXPECT_TRUE(device.SetVoiceParams(voice, AudioVoiceParams{Math::Vector3{}, 0.5F, 1.0F, 0.0F, 700.0F}));
    EXPECT_EQ(device.GetRecordedCalls().size(), 3U);
}

TEST(NullAudioDeviceTest, GetVoiceState_UnknownHandle_ReturnsStoppedAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDevice device;
    EXPECT_EQ(device.GetVoiceState(VoiceHandle{999}), VoicePlaybackState::Stopped);

    EXPECT_TRUE(LoggedAnError(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceTest, GetVoiceState_TracksPlayAndStop) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateVoice(AudioVoiceDesc{});

    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Stopped);
    EXPECT_TRUE(device.Play(voice));
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Playing);
    EXPECT_TRUE(device.Stop(voice));
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Stopped);
}

TEST(NullAudioDeviceTest, GetLiveVoiceCount_TracksCreateAndDestroy) {
    NullAudioDevice device;
    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);

    const VoiceHandle voice = device.CreateVoice(AudioVoiceDesc{});
    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);

    device.DestroyVoice(voice);
    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
}

TEST(NullAudioDeviceTest, GetBackendName_ReturnsNull) {
    NullAudioDevice device;
    EXPECT_EQ(device.GetBackendName(), "Null");
}

TEST(NullAudioDeviceTest, GetRecordedCalls_RecordsPlaySetParamsStopInOrder) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateVoice(AudioVoiceDesc{});

    EXPECT_TRUE(device.Play(voice));
    const AudioVoiceParams params{Math::Vector3{1.0F, 2.0F, 3.0F}, 0.5F, 1.0F};
    EXPECT_TRUE(device.SetVoiceParams(voice, params));
    EXPECT_TRUE(device.Stop(voice));

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<PlayVoiceCall>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<SetVoiceParamsCall>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<StopVoiceCall>(recorded[2]));

    EXPECT_EQ(std::get<PlayVoiceCall>(recorded[0]).voice, voice);
    EXPECT_EQ(std::get<SetVoiceParamsCall>(recorded[1]).voice, voice);
    EXPECT_EQ(std::get<SetVoiceParamsCall>(recorded[1]).params.gain, 0.5F);
    EXPECT_EQ(std::get<StopVoiceCall>(recorded[2]).voice, voice);
}

TEST(NullAudioDeviceTest, ClearRecordedCalls_EmptiesLogWithoutAffectingLiveVoiceCount) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateVoice(AudioVoiceDesc{});
    EXPECT_TRUE(device.Play(voice));
    ASSERT_FALSE(device.GetRecordedCalls().empty());

    device.ClearRecordedCalls();

    EXPECT_TRUE(device.GetRecordedCalls().empty());
    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
}

// --- Streaming voices: a bounded ring instead of a fully decoded clip. ---

TEST(NullAudioDeviceStreamTest, CreateStreamVoice_RejectsAnInvalidFormat) {
    NullAudioDevice device;
    EXPECT_EQ(device.CreateStreamVoice(AudioStreamFormat{0U, 1U}), kInvalidVoiceHandle);
    EXPECT_EQ(device.CreateStreamVoice(AudioStreamFormat{48000U, 0U}), kInvalidVoiceHandle);
    EXPECT_EQ(device.CreateStreamVoice(AudioStreamFormat{48000U, 3U}), kInvalidVoiceHandle);
    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
}

TEST(NullAudioDeviceStreamTest, PushFillsTheRingAndReportsExactFreeSpace) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateStreamVoice(AudioStreamFormat{48000U, 2U});
    ASSERT_NE(voice, kInvalidVoiceHandle);
    EXPECT_EQ(device.GetStreamBufferedFrames(voice), 0U);
    EXPECT_EQ(device.GetStreamFreeFrames(voice), kAudioStreamRingFrames);

    const std::vector<float> frames(6U, 0.25F); // three stereo frames
    EXPECT_EQ(device.PushStreamFrames(voice, frames), 3U);
    EXPECT_EQ(device.GetStreamBufferedFrames(voice), 3U);
    EXPECT_EQ(device.GetStreamFreeFrames(voice), kAudioStreamRingFrames - 3U);
}

TEST(NullAudioDeviceStreamTest, PushRefusesAPartialFrameAndAClipVoice) {
    NullAudioDevice device;
    const VoiceHandle stream = device.CreateStreamVoice(AudioStreamFormat{48000U, 2U});
    ASSERT_NE(stream, kInvalidVoiceHandle);
    const std::vector<float> odd(3U, 0.1F); // one and a half stereo frames
    EXPECT_EQ(device.PushStreamFrames(stream, odd), 0U);
    EXPECT_EQ(device.GetStreamBufferedFrames(stream), 0U);

    const VoiceHandle clip = device.CreateVoice(AudioVoiceDesc{});
    const std::vector<float> frames(4U, 0.0F);
    EXPECT_EQ(device.PushStreamFrames(clip, frames), 0U);
    EXPECT_EQ(device.GetStreamFreeFrames(clip), 0U);
    EXPECT_FALSE(device.EndStream(clip));
}

TEST(NullAudioDeviceStreamTest, EndStreamStopsOnlyAfterTheRingDrains) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateStreamVoice(AudioStreamFormat{48000U, 1U});
    ASSERT_NE(voice, kInvalidVoiceHandle);
    const std::vector<float> frames(4U, 0.5F);
    ASSERT_EQ(device.PushStreamFrames(voice, frames), 4U);
    ASSERT_TRUE(device.Play(voice));
    ASSERT_TRUE(device.EndStream(voice));

    // Still playing: the ring still holds audio the caller asked us to finish.
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Playing);
    device.ConsumeStreamFrames(voice, 2U);
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Playing);
    device.ConsumeStreamFrames(voice, 2U);
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Stopped);
    // Nothing may be pushed after the end was declared.
    EXPECT_EQ(device.PushStreamFrames(voice, frames), 0U);
}

TEST(NullAudioDeviceStreamTest, EndStreamOnAnEmptyRingStopsImmediately) {
    NullAudioDevice device;
    const VoiceHandle voice = device.CreateStreamVoice(AudioStreamFormat{48000U, 2U});
    ASSERT_NE(voice, kInvalidVoiceHandle);
    ASSERT_TRUE(device.Play(voice));
    ASSERT_TRUE(device.EndStream(voice));
    EXPECT_EQ(device.GetVoiceState(voice), VoicePlaybackState::Stopped);
}

TEST(NullAudioDeviceStreamTest, UnknownHandlesAreRefusedWithoutTouchingBookkeeping) {
    NullAudioDevice device;
    const VoiceHandle unknown{1234U};
    const std::vector<float> frames(2U, 0.0F);
    EXPECT_EQ(device.PushStreamFrames(unknown, frames), 0U);
    EXPECT_EQ(device.GetStreamBufferedFrames(unknown), 0U);
    EXPECT_EQ(device.GetStreamFreeFrames(unknown), 0U);
    EXPECT_FALSE(device.EndStream(unknown));
    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
}


} // namespace
} // namespace UVE::Audio::Tests
