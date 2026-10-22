// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/ambisonic_stream_source_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"
#include "uve/audio/audio_system_uve.h"
#include "uve/audio/null_audio_device_uve.h"
#include "uve/asset/audio_asset_uve.h"

namespace UVE::Audio::Tests {
namespace {

constexpr float kToleranceUVE = 1.0e-6F;

/// An in-memory source that behaves like a decoded clip: fixed format, a cursor, and a seek.
class FixedStreamSourceUVE final : public IAudioStreamSourceUVE {
public:
    FixedStreamSourceUVE(const std::uint32_t channels, const std::uint32_t sampleRate,
                         std::vector<float> interleavedSamples)
        : m_format{sampleRate, channels}, m_samples(std::move(interleavedSamples)) {}

    [[nodiscard]] AudioStreamFormatUVE GetFormatUVE() const noexcept override { return m_format; }
    [[nodiscard]] std::size_t GetFrameCountUVE() const noexcept override {
        return m_format.channels == 0U ? 0U : m_samples.size() / m_format.channels;
    }

    [[nodiscard]] std::size_t ReadFramesUVE(const std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override {
        outFrames.clear();
        const std::size_t channels = m_format.channels;
        const std::size_t frameCount = GetFrameCountUVE();
        if (channels == 0U || requestedFrames == 0U || m_cursor >= frameCount) return 0U;
        const std::size_t frames = std::min(requestedFrames, frameCount - m_cursor);
        outFrames.assign(m_samples.begin() + static_cast<std::ptrdiff_t>(m_cursor * channels),
                         m_samples.begin() + static_cast<std::ptrdiff_t>((m_cursor + frames) * channels));
        m_cursor += frames;
        return frames;
    }

    [[nodiscard]] bool SeekToFrameUVE(const std::size_t frame) override {
        if (frame > GetFrameCountUVE()) return false;
        m_cursor = frame;
        return true;
    }

private:
    AudioStreamFormatUVE m_format;
    std::vector<float> m_samples;
    std::size_t m_cursor = 0U;
};

/// A source that overruns its own contract by one frame, to prove the adapter does not read past
/// the window it was handed.
class OverrunningStreamSourceUVE final : public IAudioStreamSourceUVE {
public:
    [[nodiscard]] AudioStreamFormatUVE GetFormatUVE() const noexcept override { return AudioStreamFormatUVE{48000U, 4U}; }
    [[nodiscard]] std::size_t GetFrameCountUVE() const noexcept override { return 16U; }
    [[nodiscard]] std::size_t ReadFramesUVE(const std::size_t requestedFrames,
                                            std::vector<float>& outFrames) override {
        outFrames.assign((requestedFrames + 1U) * 4U, 0.5F);
        return requestedFrames + 1U;
    }
    [[nodiscard]] bool SeekToFrameUVE(std::size_t) override { return true; }
};

/// One AmbiX frame repeated `frameCount` times.
[[nodiscard]] std::vector<float> ConstantFramesUVE(const float w, const float y, const float z, const float x,
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

TEST(AmbisonicStreamSourceUVETest, TheSourceAndDeviceChannelCapsAreDifferentOnPurpose) {
    // Four channels are legal for a source and illegal for a device voice: the decoder in between
    // is the only thing allowed to cross that line, so a four-channel file can never be played as
    // if it were stereo.
    static_assert(IsAudioStreamSourceFormatValidUVE(AudioStreamFormatUVE{48000U, 4U}));
    static_assert(!IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{48000U, 4U}));
    static_assert(!IsAudioStreamSourceFormatValidUVE(AudioStreamFormatUVE{48000U, 5U}));
    static_assert(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{48000U, 2U}));
    static_assert(IsAudioStreamSourceFormatValidUVE(AudioStreamFormatUVE{48000U, 1U}));
}

TEST(AmbisonicStreamSourceUVETest, ASourceThatIsNotFirstOrderIsRefused) {
    // Stereo is what the device takes, not what a bed is: a two-channel "ambisonic" source is an
    // authoring mistake, and decoding it as W/Y would silently discard half the field.
    FixedStreamSourceUVE stereo{2U, 48000U, {1.0F, 0.0F, 0.5F, 0.0F}};
    AmbisonicStreamSourceUVE bed{stereo};
    EXPECT_FALSE(bed.IsReadyUVE());
    EXPECT_EQ(bed.GetFormatUVE(), (AudioStreamFormatUVE{0U, 0U}));
    EXPECT_EQ(bed.GetFrameCountUVE(), 0U);

    std::vector<float> out{1.0F};
    EXPECT_EQ(bed.ReadFramesUVE(4U, out), 0U);
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(bed.SeekToFrameUVE(0U));

    FixedStreamSourceUVE fiveChannels{5U, 48000U, std::vector<float>(20U, 0.25F)};
    const AmbisonicStreamSourceUVE outOfRange{fiveChannels};
    EXPECT_FALSE(outOfRange.IsReadyUVE());
}

TEST(AmbisonicStreamSourceUVETest, ReportsStereoAtTheSourcesRateAndForwardsItsLength) {
    FixedStreamSourceUVE source{4U, 44100U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 0.0F, 12U)};
    const AmbisonicStreamSourceUVE bed{source};
    EXPECT_TRUE(bed.IsReadyUVE());
    EXPECT_EQ(bed.GetFormatUVE(), (AudioStreamFormatUVE{44100U, 2U}));
    EXPECT_EQ(bed.GetFrameCountUVE(), 12U);
}

TEST(AmbisonicStreamSourceUVETest, AnOmnidirectionalBedIsUnityOnBothChannels) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 0.0F, 3U)};
    AmbisonicStreamSourceUVE bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(3U, out), 3U);
    ASSERT_EQ(out.size(), 6U);
    for (std::size_t index = 0U; index < out.size(); ++index) {
        EXPECT_FLOAT_EQ(out[index], 1.0F) << "sample " << index;
    }
}

TEST(AmbisonicStreamSourceUVETest, AWaveFromTheLeftDecodesToTheLeftChannel) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 1.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSourceUVE bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(2U, out), 2U);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_FLOAT_EQ(out[0], 2.0F);
    EXPECT_FLOAT_EQ(out[1], 0.0F);
    EXPECT_FLOAT_EQ(out[2], 2.0F);
    EXPECT_FLOAT_EQ(out[3], 0.0F);
}

TEST(AmbisonicStreamSourceUVETest, TurningTheHeadSteersTheField) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 1.0F, 1U)};
    AmbisonicStreamSourceUVE bed{source};

    // Facing the world's front, the wave is centred.
    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(1U, out), 1U);
    EXPECT_NEAR(out[0], 1.0F, kToleranceUVE);
    EXPECT_NEAR(out[1], 1.0F, kToleranceUVE);

    // Turning to face -X puts the same world-locked wave on the listener's right. The frame is
    // re-read from the start, because the point is the orientation, not the cursor.
    ASSERT_TRUE(bed.SetListenerOrientationUVE(Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    ASSERT_TRUE(bed.SeekToFrameUVE(0U));
    ASSERT_EQ(bed.ReadFramesUVE(1U, out), 1U);
    EXPECT_NEAR(out[0], 0.0F, kToleranceUVE);
    EXPECT_NEAR(out[1], 2.0F, kToleranceUVE);
}

TEST(AmbisonicStreamSourceUVETest, GainScalesTheWholeWindowAndIsValidated) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSourceUVE bed{source};

    EXPECT_FLOAT_EQ(bed.GetGainUVE(), 1.0F);
    ASSERT_TRUE(bed.SetGainUVE(0.25F));
    EXPECT_FLOAT_EQ(bed.GetGainUVE(), 0.25F);

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(2U, out), 2U);
    for (const float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.25F);
    }

    EXPECT_FALSE(bed.SetGainUVE(-0.1F));
    EXPECT_FALSE(bed.SetGainUVE(1.5F));
    EXPECT_FALSE(bed.SetGainUVE(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FLOAT_EQ(bed.GetGainUVE(), 0.25F); // a refused write leaves the last good value
}

TEST(AmbisonicStreamSourceUVETest, AnInvalidOrientationIsRefusedAndThePreviousOneSurvives) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 1.0F, 1U)};
    AmbisonicStreamSourceUVE bed{source};

    ASSERT_TRUE(bed.SetListenerOrientationUVE(Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    const AmbisonicRotationUVE previous = bed.GetRotationUVE();

    EXPECT_FALSE(bed.SetListenerOrientationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(bed.SetListenerOrientationUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F}, Math::Vector3UVE{0.0F, 0.0F, 3.0F}));
    EXPECT_FALSE(bed.SetListenerOrientationUVE(
        Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, -1.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    EXPECT_EQ(bed.GetRotationUVE(), previous);

    // The refusal is observable in the output too: the wave is still on the right.
    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(1U, out), 1U);
    EXPECT_NEAR(out[0], 0.0F, kToleranceUVE);
    EXPECT_NEAR(out[1], 2.0F, kToleranceUVE);
}

TEST(AmbisonicStreamSourceUVETest, WindowsAreBoundedByTheRingAndZeroFramesReadNothing) {
    FixedStreamSourceUVE source{4U, 48000U,
                                ConstantFramesUVE(1.0F, 0.0F, 0.0F, 0.0F, kMaximumAudioStreamRingFramesUVE)};
    AmbisonicStreamSourceUVE bed{source};

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFramesUVE(kMaximumAudioStreamRingFramesUVE + 1U, out), 0U);
    EXPECT_TRUE(out.empty());

    ASSERT_EQ(bed.ReadFramesUVE(kMaximumAudioStreamRingFramesUVE, out),
              kMaximumAudioStreamRingFramesUVE); // the ring's own depth is still legal
    EXPECT_EQ(out.size(), static_cast<std::size_t>(kMaximumAudioStreamRingFramesUVE) * 2U);
    EXPECT_EQ(bed.ReadFramesUVE(0U, out), 0U);
    EXPECT_TRUE(out.empty());
}

TEST(AmbisonicStreamSourceUVETest, EndOfSourceIsReportedAndSeekingRestartsIt) {
    FixedStreamSourceUVE source{4U, 48000U, ConstantFramesUVE(1.0F, 0.0F, 0.0F, 0.0F, 2U)};
    AmbisonicStreamSourceUVE bed{source};

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFramesUVE(8U, out), 2U); // a short source returns what it has, not a padded window
    EXPECT_EQ(bed.ReadFramesUVE(8U, out), 0U);
    EXPECT_TRUE(out.empty());

    ASSERT_TRUE(bed.SeekToFrameUVE(0U));
    EXPECT_EQ(bed.ReadFramesUVE(8U, out), 2U);
    // Seeking forward into the middle leaves exactly the tail.
    ASSERT_TRUE(bed.SeekToFrameUVE(1U));
    EXPECT_EQ(bed.ReadFramesUVE(8U, out), 1U);
    EXPECT_FALSE(bed.SeekToFrameUVE(3U));
}

TEST(AmbisonicStreamSourceUVETest, NonFiniteSamplesBecomeSilence) {
    FixedStreamSourceUVE source{4U, 48000U,
                                ConstantFramesUVE(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F, 1U)};
    AmbisonicStreamSourceUVE bed{source};

    std::vector<float> out;
    ASSERT_EQ(bed.ReadFramesUVE(1U, out), 1U);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_FLOAT_EQ(out[0], 0.0F);
    EXPECT_FLOAT_EQ(out[1], 0.0F);
}

TEST(AmbisonicStreamSourceUVETest, ASourceThatOverrunsItsWindowIsDroppedRatherThanRead) {
    OverrunningStreamSourceUVE source;
    AmbisonicStreamSourceUVE bed{source};
    ASSERT_TRUE(bed.IsReadyUVE());

    std::vector<float> out;
    EXPECT_EQ(bed.ReadFramesUVE(4U, out), 0U);
    EXPECT_TRUE(out.empty());
}

TEST(AmbisonicStreamSourceUVETest, FourChannelFilesAreReadableAsStreams) {
    const std::filesystem::path path = UVE::Tests::MakeTestCaseDirectoryUVE("four-channel") / "bed.uvaudio";
    Asset::AudioAssetUVE asset;
    asset.channels = 4U;
    asset.sampleRate = 48000U;
    asset.samples = {0.5F, 0.25F, 0.0F, -0.25F, 0.1F, 0.2F, 0.3F, 0.4F};
    ASSERT_TRUE(Asset::SaveAudioAssetUVE(asset, path));

    AudioFileStreamSourceUVE fileSource{path};
    ASSERT_TRUE(fileSource.OpenUVE());
    EXPECT_EQ(fileSource.GetFormatUVE(), (AudioStreamFormatUVE{48000U, 4U}));
    EXPECT_EQ(fileSource.GetFrameCountUVE(), 2U);

    std::vector<float> window;
    ASSERT_EQ(fileSource.ReadFramesUVE(2U, window), 2U);
    EXPECT_EQ(window, asset.samples);
}

TEST(AmbisonicStreamSourceUVETest, AFileBackedBedPlaysThroughTheAudioSystemAndLoops) {
    const std::filesystem::path path = UVE::Tests::MakeTestCaseDirectoryUVE("bed") / "loop.uvaudio";
    Asset::AudioAssetUVE asset;
    asset.channels = 4U;
    asset.sampleRate = 48000U;
    asset.samples = {1.0F, 1.0F, 0.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F}; // hard left, then hard right
    ASSERT_TRUE(Asset::SaveAudioAssetUVE(asset, path));

    AudioFileStreamSourceUVE fileSource{path};
    ASSERT_TRUE(fileSource.OpenUVE());
    AmbisonicStreamSourceUVE bed{fileSource};
    ASSERT_TRUE(bed.IsReadyUVE());

    NullAudioDeviceUVE device;
    AudioSystemUVE audioSystem{device};

    AudioSourceDescUVE desc;
    desc.spatial = false; // a bed is a field, not a point: distance must not attenuate it
    desc.looping = true;
    const VoiceHandleUVE voice = audioSystem.CreateStreamingSourceUVE(desc, bed);
    ASSERT_NE(voice, kInvalidVoiceHandleUVE);

    // The pump is the whole integration: no device changes, no source-system changes, and it
    // refills because the looping flag makes the system seek the bed back to frame zero.
    EXPECT_EQ(audioSystem.PumpStreamingSourceUVE(voice), 2U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFramesUVE(voice), 2U);
    EXPECT_EQ(audioSystem.PumpStreamingSourceUVE(voice), 2U);
    EXPECT_EQ(audioSystem.GetStreamingSourceBufferedFramesUVE(voice), 4U);

    EXPECT_TRUE(audioSystem.PlayUVE(voice));
    audioSystem.DestroySourceUVE(voice);
}

} // namespace
} // namespace UVE::Audio::Tests
