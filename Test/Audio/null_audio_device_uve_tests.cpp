// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/audio/null_audio_device_uve.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"

namespace UVE::Audio::Tests {
namespace {

[[nodiscard]] bool LoggedAnErrorUVE(const Debug::MemorySinkUVE& sink) {
    const std::vector<Debug::LogMessageUVE> messages = sink.GetMessagesUVE();
    return std::any_of(messages.begin(), messages.end(),
                        [](const Debug::LogMessageUVE& message) { return message.level == Debug::LogLevelUVE::Error; });
}

TEST(NullAudioDeviceUVETest, CreateVoiceUVE_ReturnsUniqueHandles) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE first = device.CreateVoiceUVE(AudioVoiceDescUVE{});
    const VoiceHandleUVE second = device.CreateVoiceUVE(AudioVoiceDescUVE{});

    EXPECT_NE(first, kInvalidVoiceHandleUVE);
    EXPECT_NE(second, kInvalidVoiceHandleUVE);
    EXPECT_NE(first, second);
}

TEST(NullAudioDeviceUVETest, DestroyVoiceUVE_UnknownHandle_LogsErrorSafely) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDeviceUVE device;
    device.DestroyVoiceUVE(VoiceHandleUVE{999});

    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceUVETest, PlayUVE_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDeviceUVE device;
    EXPECT_FALSE(device.PlayUVE(VoiceHandleUVE{999}));

    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceUVETest, StopUVE_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDeviceUVE device;
    EXPECT_FALSE(device.StopUVE(VoiceHandleUVE{999}));

    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceUVETest, SetVoiceParamsUVE_UnknownHandle_ReturnsFalseAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDeviceUVE device;
    EXPECT_FALSE(device.SetVoiceParamsUVE(VoiceHandleUVE{999}, AudioVoiceParamsUVE{}));

    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceUVETest, SetVoiceParamsUVE_RejectsInvalidParametersWithoutRecording) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateVoiceUVE(AudioVoiceDescUVE{});
    const AudioVoiceParamsUVE original{Math::Vector3UVE{1.0F, 2.0F, 3.0F}, 0.5F, 1.0F};
    EXPECT_TRUE(device.SetVoiceParamsUVE(voice, original));
    ASSERT_EQ(device.GetRecordedCallsUVE().size(), 1U);
    const AudioVoiceParamsUVE invalidPosition{
        Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 0.5F, 1.0F};
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, invalidPosition));
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 1.1F, 1.0F}));
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, -1.0F}));
    // Pan is a finished multiplier in [-1, 1]; anything outside that would drive one channel louder
    // than the mix ever asked for, so the device refuses it like any other bad parameter.
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, 1.5F}));
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, -2.0F}));
    EXPECT_TRUE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, -1.0F}));
    // The low-pass corner is either the 0 bypass sentinel or a real, audible corner.
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, 0.0F, -5.0F}));
    EXPECT_FALSE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, 0.0F, 30000.0F}));
    EXPECT_TRUE(device.SetVoiceParamsUVE(voice, AudioVoiceParamsUVE{Math::Vector3UVE{}, 0.5F, 1.0F, 0.0F, 700.0F}));
    EXPECT_EQ(device.GetRecordedCallsUVE().size(), 3U);
}

TEST(NullAudioDeviceUVETest, GetVoiceStateUVE_UnknownHandle_ReturnsStoppedAndLogsError) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    NullAudioDeviceUVE device;
    EXPECT_EQ(device.GetVoiceStateUVE(VoiceHandleUVE{999}), VoicePlaybackStateUVE::Stopped);

    EXPECT_TRUE(LoggedAnErrorUVE(*memorySinkPtr));
    logger.Shutdown();
}

TEST(NullAudioDeviceUVETest, GetVoiceStateUVE_TracksPlayAndStop) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateVoiceUVE(AudioVoiceDescUVE{});

    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
    EXPECT_TRUE(device.PlayUVE(voice));
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    EXPECT_TRUE(device.StopUVE(voice));
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
}

TEST(NullAudioDeviceUVETest, GetLiveVoiceCountUVE_TracksCreateAndDestroy) {
    NullAudioDeviceUVE device;
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 0U);

    const VoiceHandleUVE voice = device.CreateVoiceUVE(AudioVoiceDescUVE{});
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 1U);

    device.DestroyVoiceUVE(voice);
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 0U);
}

TEST(NullAudioDeviceUVETest, GetBackendNameUVE_ReturnsNull) {
    NullAudioDeviceUVE device;
    EXPECT_EQ(device.GetBackendNameUVE(), "Null");
}

TEST(NullAudioDeviceUVETest, GetRecordedCallsUVE_RecordsPlaySetParamsStopInOrder) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateVoiceUVE(AudioVoiceDescUVE{});

    EXPECT_TRUE(device.PlayUVE(voice));
    const AudioVoiceParamsUVE params{Math::Vector3UVE{1.0F, 2.0F, 3.0F}, 0.5F, 1.0F};
    EXPECT_TRUE(device.SetVoiceParamsUVE(voice, params));
    EXPECT_TRUE(device.StopUVE(voice));

    const std::vector<RecordedAudioCallUVE>& recorded = device.GetRecordedCallsUVE();
    ASSERT_EQ(recorded.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<PlayVoiceCallUVE>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<SetVoiceParamsCallUVE>(recorded[1]));
    EXPECT_TRUE(std::holds_alternative<StopVoiceCallUVE>(recorded[2]));

    EXPECT_EQ(std::get<PlayVoiceCallUVE>(recorded[0]).voice, voice);
    EXPECT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[1]).voice, voice);
    EXPECT_EQ(std::get<SetVoiceParamsCallUVE>(recorded[1]).params.gain, 0.5F);
    EXPECT_EQ(std::get<StopVoiceCallUVE>(recorded[2]).voice, voice);
}

TEST(NullAudioDeviceUVETest, ClearRecordedCallsUVE_EmptiesLogWithoutAffectingLiveVoiceCount) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateVoiceUVE(AudioVoiceDescUVE{});
    EXPECT_TRUE(device.PlayUVE(voice));
    ASSERT_FALSE(device.GetRecordedCallsUVE().empty());

    device.ClearRecordedCallsUVE();

    EXPECT_TRUE(device.GetRecordedCallsUVE().empty());
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 1U);
}

// --- Streaming voices: a bounded ring instead of a fully decoded clip. ---

TEST(NullAudioDeviceStreamUVETest, CreateStreamVoiceUVE_RejectsAnInvalidFormat) {
    NullAudioDeviceUVE device;
    EXPECT_EQ(device.CreateStreamVoiceUVE(AudioStreamFormatUVE{0U, 1U}), kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 0U}), kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 3U}), kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 0U);
}

TEST(NullAudioDeviceStreamUVETest, PushFillsTheRingAndReportsExactFreeSpace) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 2U});
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    EXPECT_EQ(device.GetStreamBufferedFramesUVE(voice), 0U);
    EXPECT_EQ(device.GetStreamFreeFramesUVE(voice), kAudioStreamRingFramesUVE);

    const std::vector<float> frames(6U, 0.25F); // three stereo frames
    EXPECT_EQ(device.PushStreamFramesUVE(voice, frames), 3U);
    EXPECT_EQ(device.GetStreamBufferedFramesUVE(voice), 3U);
    EXPECT_EQ(device.GetStreamFreeFramesUVE(voice), kAudioStreamRingFramesUVE - 3U);
}

TEST(NullAudioDeviceStreamUVETest, PushRefusesAPartialFrameAndAClipVoice) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE stream = device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 2U});
    ASSERT_NE(stream, kInvalidVoiceHandleUVE);
    const std::vector<float> odd(3U, 0.1F); // one and a half stereo frames
    EXPECT_EQ(device.PushStreamFramesUVE(stream, odd), 0U);
    EXPECT_EQ(device.GetStreamBufferedFramesUVE(stream), 0U);

    const VoiceHandleUVE clip = device.CreateVoiceUVE(AudioVoiceDescUVE{});
    const std::vector<float> frames(4U, 0.0F);
    EXPECT_EQ(device.PushStreamFramesUVE(clip, frames), 0U);
    EXPECT_EQ(device.GetStreamFreeFramesUVE(clip), 0U);
    EXPECT_FALSE(device.EndStreamUVE(clip));
}

TEST(NullAudioDeviceStreamUVETest, EndStreamStopsOnlyAfterTheRingDrains) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 1U});
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    const std::vector<float> frames(4U, 0.5F);
    ASSERT_EQ(device.PushStreamFramesUVE(voice, frames), 4U);
    ASSERT_TRUE(device.PlayUVE(voice));
    ASSERT_TRUE(device.EndStreamUVE(voice));

    // Still playing: the ring still holds audio the caller asked us to finish.
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    device.ConsumeStreamFramesUVE(voice, 2U);
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Playing);
    device.ConsumeStreamFramesUVE(voice, 2U);
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
    // Nothing may be pushed after the end was declared.
    EXPECT_EQ(device.PushStreamFramesUVE(voice, frames), 0U);
}

TEST(NullAudioDeviceStreamUVETest, EndStreamOnAnEmptyRingStopsImmediately) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE voice = device.CreateStreamVoiceUVE(AudioStreamFormatUVE{48000U, 2U});
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);
    ASSERT_TRUE(device.PlayUVE(voice));
    ASSERT_TRUE(device.EndStreamUVE(voice));
    EXPECT_EQ(device.GetVoiceStateUVE(voice), VoicePlaybackStateUVE::Stopped);
}

TEST(NullAudioDeviceStreamUVETest, UnknownHandlesAreRefusedWithoutTouchingBookkeeping) {
    NullAudioDeviceUVE device;
    const VoiceHandleUVE unknown{1234U};
    const std::vector<float> frames(2U, 0.0F);
    EXPECT_EQ(device.PushStreamFramesUVE(unknown, frames), 0U);
    EXPECT_EQ(device.GetStreamBufferedFramesUVE(unknown), 0U);
    EXPECT_EQ(device.GetStreamFreeFramesUVE(unknown), 0U);
    EXPECT_FALSE(device.EndStreamUVE(unknown));
    EXPECT_EQ(device.GetLiveVoiceCountUVE(), 0U);
}


} // namespace
} // namespace UVE::Audio::Tests
