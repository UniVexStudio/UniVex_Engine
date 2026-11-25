// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ambisonic_stream_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include "audio_system.h"
#include "null_audio_device.h"
#include "audio_asset.h"

namespace UVE::Audio::Tests {
namespace {

constexpr float kTolerance = 1.0e-6F;

/// An in-memory source that behaves like a decoded clip: fixed format, a cursor, and a seek.
class FixedStreamSource final : public IAudioStreamSource {
public:
    FixedStreamSource(const std::uint32_t channels, const std::uint32_t sampleRate,
                         std::vector<float> interleavedSamples)
        : m_format{sampleRate, channels}, m_samples(std::move(interleavedSamples)) {}

    [[nodiscard]] AudioStreamFormat GetFormat() const noexcept override { return m_format; }
    [[nodiscard]] std::size_t GetFrameCount() const noexcept override {
        return m_format.channels == 0U ? 0U : m_samples.size() / m_format.channels;
    }

    [[nodiscard]] std::size_t ReadFrames(const std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override {
        outFrames.clear();
        const std::size_t channels = m_format.channels;
        const std::size_t frameCount = GetFrameCount();
        if (channels == 0U || requestedFrames == 0U || m_cursor >= frameCount) return 0U;
        const std::size_t frames = std::min(requestedFrames, frameCount - m_cursor);
        outFrames.assign(m_samples.begin() + static_cast<std::ptrdiff_t>(m_cursor * channels),
                         m_samples.begin() + static_cast<std::ptrdiff_t>((m_cursor + frames) * channels));
        m_cursor += frames;
        return frames;
    }

    [[nodiscard]] bool SeekToFrame(const std::size_t frame) override {
        if (frame > GetFrameCount()) return false;
        m_cursor = frame;
        return true;
    }

private:
    AudioStreamFormat m_format;
    std::vector<float> m_samples;
    std::size_t m_cursor = 0U;
};

/// A source that overruns its own contract by one frame, to prove the adapter does not read past
/// the window it was handed.
class OverrunningStreamSource final : public IAudioStreamSource {
public:
    [[nodiscard]] AudioStreamFormat GetFormat() const noexcept override { return AudioStreamFormat{48000U, 4U}; }
    [[nodiscard]] std::size_t GetFrameCount() const noexcept override { return 16U; }
    [[nodiscard]] std::size_t ReadFrames(const std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override {
        outFrames.assign((requestedFrames + 1U) * 4U, 0.5F);
        return requestedFrames + 1U;
    }
    [[nodiscard]] bool SeekToFrame(std::size_t) override { return true; }
};

/// One AmbiX frame repeated `frameCount` times.
[[nodiscard]] std::vector<float> ConstantFrames(const float w, const float y, const float z, const float x,
                                                   const std::size_t frameCount) {
    std::vector<float> samples;
    samples.reserve(frameCount * 4U);
    for (std::size_t frame = 0U; frame < frameCount; ++frame) {
        samples.push_back(w);
        samples.push_back(y);
        samples.push_back(z);
        samples.push_back(x);
    }
    return samples;
}

TEST(AmbisonicStreamSourceTest, TheSourceAndDeviceChannelCapsAreDifferentOnPurpose) {
    // Four channels are legal for a source and illegal for a device voice: the decoder in between
    // is the only thing allowed to cross that line, so a four-channel file can never be played as
    // if it were stereo.
    static_assert(IsAudioStreamSourceFormatValid(AudioStreamFormat{48000U, 4U}));
    static_assert(!IsAudioStreamFormatValid(AudioStreamFormat{48000U, 4U}));
    static_assert(!IsAudioStreamSourceFormatValid(AudioStreamFormat{48000U, 5U}));
    static_assert(IsAudioStreamFormatValid(AudioStreamFormat{48000U, 2U}));
    static_assert(IsAudioStreamSourceFormatValid(AudioStreamFormat{48000U, 1U}));
}

TEST(AmbisonicStreamSourceTest, ASourceThatIsNotFirstOrderIsRefused) {
    // Stereo is what the device takes, not what a bed is: a two-channel "ambisonic" source is an
    // authoring mistake, and decoding it as W/Y would silently discard half the field.
    FixedStreamSource stereo{2U, 48000U, {1.0F, 0.0F, 0.5F, 0.0F}};
    AmbisonicStreamSource bed{stereo};
    EXPECT_FALSE(bed.IsReady());
    EXPECT_EQ(bed.GetFormat(), (AudioStreamFormat{0U, 0U}));
    EXPECT_EQ(bed.GetFrameCount(), 0U);

    std::vector<float> out{1.0F};
    EXPECT_EQ(bed.ReadFrames(4U, out), 0U);
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(bed.SeekToFrame(0U));

    FixedStreamSource fiveChannels{5U, 48000U, std::vector<float>(20U, 0.25F)};
    const AmbisonicStreamSource outOfRange{fiveChannels};
    EXPECT_FALSE(outOfRange.IsReady());
}

TEST(AmbisonicStreamSourceTest, ReportsStereoAtTheSourcesRateAndForwardsItsLength) {
    FixedStreamSource source{4U, 44100U, ConstantFrames(1.0F, 0.0F, 0.0F, 0.0F, 12U)};
    const AmbisonicStreamSource bed{source};
    EXPECT_TRUE(bed.IsReady());
    EXPECT_EQ(bed.GetFormat(), (AudioStreamFormat{44100U, 2U}));
    EXPECT_EQ(bed.GetFrameCount(), 12U);
}

TEST(AmbisonicStreamSourceTest, AnOmnidirectionalBedIsUnityOnBothChannels) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 0.0F, 0.0F, 0.0F, 3U)};
    AmbisonicStreamSource bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(3U, out), 3U);
    ASSERT_EQ(out.size(), 6U);
    for (std::size_t index = 0U; index < out.size(); ++index) {
        EXPECT_FLOAT_EQ(out[index], 1.0F) << "sample " << index;
    }
}

TEST(AmbisonicStreamSourceTest, AWaveFromTheLeftDecodesToTheLeftChannel) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 1.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSource bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(2U, out), 2U);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_FLOAT_EQ(out[0], 2.0F);
    EXPECT_FLOAT_EQ(out[1], 0.0F);
    EXPECT_FLOAT_EQ(out[2], 2.0F);
    EXPECT_FLOAT_EQ(out[3], 0.0F);
}

TEST(AmbisonicStreamSourceTest, TurningTheHeadSteersTheField) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 0.0F, 0.0F, 1.0F, 1U)};
    AmbisonicStreamSource bed{source};

    // Facing the world's front, the wave is centred.
    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(1U, out), 1U);
    EXPECT_NEAR(out[0], 1.0F, kTolerance);
    EXPECT_NEAR(out[1], 1.0F, kTolerance);

    // Turning to face -X puts the same world-locked wave on the listener's right. The frame is
    // re-read from the start, because the point is the orientation, not the cursor.
    ASSERT_TRUE(bed.SetListenerOrientation(Math::Vector3{-1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}));
    ASSERT_TRUE(bed.SeekToFrame(0U));
    ASSERT_EQ(bed.ReadFrames(1U, out), 1U);
    EXPECT_NEAR(out[0], 0.0F, kTolerance);
    EXPECT_NEAR(out[1], 2.0F, kTolerance);
}

TEST(AmbisonicStreamSourceTest, GainScalesTheWholeWindowAndIsValidated) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 0.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSource bed{source};

    EXPECT_FLOAT_EQ(bed.GetGain(), 1.0F);
    ASSERT_TRUE(bed.SetGain(0.25F));
    EXPECT_FLOAT_EQ(bed.GetGain(), 0.25F);

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(2U, out), 2U);
    for (const float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.25F);
    }

    EXPECT_FALSE(bed.SetGain(-0.1F));
    EXPECT_FALSE(bed.SetGain(1.5F));
    EXPECT_FALSE(bed.SetGain(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FLOAT_EQ(bed.GetGain(), 0.25F); // a refused write leaves the last good value
}

TEST(AmbisonicStreamSourceTest, AnInvalidOrientationIsRefusedAndThePreviousOneSurvives) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 0.0F, 0.0F, 1.0F, 1U)};
    AmbisonicStreamSource bed{source};

    ASSERT_TRUE(bed.SetListenerOrientation(Math::Vector3{-1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}));
    const AmbisonicRotation previous = bed.GetRotation();

    EXPECT_FALSE(bed.SetListenerOrientation(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(bed.SetListenerOrientation(Math::Vector3{0.0F, 0.0F, -1.0F}, Math::Vector3{0.0F, 0.0F, 3.0F}));
    EXPECT_FALSE(bed.SetListenerOrientation(
        Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, -1.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}));
    EXPECT_EQ(bed.GetRotation(), previous);

    // The refusal is observable in the output too: the wave is still on the right.
    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(1U, out), 1U);
    EXPECT_NEAR(out[0], 0.0F, kTolerance);
    EXPECT_NEAR(out[1], 2.0F, kTolerance);
}

TEST(AmbisonicStreamSourceTest, WindowsAreBoundedByTheRingAndZeroFramesReadNothing) {
    FixedStreamSource source{4U, 48000U,
                                ConstantFrames(1.0F, 0.0F, 0.0F, 0.0F, kMaximumAudioStreamRingFrames)};
    AmbisonicStreamSource bed{source};

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFrames(kMaximumAudioStreamRingFrames + 1U, out), 0U);
    EXPECT_TRUE(out.empty());

    ASSERT_EQ(bed.ReadFrames(kMaximumAudioStreamRingFrames, out),
              kMaximumAudioStreamRingFrames); // the ring's own depth is still legal
    EXPECT_EQ(out.size(), static_cast<std::size_t>(kMaximumAudioStreamRingFrames) * 2U);
    EXPECT_EQ(bed.ReadFrames(0U, out), 0U);
    EXPECT_TRUE(out.empty());
}

TEST(AmbisonicStreamSourceTest, EndOfSourceIsReportedAndSeekingRestartsIt) {
    FixedStreamSource source{4U, 48000U, ConstantFrames(1.0F, 0.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSource bed{source};

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFrames(8U, out), 2U); // a short source returns what it has, not a padded window
    EXPECT_EQ(bed.ReadFrames(8U, out), 0U);
    EXPECT_TRUE(out.empty());

    ASSERT_TRUE(bed.SeekToFrame(0U));
    EXPECT_EQ(bed.ReadFrames(8U, out), 2U);
    // Seeking forward into the middle leaves exactly the tail.
    ASSERT_TRUE(bed.SeekToFrame(1U));
    EXPECT_EQ(bed.ReadFrames(8U, out), 1U);
    EXPECT_FALSE(bed.SeekToFrame(3U));
}

TEST(AmbisonicStreamSourceTest, NonFiniteSamplesBecomeSilence) {
    FixedStreamSource source{4U, 48000U,
                                ConstantFrames(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F, 1U)};
    AmbisonicStreamSource bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFrames(1U, out), 1U);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_FLOAT_EQ(out[0], 0.0F);
    EXPECT_FLOAT_EQ(out[1], 0.0F);
}

TEST(AmbisonicStreamSourceTest, ASourceThatOverrunsItsWindowIsDroppedRatherThanRead) {
    OverrunningStreamSource source;
    AmbisonicStreamSource bed{source};
    ASSERT_TRUE(bed.IsReady());

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFrames(4U, out), 0U);
    EXPECT_TRUE(out.empty());
}

TEST(AmbisonicStreamSourceTest, FourChannelFilesAreReadableAsStreams) {
    const std::filesystem::path path = UVE::Tests::MakeTestCaseDirectory("four-channel") / "bed.uvaudio";
    Asset::AudioAsset asset;
    asset.channels = 4U;
    asset.sampleRate = 48000U;
    asset.samples = {0.5F, 0.25F, 0.0F, -0.25F, 0.1F, 0.2F, 0.3F, 0.4F};
    ASSERT_TRUE(Asset::SaveAudioAsset(asset, path));

    AudioFileStreamSource fileSource{path};
    ASSERT_TRUE(fileSource.Open());
    EXPECT_EQ(fileSource.GetFormat(), (AudioStreamFormat{48000U, 4U}));
    EXPECT_EQ(fileSource.GetFrameCount(), 2U);

    std::vector<float> window;
    ASSERT_EQ(fileSource.ReadFrames(2U, window), 2U);
    EXPECT_EQ(window, asset.samples);
}

TEST(AmbisonicStreamSourceTest, AFileBackedBedPlaysThroughTheAudioSystemAndLoops) {
    const std::filesystem::path path = UVE::Tests::MakeTestCaseDirectory("bed") / "loop.uvaudio";
    Asset::AudioAsset asset;
    asset.channels = 4U;
    asset.sampleRate = 48000U;
    asset.samples = {1.0F, 1.0F, 0.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F}; // hard left, then hard right
    ASSERT_TRUE(Asset::SaveAudioAsset(asset, path));

    AudioFileStreamSource fileSource{path};
    ASSERT_TRUE(fileSource.Open());
    AmbisonicStreamSource bed{fileSource};
    ASSERT_TRUE(bed.IsReady());

    NullAudioDevice device;
    AudioSystem audioSystem{device};

    AudioSourceDesc desc;
    desc.spatial = false; // a bed is a field, not a point: distance must not attenuate it
    desc.looping = true;
    const VoiceHandle voice = audioSystem.CreateStreamingSource(desc, bed);
    ASSERT_NE(voice, kInvalidVoiceHandle);

    // The pump is the whole integration: no device changes, no source-system changes, and it
    // refills because the looping flag makes the system seek the bed back to frame zero.
    EXPECT_EQ(audioSystem.PumpStreamingSource(voice), 2U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), 2U);
    EXPECT_EQ(audioSystem.PumpStreamingSource(voice), 2U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFrames(voice), 4U);

    EXPECT_TRUE(audioSystem.Play(voice));
    audioSystem.DestroySource(voice);
}

} // namespace
} // namespace UVE::Audio::Tests
