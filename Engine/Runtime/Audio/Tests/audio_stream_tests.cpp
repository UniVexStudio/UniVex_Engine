// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"
#include "audio_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Audio::Tests {
namespace {

[[nodiscard]] std::vector<float> Ramp(const std::size_t sampleCount, const std::size_t channels) {
    std::vector<float> samples(sampleCount);
    for (std::size_t index = 0U; index < sampleCount; ++index) {
        samples[index] = static_cast<float>(index % channels) * 0.25F +
                         static_cast<float>(index / channels) * 0.001F;
    }
    return samples;
}

TEST(AudioStreamFormatTest, AcceptsOnlyTheBoundedDeviceRange) {
    EXPECT_TRUE(IsAudioStreamFormatValid(AudioStreamFormat{48000U, 2U}));
    EXPECT_TRUE(IsAudioStreamFormatValid(AudioStreamFormat{8000U, 1U}));
    EXPECT_TRUE(IsAudioStreamFormatValid(AudioStreamFormat{384000U, 2U}));

    EXPECT_FALSE(IsAudioStreamFormatValid(AudioStreamFormat{7999U, 2U}));
    EXPECT_FALSE(IsAudioStreamFormatValid(AudioStreamFormat{384001U, 2U}));
    EXPECT_FALSE(IsAudioStreamFormatValid(AudioStreamFormat{48000U, 0U}));
    EXPECT_FALSE(IsAudioStreamFormatValid(AudioStreamFormat{48000U, 3U}));
}

TEST(AudioStreamRingBufferTest, AnUnconfiguredRingIsInertRatherThanFatal) {
    AudioStreamRingBuffer ring;
    EXPECT_FALSE(ring.IsConfigured());
    EXPECT_EQ(ring.GetBufferedFrames(), 0U);
    EXPECT_EQ(ring.GetFreeFrames(), 0U);
    EXPECT_EQ(ring.Write(nullptr, 4U), 0U);

    // Out-of-contract configurations are refused, not half-applied.
    ring.Configure(0U, 2U);
    EXPECT_FALSE(ring.IsConfigured());
    ring.Configure(4U, 0U);
    EXPECT_FALSE(ring.IsConfigured());
    ring.Configure(kMaximumAudioStreamRingFrames + 1U, 2U);
    EXPECT_FALSE(ring.IsConfigured());
    ring.Configure(4U, kMaximumAudioStreamChannels + 1U);
    EXPECT_FALSE(ring.IsConfigured());

    ring.Configure(4U, 2U);
    EXPECT_TRUE(ring.IsConfigured());
    EXPECT_EQ(ring.GetCapacityFrames(), 4U);
    EXPECT_EQ(ring.GetChannels(), 2U);
    EXPECT_EQ(ring.GetFreeFrames(), 4U);
}

TEST(AudioStreamRingBufferTest, WritesAndReadsFramesInOrder) {
    AudioStreamRingBuffer ring{8U, 2U};
    const std::vector<float> written = Ramp(10U, 2U); // 5 stereo frames
    EXPECT_EQ(ring.Write(written.data(), 5U), 5U);
    EXPECT_EQ(ring.GetBufferedFrames(), 5U);
    EXPECT_EQ(ring.GetFreeFrames(), 3U);

    std::vector<float> read(10U, 0.0F);
    EXPECT_EQ(ring.Read(read.data(), 5U), 5U);
    EXPECT_EQ(read, written);
    EXPECT_EQ(ring.GetBufferedFrames(), 0U);

    // Nothing left: a read is a miss, not a crash.
    EXPECT_EQ(ring.Read(read.data(), 1U), 0U);
}

TEST(AudioStreamRingBufferTest, AFullRingAcceptsAPartialWriteAndSaysSo) {
    AudioStreamRingBuffer ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    // Six frames into a four-frame ring: the newest beyond capacity are dropped, and the count is
    // the contract - a producer that ignores it is the only one at fault.
    EXPECT_EQ(ring.Write(written.data(), written.size()), 4U);
    EXPECT_EQ(ring.GetFreeFrames(), 0U);
    EXPECT_EQ(ring.Write(written.data(), 1U), 0U);

    std::vector<float> read(4U, 0.0F);
    ASSERT_EQ(ring.Read(read.data(), 4U), 4U);
    EXPECT_EQ(read, (std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
}

TEST(AudioStreamRingBufferTest, StorageWrapsAroundItsPhysicalEnd) {
    AudioStreamRingBuffer ring{4U, 1U};
    const std::vector<float> first{1.0F, 2.0F, 3.0F};
    ASSERT_EQ(ring.Write(first.data(), 3U), 3U);
    std::vector<float> drained(2U, 0.0F);
    ASSERT_EQ(ring.Read(drained.data(), 2U), 2U);

    // Pushing three more wraps the write index past the end of the storage and back to the start.
    const std::vector<float> second{4.0F, 5.0F, 6.0F};
    ASSERT_EQ(ring.Write(second.data(), 3U), 3U);

    std::vector<float> tail(4U, 0.0F);
    ASSERT_EQ(ring.Read(tail.data(), 4U), 4U);
    EXPECT_EQ(tail, (std::vector<float>{3.0F, 4.0F, 5.0F, 6.0F}));
}

TEST(AudioStreamRingBufferTest, PeeksAheadWithoutConsumingAnything) {
    AudioStreamRingBuffer ring{4U, 2U};
    const std::vector<float> written{0.1F, 0.2F, 0.3F, 0.4F};
    ASSERT_EQ(ring.Write(written.data(), 2U), 2U);

    float frame[2] = {0.0F, 0.0F};
    ASSERT_TRUE(ring.PeekFrame(0U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.1F);
    EXPECT_FLOAT_EQ(frame[1], 0.2F);
    ASSERT_TRUE(ring.PeekFrame(1U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.3F);
    EXPECT_FLOAT_EQ(frame[1], 0.4F);
    EXPECT_FALSE(ring.PeekFrame(2U, frame));
    EXPECT_EQ(ring.GetBufferedFrames(), 2U); // peeking consumed nothing

    ring.Consume(1U);
    EXPECT_EQ(ring.GetBufferedFrames(), 1U);
    ASSERT_TRUE(ring.PeekFrame(0U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.3F);
}

TEST(AudioStreamRingBufferTest, ConsumeNeverRemovesMoreThanIsBuffered) {
    AudioStreamRingBuffer ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F};
    ASSERT_EQ(ring.Write(written.data(), 2U), 2U);
    ring.Consume(99U);
    EXPECT_EQ(ring.GetBufferedFrames(), 0U);
    // The cursor is back at a clean state, so the ring is fully usable again.
    EXPECT_EQ(ring.GetFreeFrames(), 4U);
    EXPECT_EQ(ring.Write(written.data(), 2U), 2U);
}

TEST(AudioStreamRingBufferTest, ReconfiguringAndClearingBothStartEmpty) {
    AudioStreamRingBuffer ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F, 3.0F};
    ASSERT_EQ(ring.Write(written.data(), 3U), 3U);
    ring.Clear();
    EXPECT_EQ(ring.GetBufferedFrames(), 0U);
    EXPECT_EQ(ring.GetFreeFrames(), 4U);

    ASSERT_EQ(ring.Write(written.data(), 3U), 3U);
    ring.Configure(8U, 2U);
    EXPECT_EQ(ring.GetBufferedFrames(), 0U);
    EXPECT_EQ(ring.GetCapacityFrames(), 8U);
    EXPECT_EQ(ring.GetChannels(), 2U);
}

TEST(AudioStreamRingBufferTest, MonoFramesStayOneSampleWide) {
    AudioStreamRingBuffer ring{2U, 1U};
    const std::vector<float> written{0.5F, -0.5F};
    ASSERT_EQ(ring.Write(written.data(), 2U), 2U);
    EXPECT_EQ(ring.GetFreeFrames(), 0U);
    float frame[2] = {0.0F, 0.0F};
    ASSERT_TRUE(ring.PeekFrame(1U, frame));
    EXPECT_FLOAT_EQ(frame[0], -0.5F);
}

// --- The file-backed source: a `.uvaudio` streamed window by window. ---

class AudioFileStreamSourceTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory = UVE::Tests::MakeTestCaseDirectory("audio_stream");
        path = directory / "tone.uvaudio";
    }

    void WriteAsset(const std::uint16_t channels, const std::uint32_t sampleRate,
                       const std::size_t frames) {
        Asset::AudioAsset audio;
        audio.channels = channels;
        audio.sampleRate = sampleRate;
        audio.samples = Ramp(frames * static_cast<std::size_t>(channels), channels);
        ASSERT_TRUE(Asset::SaveAudioAsset(audio, path));
    }

    std::filesystem::path directory;
    std::filesystem::path path;
};

TEST_F(AudioFileStreamSourceTest, StreamsWindowsInOrderWithoutHoldingTheClip) {
    WriteAsset(1U, 8000U, 16U);

    AudioFileStreamSource source{path};
    ASSERT_TRUE(source.Open());
    EXPECT_TRUE(source.IsOpen());
    EXPECT_EQ(source.GetFormat(), (AudioStreamFormat{8000U, 1U}));
    EXPECT_EQ(source.GetFrameCount(), 16U);

    std::vector<float> window;
    ASSERT_EQ(source.ReadFrames(4U, window), 4U);
    ASSERT_EQ(window.size(), 4U);
    EXPECT_FLOAT_EQ(window[0], 0.0F);
    EXPECT_NEAR(window[3], 0.003F, 1.0e-6F);

    ASSERT_EQ(source.ReadFrames(4U, window), 4U);
    EXPECT_NEAR(window[0], 0.004F, 1.0e-6F);

    // A request larger than what is left returns the remainder, not an error.
    ASSERT_EQ(source.ReadFrames(100U, window), 8U);
    EXPECT_EQ(window.size(), 8U);
    // ... and then answers end-of-stream with zero, leaving the caller's buffer empty.
    EXPECT_EQ(source.ReadFrames(4U, window), 0U);
    EXPECT_TRUE(window.empty());
}

TEST_F(AudioFileStreamSourceTest, SeeksAnywhereInsideTheClipAndRefusesPastTheEnd) {
    WriteAsset(2U, 48000U, 8U);
    AudioFileStreamSource source{path};
    ASSERT_TRUE(source.Open());

    ASSERT_TRUE(source.SeekToFrame(6U));
    std::vector<float> window;
    ASSERT_EQ(source.ReadFrames(2U, window), 2U);
    // Frame 6 of a 2-channel ramp is (0.0, 0.25) plus the frame's own 0.001 * 6 contribution.
    EXPECT_NEAR(window[0], 0.006F, 1.0e-6F);
    EXPECT_NEAR(window[1], 0.256F, 1.0e-6F);

    ASSERT_TRUE(source.SeekToFrame(0U));
    ASSERT_EQ(source.ReadFrames(1U, window), 1U);
    EXPECT_FLOAT_EQ(window[0], 0.0F);

    EXPECT_TRUE(source.SeekToFrame(8U)); // exactly the end is a legal position
    EXPECT_EQ(source.ReadFrames(1U, window), 0U);
    EXPECT_FALSE(source.SeekToFrame(9U));
    EXPECT_TRUE(source.SeekToFrame(0U));
}

TEST_F(AudioFileStreamSourceTest, RefusesAMissingFileWithoutThrowing) {
    AudioFileStreamSource source{directory / "absent.uvaudio"};
    EXPECT_FALSE(source.Open());
    EXPECT_FALSE(source.IsOpen());
    std::vector<float> window;
    EXPECT_EQ(source.ReadFrames(4U, window), 0U);
    EXPECT_TRUE(window.empty());
}

TEST_F(AudioFileStreamSourceTest, RefusesADifferentAssetKind) {
    const std::vector<std::byte> payload{std::byte{1}, std::byte{2}};
    ASSERT_TRUE(Asset::WriteUveFile(path, Asset::AssetKind::Blob, payload));
    AudioFileStreamSource source{path};
    EXPECT_FALSE(source.Open());
}

TEST_F(AudioFileStreamSourceTest, RefusesAFileShorterThanItDeclares) {
    WriteAsset(1U, 8000U, 8U);
    const std::uintmax_t size = std::filesystem::file_size(path);
    ASSERT_GT(size, 4U);
    std::filesystem::resize_file(path, size - 4U);

    AudioFileStreamSource source{path};
    EXPECT_FALSE(source.Open());
}

TEST_F(AudioFileStreamSourceTest, RefusesAnUnsupportedPayloadVersion) {
    WriteAsset(1U, 8000U, 4U);
    std::vector<std::byte> bytes;
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(stream.is_open());
        bytes.resize(static_cast<std::size_t>(stream.tellg()));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    // The payload's own version is the first four bytes after the universal envelope header.
    const std::size_t versionOffset = Asset::kUveFileEnvelopeHeaderBytes;
    ASSERT_LT(versionOffset + sizeof(std::uint32_t), bytes.size());
    bytes[versionOffset] = std::byte{2};
    bytes[versionOffset + 1U] = std::byte{0};
    bytes[versionOffset + 2U] = std::byte{0};
    bytes[versionOffset + 3U] = std::byte{0};
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.is_open());
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    AudioFileStreamSource source{path};
    EXPECT_FALSE(source.Open());
}

} // namespace
} // namespace UVE::Audio::Tests
