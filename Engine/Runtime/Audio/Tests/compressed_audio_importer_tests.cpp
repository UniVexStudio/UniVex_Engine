// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "compressed_audio_importer.h"
#include "wav_importer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "Support/test_scratch.h"
#include "asset_database.h"
#include "asset_importer.h"

namespace {
using namespace UVE;
struct CodecCase {
    Audio::EncodedAudioFormat format;
    const char *extension;
};
void PrintTo(const CodecCase &value, std::ostream *output) { *output << value.extension; }
constexpr std::array<CodecCase, 3> kCases{{{Audio::EncodedAudioFormat::Flac, "flac"},
                                                 {Audio::EncodedAudioFormat::Mp3, "mp3"},
                                                 {Audio::EncodedAudioFormat::Vorbis, "ogg"}}};
std::filesystem::path Fixture(const char *extension) {
    return Tests::RepositoryRoot() / "Engine/Runtime/Audio/Tests/Fixtures" / (std::string("tone.") + extension);
}
std::vector<std::byte> Read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() < 0)
        throw std::runtime_error("fixture could not be read");
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream)
        throw std::runtime_error("fixture is truncated");
    return bytes;
}
void ExpectRetained(const Asset::AudioAsset &audio) {
    EXPECT_EQ(audio.channels, 2U);
    EXPECT_EQ(audio.sampleRate, 44100U);
    EXPECT_EQ(audio.samples, (std::vector<float>{0.25F, -0.25F}));
}
class CompressedAudioTest : public ::testing::TestWithParam<CodecCase> {};
TEST_P(CompressedAudioTest, DecodesRealSignalWithoutOpeningADevice) {
    const auto [format, extension] = GetParam();
    Asset::AudioAsset audio;
    ASSERT_TRUE(Audio::DecodeEncodedAudio(Read(Fixture(extension)), format, audio));
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_EQ(audio.sampleRate, 8000U);
    ASSERT_FALSE(audio.samples.empty());
    EXPECT_LE(audio.samples.size(), Asset::kMaximumAudioAssetSamples);
    double energy = 0.0;
    for (float sample : audio.samples) {
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_GE(sample, -1.0F);
        EXPECT_LE(sample, 1.0F);
        energy += static_cast<double>(sample) * sample;
    }
    EXPECT_GT(energy / static_cast<double>(audio.samples.size()), 0.00001);
    if (format == Audio::EncodedAudioFormat::Flac) {
        ASSERT_EQ(audio.samples.size(), 512U);
        EXPECT_FLOAT_EQ(audio.samples[0], 0.0F);
        EXPECT_NEAR(audio.samples[1], 0.0423F, 0.0001F);
    }
}
TEST_P(CompressedAudioTest, ImportsExistingEnvelopeAndRegistersGuid) {
    const auto entry = GetParam();
    const auto destination = Tests::MakeTestCaseDirectory() / "tone.uvaudio";
    Asset::AssetImporter importer;
    Audio::RegisterCompressedAudioImporters(importer);
    Asset::AssetDatabase database;
    const auto classification = importer.ClassifySource(Fixture(entry.extension));
    EXPECT_EQ(classification.kind, Asset::AssetImportSourceKind::RawAudio);
    EXPECT_TRUE(classification.importerRegistered);
    const auto guid = importer.Import(Fixture(entry.extension), destination, database);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destination);
    Asset::AudioAsset imported, decoded;
    ASSERT_TRUE(Asset::LoadAudioAsset(destination, imported));
    ASSERT_TRUE(Audio::DecodeEncodedAudio(Read(Fixture(entry.extension)), entry.format, decoded));
    EXPECT_EQ(imported.channels, decoded.channels);
    EXPECT_EQ(imported.sampleRate, decoded.sampleRate);
    EXPECT_EQ(imported.samples, decoded.samples);
}
TEST_P(CompressedAudioTest, TruncatedHeaderPreservesOutput) {
    auto bytes = Read(Fixture(GetParam().extension));
    bytes.resize(3U);
    Asset::AudioAsset output{2U, 44100U, {0.25F, -0.25F}};
    EXPECT_FALSE(Audio::DecodeEncodedAudio(bytes, GetParam().format, output));
    ExpectRetained(output);
}
TEST_P(CompressedAudioTest, MalformedSourcePreservesDestinationAndDoesNotRegister) {
    const auto directory = Tests::MakeTestCaseDirectory();
    const auto source = directory / (std::string("invalid.") + GetParam().extension);
    {
        std::ofstream file(source, std::ios::binary);
        file << "not an encoded audio stream";
    }
    const auto destination = directory / "retained.uvaudio";
    ASSERT_TRUE(Asset::SaveAudioAsset({2U, 44100U, {0.25F, -0.25F}}, destination));
    const auto originalBytes = Read(destination);
    Asset::AssetImporter importer;
    Audio::RegisterCompressedAudioImporters(importer);
    Asset::AssetDatabase database;
    EXPECT_EQ(importer.Import(source, destination, database), Asset::kInvalidAssetGuid);
    EXPECT_EQ(Read(destination), originalBytes);
    EXPECT_TRUE(database.GetRegisteredAssets().empty());
}
INSTANTIATE_TEST_SUITE_P(Formats, CompressedAudioTest, ::testing::ValuesIn(kCases),
                         [](const auto &codecParameter) {
                             return std::string(codecParameter.param.extension);
                         });

TEST(CompressedAudioBoundaryTest, WrongFormatAndUnknownEnumNeverFallBackOrMutateOutput) {
    Asset::AudioAsset output{2U, 44100U, {0.25F, -0.25F}};
    for (const auto source : kCases) {
        const auto bytes = Read(Fixture(source.extension));
        for (const auto target : kCases) {
            if (source.format == target.format)
                continue;
            EXPECT_FALSE(Audio::DecodeEncodedAudio(bytes, target.format, output));
            ExpectRetained(output);
        }
        EXPECT_FALSE(
            Audio::DecodeEncodedAudio(bytes, static_cast<Audio::EncodedAudioFormat>(255), output));
        ExpectRetained(output);
    }
    EXPECT_FALSE(Audio::DecodeEncodedAudio({}, Audio::EncodedAudioFormat::Flac, output));
    ExpectRetained(output);
}
TEST(CompressedAudioBoundaryTest, DeclaredDecodedLengthOverAssetCapIsRejected) {
    auto bytes = Read(Fixture("flac"));
    ASSERT_GT(bytes.size(), 26U);
    // STREAMINFO's low 36 bits carry total samples. Advertise 2^25 frames without
    // inflating the fixture. The decoder must refuse before allocating that output.
    bytes[21] &= std::byte{0xF0};
    bytes[22] = std::byte{0x02};
    bytes[23] = std::byte{0};
    bytes[24] = std::byte{0};
    bytes[25] = std::byte{0};
    Asset::AudioAsset output{2U, 44100U, {0.25F, -0.25F}};
    EXPECT_FALSE(Audio::DecodeEncodedAudio(bytes, Audio::EncodedAudioFormat::Flac, output));
    ExpectRetained(output);
}
TEST(CompressedAudioBoundaryTest, SparseOversizeSourceIsRejectedBeforeReadingItsContents) {
    const auto directory = Tests::MakeTestCaseDirectory();
    const auto source = directory / "oversize.flac";
    {
        std::ofstream file(source, std::ios::binary);
        ASSERT_TRUE(file.good());
        file.seekp(static_cast<std::streamoff>(Audio::kMaximumEncodedAudioSourceBytes));
        file.put('x');
        ASSERT_TRUE(file.good());
    }
    Asset::AssetImporter importer;
    Audio::RegisterCompressedAudioImporters(importer);
    Asset::AssetDatabase database;
    EXPECT_EQ(importer.Import(source, directory / "output.uvaudio", database),
              Asset::kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(directory / "output.uvaudio"));
}
TEST(CompressedAudioBoundaryTest, PublicationFailureCleansTemporaryAndDoesNotReplaceDirectory) {
    const auto directory = Tests::MakeTestCaseDirectory();
    const auto destination = directory / "blocked.uvaudio";
    ASSERT_TRUE(std::filesystem::create_directory(destination));
    Asset::AssetImporter importer;
    Audio::RegisterCompressedAudioImporters(importer);
    Asset::AssetDatabase database;
    EXPECT_EQ(importer.Import(Fixture("flac"), destination, database), Asset::kInvalidAssetGuid);
    EXPECT_TRUE(std::filesystem::is_directory(destination));
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path(), destination);
}
TEST(CompressedAudioBoundaryTest, ReplacementAndWavRegistrationRemainCompatible) {
    const auto directory = Tests::MakeTestCaseDirectory();
    const auto destination = directory / "replace.uvaudio";
    ASSERT_TRUE(Asset::SaveAudioAsset({1U, 44100U, {0.5F}}, destination));
    Asset::AssetImporter importer;
    Audio::RegisterWavImporter(importer);
    Audio::RegisterCompressedAudioImporters(importer);
    Asset::AssetDatabase database;
    ASSERT_NE(importer.Import(Fixture("flac"), destination, database), Asset::kInvalidAssetGuid);
    Asset::AudioAsset result;
    ASSERT_TRUE(Asset::LoadAudioAsset(destination, result));
    EXPECT_EQ(result.sampleRate, 8000U);
    EXPECT_EQ(result.samples.size(), 512U);
    EXPECT_TRUE(importer.ClassifySource("existing.wav").importerRegistered);
    EXPECT_EQ(importer.Import(Fixture("mp3"), directory / "wrong.uvtex", database),
              Asset::kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(directory / "wrong.uvtex"));
}
} // namespace
