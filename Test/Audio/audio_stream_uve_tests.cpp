// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_stream_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"
#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"

namespace UVE::Audio::Tests {
namespace {

[[nodiscard]] std::vector<float> RampUVE(const std::size_t sampleCount, const std::size_t channels) {
    std::vector<float> samples(sampleCount);
    for (std::size_t index = 0U; index < sampleCount; ++index) {
        samples[index] = static_cast<float>(index % channels) * 0.25F +
                         static_cast<float>(index / channels) * 0.001F;
    }
    return samples;
}

TEST(AudioStreamFormatUVETest, AcceptsOnlyTheBoundedDeviceRange) {
    EXPECT_TRUE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{48000U, 2U}));
    EXPECT_TRUE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{8000U, 1U}));
    EXPECT_TRUE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{384000U, 2U}));

    EXPECT_FALSE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{7999U, 2U}));
    EXPECT_FALSE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{384001U, 2U}));
    EXPECT_FALSE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{48000U, 0U}));
    EXPECT_FALSE(IsAudioStreamFormatValidUVE(AudioStreamFormatUVE{48000U, 3U}));
}

TEST(AudioStreamRingBufferUVETest, AnUnconfiguredRingIsInertRatherThanFatal) {
    AudioStreamRingBufferUVE ring;
    EXPECT_FALSE(ring.IsConfiguredUVE());
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 0U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 0U);
    EXPECT_EQ(ring.WriteUVE(nullptr, 4U), 0U);

    // Out-of-contract configurations are refused, not half-applied.
    ring.ConfigureUVE(0U, 2U);
    EXPECT_FALSE(ring.IsConfiguredUVE());
    ring.ConfigureUVE(4U, 0U);
    EXPECT_FALSE(ring.IsConfiguredUVE());
    ring.ConfigureUVE(kMaximumAudioStreamRingFramesUVE + 1U, 2U);
    EXPECT_FALSE(ring.IsConfiguredUVE());
    ring.ConfigureUVE(4U, kMaximumAudioStreamChannelsUVE + 1U);
    EXPECT_FALSE(ring.IsConfiguredUVE());

    ring.ConfigureUVE(4U, 2U);
    EXPECT_TRUE(ring.IsConfiguredUVE());
    EXPECT_EQ(ring.GetCapacityFramesUVE(), 4U);
    EXPECT_EQ(ring.GetChannelsUVE(), 2U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 4U);
}

TEST(AudioStreamRingBufferUVETest, WritesAndReadsFramesInOrder) {
    AudioStreamRingBufferUVE ring{8U, 2U};
    const std::vector<float> written = RampUVE(10U, 2U); // 5 stereo frames
    EXPECT_EQ(ring.WriteUVE(written.data(), 5U), 5U);
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 5U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 3U);

    std::vector<float> read(10U, 0.0F);
    EXPECT_EQ(ring.ReadUVE(read.data(), 5U), 5U);
    EXPECT_EQ(read, written);
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 0U);

    // Nothing left: a read is a miss, not a crash.
    EXPECT_EQ(ring.ReadUVE(read.data(), 1U), 0U);
}

TEST(AudioStreamRingBufferUVETest, AFullRingAcceptsAPartialWriteAndSaysSo) {
    AudioStreamRingBufferUVE ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    // Six frames into a four-frame ring: the newest beyond capacity are dropped, and the count is
    // the contract - a producer that ignores it is the only one at fault.
    EXPECT_EQ(ring.WriteUVE(written.data(), written.size()), 4U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 0U);
    EXPECT_EQ(ring.WriteUVE(written.data(), 1U), 0U);

    std::vector<float> read(4U, 0.0F);
    ASSERT_EQ(ring.ReadUVE(read.data(), 4U), 4U);
    EXPECT_EQ(read, (std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
}

TEST(AudioStreamRingBufferUVETest, StorageWrapsAroundItsPhysicalEnd) {
    AudioStreamRingBufferUVE ring{4U, 1U};
    const std::vector<float> first{1.0F, 2.0F, 3.0F};
    ASSERT_EQ(ring.WriteUVE(first.data(), 3U), 3U);
    std::vector<float> drained(2U, 0.0F);
    ASSERT_EQ(ring.ReadUVE(drained.data(), 2U), 2U);

    // Pushing three more wraps the write index past the end of the storage and back to the start.
    const std::vector<float> second{4.0F, 5.0F, 6.0F};
    ASSERT_EQ(ring.WriteUVE(second.data(), 3U), 3U);

    std::vector<float> tail(4U, 0.0F);
    ASSERT_EQ(ring.ReadUVE(tail.data(), 4U), 4U);
    EXPECT_EQ(tail, (std::vector<float>{3.0F, 4.0F, 5.0F, 6.0F}));
}

TEST(AudioStreamRingBufferUVETest, PeeksAheadWithoutConsumingAnything) {
    AudioStreamRingBufferUVE ring{4U, 2U};
    const std::vector<float> written{0.1F, 0.2F, 0.3F, 0.4F};
    ASSERT_EQ(ring.WriteUVE(written.data(), 2U), 2U);

    float frame[2] = {0.0F, 0.0F};
    ASSERT_TRUE(ring.PeekFrameUVE(0U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.1F);
    EXPECT_FLOAT_EQ(frame[1], 0.2F);
    ASSERT_TRUE(ring.PeekFrameUVE(1U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.3F);
    EXPECT_FLOAT_EQ(frame[1], 0.4F);
    EXPECT_FALSE(ring.PeekFrameUVE(2U, frame));
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 2U); // peeking consumed nothing

    ring.ConsumeUVE(1U);
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 1U);
    ASSERT_TRUE(ring.PeekFrameUVE(0U, frame));
    EXPECT_FLOAT_EQ(frame[0], 0.3F);
}

TEST(AudioStreamRingBufferUVETest, ConsumeNeverRemovesMoreThanIsBuffered) {
    AudioStreamRingBufferUVE ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F};
    ASSERT_EQ(ring.WriteUVE(written.data(), 2U), 2U);
    ring.ConsumeUVE(99U);
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 0U);
    // The cursor is back at a clean state, so the ring is fully usable again.
    EXPECT_EQ(ring.GetFreeFramesUVE(), 4U);
    EXPECT_EQ(ring.WriteUVE(written.data(), 2U), 2U);
}

TEST(AudioStreamRingBufferUVETest, ReconfiguringAndClearingBothStartEmpty) {
    AudioStreamRingBufferUVE ring{4U, 1U};
    const std::vector<float> written{1.0F, 2.0F, 3.0F};
    ASSERT_EQ(ring.WriteUVE(written.data(), 3U), 3U);
    ring.ClearUVE();
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 0U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 4U);

    ASSERT_EQ(ring.WriteUVE(written.data(), 3U), 3U);
    ring.ConfigureUVE(8U, 2U);
    EXPECT_EQ(ring.GetBufferedFramesUVE(), 0U);
    EXPECT_EQ(ring.GetCapacityFramesUVE(), 8U);
    EXPECT_EQ(ring.GetChannelsUVE(), 2U);
}

TEST(AudioStreamRingBufferUVETest, MonoFramesStayOneSampleWide) {
    AudioStreamRingBufferUVE ring{2U, 1U};
    const std::vector<float> written{0.5F, -0.5F};
    ASSERT_EQ(ring.WriteUVE(written.data(), 2U), 2U);
    EXPECT_EQ(ring.GetFreeFramesUVE(), 0U);
    float frame[2] = {0.0F, 0.0F};
    ASSERT_TRUE(ring.PeekFrameUVE(1U, frame));
    EXPECT_FLOAT_EQ(frame[0], -0.5F);
}

// --- The file-backed source: a `.uvaudio` streamed window by window. ---

class AudioFileStreamSourceUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        directory = UVE::Tests::MakeTestCaseDirectoryUVE("audio_stream");
        path = directory / "tone.uvaudio";
    }

    void WriteAssetUVE(const std::uint16_t channels, const std::uint32_t sampleRate,
                       const std::size_t frames) {
        Asset::AudioAssetUVE audio;
        audio.channels = channels;
        audio.sampleRate = sampleRate;
        audio.samples = RampUVE(frames * static_cast<std::size_t>(channels), channels);
        ASSERT_TRUE(Asset::SaveAudioAssetUVE(audio, path));
    }

    std::filesystem::path directory;
    std::filesystem::path path;
};

TEST_F(AudioFileStreamSourceUVETest, StreamsWindowsInOrderWithoutHoldingTheClip) {
    WriteAssetUVE(1U, 8000U, 16U);

    AudioFileStreamSourceUVE source{path};
    ASSERT_TRUE(source.OpenUVE());
    EXPECT_TRUE(source.IsOpenUVE());
    EXPECT_EQ(source.GetFormatUVE(), (AudioStreamFormatUVE{8000U, 1U}));
    EXPECT_EQ(source.GetFrameCountUVE(), 16U);

    std::vector<float> window;
    ASSERT_EQ(source.ReadFramesUVE(4U, window), 4U);
    ASSERT_EQ(window.size(), 4U);
    EXPECT_FLOAT_EQ(window[0], 0.0F);
    EXPECT_NEAR(window[3], 0.003F, 1.0e-6F);

    ASSERT_EQ(source.ReadFramesUVE(4U, window), 4U);
    EXPECT_NEAR(window[0], 0.004F, 1.0e-6F);

    // A request larger than what is left returns the remainder, not an error.
    ASSERT_EQ(source.ReadFramesUVE(100U, window), 8U);
    EXPECT_EQ(window.size(), 8U);
    // ... and then answers end-of-stream with zero, leaving the caller's buffer empty.
    EXPECT_EQ(source.ReadFramesUVE(4U, window), 0U);
    EXPECT_TRUE(window.empty());
}

TEST_F(AudioFileStreamSourceUVETest, SeeksAnywhereInsideTheClipAndRefusesPastTheEnd) {
    WriteAssetUVE(2U, 48000U, 8U);
    AudioFileStreamSourceUVE source{path};
    ASSERT_TRUE(source.OpenUVE());

    ASSERT_TRUE(source.SeekToFrameUVE(6U));
    std::vector<float> window;
    ASSERT_EQ(source.ReadFramesUVE(2U, window), 2U);
    // Frame 6 of a 2-channel ramp is (0.0, 0.25) plus the frame's own 0.001 * 6 contribution.
    EXPECT_NEAR(window[0], 0.006F, 1.0e-6F);
    EXPECT_NEAR(window[1], 0.256F, 1.0e-6F);

    ASSERT_TRUE(source.SeekToFrameUVE(0U));
    ASSERT_EQ(source.ReadFramesUVE(1U, window), 1U);
    EXPECT_FLOAT_EQ(window[0], 0.0F);

    EXPECT_TRUE(source.SeekToFrameUVE(8U)); // exactly the end is a legal position
    EXPECT_EQ(source.ReadFramesUVE(1U, window), 0U);
    EXPECT_FALSE(source.SeekToFrameUVE(9U));
    EXPECT_TRUE(source.SeekToFrameUVE(0U));
}

TEST_F(AudioFileStreamSourceUVETest, RefusesAMissingFileWithoutThrowing) {
    AudioFileStreamSourceUVE source{directory / "absent.uvaudio"};
    EXPECT_FALSE(source.OpenUVE());
    EXPECT_FALSE(source.IsOpenUVE());
    std::vector<float> window;
    EXPECT_EQ(source.ReadFramesUVE(4U, window), 0U);
    EXPECT_TRUE(window.empty());
}

TEST_F(AudioFileStreamSourceUVETest, RefusesADifferentAssetKind) {
    const std::vector<std::byte> payload{std::byte{1}, std::byte{2}};
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, Asset::AssetKindUVE::Blob, payload));
    AudioFileStreamSourceUVE source{path};
    EXPECT_FALSE(source.OpenUVE());
}

TEST_F(AudioFileStreamSourceUVETest, RefusesAFileShorterThanItDeclares) {
    WriteAssetUVE(1U, 8000U, 8U);
    const std::uintmax_t size = std::filesystem::file_size(path);
    ASSERT_GT(size, 4U);
    std::filesystem::resize_file(path, size - 4U);

    AudioFileStreamSourceUVE source{path};
    EXPECT_FALSE(source.OpenUVE());
}

TEST_F(AudioFileStreamSourceUVETest, RefusesAnUnsupportedPayloadVersion) {
    WriteAssetUVE(1U, 8000U, 4U);
    std::vector<std::byte> bytes;
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(stream.is_open());
        bytes.resize(static_cast<std::size_t>(stream.tellg()));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    // The payload's own version is the first four bytes after the universal envelope header.
    const std::size_t versionOffset = Asset::kUveFileEnvelopeHeaderBytesUVE;
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

    AudioFileStreamSourceUVE source{path};
    EXPECT_FALSE(source.OpenUVE());
}

} // namespace
} // namespace UVE::Audio::Tests
