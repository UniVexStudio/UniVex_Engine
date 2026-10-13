// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/audio/compressed_audio_importer_uve.h"
#include "uve/audio/wav_importer_uve.h"
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
#include "Support/test_scratch_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_importer_uve.h"

namespace {
using namespace UVE;
struct CodecCaseUVE {
    Audio::EncodedAudioFormatUVE format;
    const char *extension;
};
void PrintTo(const CodecCaseUVE &value, std::ostream *output) { *output << value.extension; }
constexpr std::array<CodecCaseUVE, 3> kCasesUVE{{{Audio::EncodedAudioFormatUVE::Flac, "flac"},
                                                 {Audio::EncodedAudioFormatUVE::Mp3, "mp3"},
                                                 {Audio::EncodedAudioFormatUVE::Vorbis, "ogg"}}};
std::filesystem::path FixtureUVE(const char *extension) {
    return Tests::RepositoryRootUVE() / "Test/Audio/Fixtures" / (std::string("tone_uve.") + extension);
}
std::vector<std::byte> ReadUVE(const std::filesystem::path &path) {
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
void ExpectRetainedUVE(const Asset::AudioAssetUVE &audio) {
    EXPECT_EQ(audio.channels, 2U);
    EXPECT_EQ(audio.sampleRate, 44100U);
    EXPECT_EQ(audio.samples, (std::vector<float>{0.25F, -0.25F}));
}
class CompressedAudioUVETest : public ::testing::TestWithParam<CodecCaseUVE> {};
TEST_P(CompressedAudioUVETest, DecodesRealSignalWithoutOpeningADevice) {
    const auto [format, extension] = GetParam();
    Asset::AudioAssetUVE audio;
    ASSERT_TRUE(Audio::DecodeEncodedAudioUVE(ReadUVE(FixtureUVE(extension)), format, audio));
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_EQ(audio.sampleRate, 8000U);
    ASSERT_FALSE(audio.samples.empty());
    EXPECT_LE(audio.samples.size(), Asset::kMaximumAudioAssetSamplesUVE);
    double energy = 0.0;
    for (float sample : audio.samples) {
        EXPECT_TRUE(std::isfinite(sample));
        EXPECT_GE(sample, -1.0F);
        EXPECT_LE(sample, 1.0F);
        energy += static_cast<double>(sample) * sample;
    }
    EXPECT_GT(energy / static_cast<double>(audio.samples.size()), 0.00001);
    if (format == Audio::EncodedAudioFormatUVE::Flac) {
        ASSERT_EQ(audio.samples.size(), 512U);
        EXPECT_FLOAT_EQ(audio.samples[0], 0.0F);
        EXPECT_NEAR(audio.samples[1], 0.0423F, 0.0001F);
    }
}
TEST_P(CompressedAudioUVETest, ImportsExistingEnvelopeAndRegistersGuid) {
    const auto entry = GetParam();
    const auto destination = Tests::MakeTestCaseDirectoryUVE() / "tone.uvaudio";
    Asset::AssetImporterUVE importer;
    Audio::RegisterCompressedAudioImportersUVE(importer);
    Asset::AssetDatabaseUVE database;
    const auto classification = importer.ClassifySourceUVE(FixtureUVE(entry.extension));
    EXPECT_EQ(classification.kind, Asset::AssetImportSourceKindUVE::RawAudio);
    EXPECT_TRUE(classification.importerRegistered);
    const auto guid = importer.ImportUVE(FixtureUVE(entry.extension), destination, database);
    ASSERT_NE(guid, Asset::kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destination);
    Asset::AudioAssetUVE imported, decoded;
    ASSERT_TRUE(Asset::LoadAudioAssetUVE(destination, imported));
    ASSERT_TRUE(Audio::DecodeEncodedAudioUVE(ReadUVE(FixtureUVE(entry.extension)), entry.format, decoded));
    EXPECT_EQ(imported.channels, decoded.channels);
    EXPECT_EQ(imported.sampleRate, decoded.sampleRate);
    EXPECT_EQ(imported.samples, decoded.samples);
}
TEST_P(CompressedAudioUVETest, TruncatedHeaderPreservesOutput) {
    auto bytes = ReadUVE(FixtureUVE(GetParam().extension));
    bytes.resize(3U);
    Asset::AudioAssetUVE output{2U, 44100U, {0.25F, -0.25F}};
    EXPECT_FALSE(Audio::DecodeEncodedAudioUVE(bytes, GetParam().format, output));
    ExpectRetainedUVE(output);
}
TEST_P(CompressedAudioUVETest, MalformedSourcePreservesDestinationAndDoesNotRegister) {
    const auto directory = Tests::MakeTestCaseDirectoryUVE();
    const auto source = directory / (std::string("invalid.") + GetParam().extension);
    {
        std::ofstream file(source, std::ios::binary);
        file << "not an encoded audio stream";
    }
    const auto destination = directory / "retained.uvaudio";
    ASSERT_TRUE(Asset::SaveAudioAssetUVE({2U, 44100U, {0.25F, -0.25F}}, destination));
    const auto originalBytes = ReadUVE(destination);
    Asset::AssetImporterUVE importer;
    Audio::RegisterCompressedAudioImportersUVE(importer);
    Asset::AssetDatabaseUVE database;
    EXPECT_EQ(importer.ImportUVE(source, destination, database), Asset::kInvalidAssetGuidUVE);
    EXPECT_EQ(ReadUVE(destination), originalBytes);
    EXPECT_TRUE(database.GetRegisteredAssetsUVE().empty());
}
INSTANTIATE_TEST_SUITE_P(Formats, CompressedAudioUVETest, ::testing::ValuesIn(kCasesUVE),
                         [](const auto &codecParameter) {
                             return std::string(codecParameter.param.extension);
                         });

TEST(CompressedAudioBoundaryUVETest, WrongFormatAndUnknownEnumNeverFallBackOrMutateOutput) {
    Asset::AudioAssetUVE output{2U, 44100U, {0.25F, -0.25F}};
    for (const auto source : kCasesUVE) {
        const auto bytes = ReadUVE(FixtureUVE(source.extension));
        for (const auto target : kCasesUVE) {
            if (source.format == target.format)
                continue;
            EXPECT_FALSE(Audio::DecodeEncodedAudioUVE(bytes, target.format, output));
            ExpectRetainedUVE(output);
        }
        EXPECT_FALSE(
            Audio::DecodeEncodedAudioUVE(bytes, static_cast<Audio::EncodedAudioFormatUVE>(255), output));
        ExpectRetainedUVE(output);
    }
    EXPECT_FALSE(Audio::DecodeEncodedAudioUVE({}, Audio::EncodedAudioFormatUVE::Flac, output));
    ExpectRetainedUVE(output);
}
TEST(CompressedAudioBoundaryUVETest, DeclaredDecodedLengthOverAssetCapIsRejected) {
    auto bytes = ReadUVE(FixtureUVE("flac"));
    ASSERT_GT(bytes.size(), 26U);
    // STREAMINFO's low 36 bits carry total samples. Advertise 2^21 frames without
    // inflating the fixture. The decoder must refuse before allocating that output.
    bytes[21] &= std::byte{0xF0};
    bytes[22] = std::byte{0};
    bytes[23] = std::byte{0x20};
    bytes[24] = std::byte{0};
    bytes[25] = std::byte{0};
    Asset::AudioAssetUVE output{2U, 44100U, {0.25F, -0.25F}};
    EXPECT_FALSE(Audio::DecodeEncodedAudioUVE(bytes, Audio::EncodedAudioFormatUVE::Flac, output));
    ExpectRetainedUVE(output);
}
TEST(CompressedAudioBoundaryUVETest, SparseOversizeSourceIsRejectedBeforeReadingItsContents) {
    const auto directory = Tests::MakeTestCaseDirectoryUVE();
    const auto source = directory / "oversize.flac";
    {
        std::ofstream file(source, std::ios::binary);
        ASSERT_TRUE(file.good());
        file.seekp(static_cast<std::streamoff>(Audio::kMaximumEncodedAudioSourceBytesUVE));
        file.put('x');
        ASSERT_TRUE(file.good());
    }
    Asset::AssetImporterUVE importer;
    Audio::RegisterCompressedAudioImportersUVE(importer);
    Asset::AssetDatabaseUVE database;
    EXPECT_EQ(importer.ImportUVE(source, directory / "output.uvaudio", database),
              Asset::kInvalidAssetGuidUVE);
    EXPECT_FALSE(std::filesystem::exists(directory / "output.uvaudio"));
}
TEST(CompressedAudioBoundaryUVETest, PublicationFailureCleansTemporaryAndDoesNotReplaceDirectory) {
    const auto directory = Tests::MakeTestCaseDirectoryUVE();
    const auto destination = directory / "blocked.uvaudio";
    ASSERT_TRUE(std::filesystem::create_directory(destination));
    Asset::AssetImporterUVE importer;
    Audio::RegisterCompressedAudioImportersUVE(importer);
    Asset::AssetDatabaseUVE database;
    EXPECT_EQ(importer.ImportUVE(FixtureUVE("flac"), destination, database), Asset::kInvalidAssetGuidUVE);
    EXPECT_TRUE(std::filesystem::is_directory(destination));
    for (const auto &entry : std::filesystem::directory_iterator(directory))
        EXPECT_EQ(entry.path(), destination);
}
TEST(CompressedAudioBoundaryUVETest, ReplacementAndWavRegistrationRemainCompatible) {
    const auto directory = Tests::MakeTestCaseDirectoryUVE();
    const auto destination = directory / "replace.uvaudio";
    ASSERT_TRUE(Asset::SaveAudioAssetUVE({1U, 44100U, {0.5F}}, destination));
    Asset::AssetImporterUVE importer;
    Audio::RegisterWavImporterUVE(importer);
    Audio::RegisterCompressedAudioImportersUVE(importer);
    Asset::AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(FixtureUVE("flac"), destination, database), Asset::kInvalidAssetGuidUVE);
    Asset::AudioAssetUVE result;
    ASSERT_TRUE(Asset::LoadAudioAssetUVE(destination, result));
    EXPECT_EQ(result.sampleRate, 8000U);
    EXPECT_EQ(result.samples.size(), 512U);
    EXPECT_TRUE(importer.ClassifySourceUVE("existing.wav").importerRegistered);
    EXPECT_EQ(importer.ImportUVE(FixtureUVE("mp3"), directory / "wrong.uvtex", database),
              Asset::kInvalidAssetGuidUVE);
    EXPECT_FALSE(std::filesystem::exists(directory / "wrong.uvtex"));
}
} // namespace
