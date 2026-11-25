// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "wav_metadata.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

void AppendTag(std::vector<std::byte>& bytes, const char* tag) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.push_back(std::byte{static_cast<unsigned char>(tag[index])});
    }
}

void AppendU16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

[[nodiscard]] std::vector<std::byte> MakePcmWav(const std::uint32_t dataBytes = 8U) {
    std::vector<std::byte> bytes;
    AppendTag(bytes, "RIFF");
    AppendU32(bytes, 36U + dataBytes);
    AppendTag(bytes, "WAVE");
    AppendTag(bytes, "fmt ");
    AppendU32(bytes, 16U);
    AppendU16(bytes, 1U);
    AppendU16(bytes, 2U);
    AppendU32(bytes, 48000U);
    AppendU32(bytes, 192000U);
    AppendU16(bytes, 4U);
    AppendU16(bytes, 16U);
    AppendTag(bytes, "data");
    AppendU32(bytes, dataBytes);
    bytes.resize(bytes.size() + dataBytes, std::byte{0x00});
    return bytes;
}

TEST(WavMetadataTest, ParseWavMetadata_ReturnsCoherentPcmFacts) {
    const std::optional<WavMetadata> metadata = ParseWavMetadata(MakePcmWav());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->audioFormat, 1U);
    EXPECT_EQ(metadata->channels, 2U);
    EXPECT_EQ(metadata->sampleRate, 48000U);
    EXPECT_EQ(metadata->byteRate, 192000U);
    EXPECT_EQ(metadata->blockAlign, 4U);
    EXPECT_EQ(metadata->bitsPerSample, 16U);
    EXPECT_EQ(metadata->dataBytes, 8U);
    EXPECT_NEAR(metadata->durationSeconds, 8.0 / 192000.0, 1e-8);
}

TEST(WavMetadataTest, ParseWavMetadata_SkipsPaddedJunkChunk) {
    std::vector<std::byte> bytes;
    AppendTag(bytes, "RIFF");
    AppendU32(bytes, 54U);
    AppendTag(bytes, "WAVE");
    AppendTag(bytes, "JUNK");
    AppendU32(bytes, 1U);
    bytes.push_back(std::byte{0x7F});
    bytes.push_back(std::byte{0x00});
    const std::vector<std::byte> pcm = MakePcmWav();
    bytes.insert(bytes.end(), pcm.begin() + 12, pcm.end());
    ASSERT_TRUE(ParseWavMetadata(bytes).has_value());
}

TEST(WavMetadataTest, ParseWavMetadata_StopsAtDeclaredRiffBoundary) {
    std::vector<std::byte> bytes = MakePcmWav();
    AppendTag(bytes, "data");
    AppendU32(bytes, 1U);
    bytes.push_back(std::byte{0xFF});
    EXPECT_TRUE(ParseWavMetadata(bytes).has_value());
}

TEST(WavMetadataTest, ParseWavMetadata_RejectsMalformedOrIncoherentInputs) {
    std::vector<std::byte> badMagic = MakePcmWav();
    badMagic[0] = std::byte{'X'};
    EXPECT_FALSE(ParseWavMetadata(badMagic).has_value());

    std::vector<std::byte> badAlignment = MakePcmWav();
    badAlignment[32] = std::byte{0x03};
    EXPECT_FALSE(ParseWavMetadata(badAlignment).has_value());

    std::vector<std::byte> truncated = MakePcmWav();
    truncated.pop_back();
    EXPECT_FALSE(ParseWavMetadata(truncated).has_value());
}

} // namespace
} // namespace UVE::Audio::Tests
