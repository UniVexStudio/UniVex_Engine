// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "wav_importer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "audio_asset.h"
#include "asset_importer.h"

namespace UVE::Audio::Tests {
namespace {

void AppendU16LittleEndian(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendU32LittleEndian(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

[[nodiscard]] std::vector<std::byte> BuildWav16(const std::vector<std::int16_t>& samples,
                                                   const std::uint16_t channels = 1U,
                                                   const std::uint32_t sampleRate = 48000U) {
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<std::byte> bytes{std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'}};
    AppendU32LittleEndian(bytes, 36U + dataSize);
    bytes.insert(bytes.end(), {std::byte{'W'}, std::byte{'A'}, std::byte{'V'}, std::byte{'E'},
                               std::byte{'f'}, std::byte{'m'}, std::byte{'t'}, std::byte{' '}});
    AppendU32LittleEndian(bytes, 16U);
    AppendU16LittleEndian(bytes, 1U);
    AppendU16LittleEndian(bytes, channels);
    AppendU32LittleEndian(bytes, sampleRate);
    AppendU32LittleEndian(bytes, sampleRate * channels * sizeof(std::int16_t));
    AppendU16LittleEndian(bytes, static_cast<std::uint16_t>(channels * sizeof(std::int16_t)));
    AppendU16LittleEndian(bytes, 16U);
    bytes.insert(bytes.end(), {std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}});
    AppendU32LittleEndian(bytes, dataSize);
    for (const std::int16_t sample : samples) {
        AppendU16LittleEndian(bytes, static_cast<std::uint16_t>(sample));
    }
    return bytes;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
}

void RemoveFiles(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) std::filesystem::remove(path);
}

TEST(WavImporterTest, Import_ValidPcm16PublishesUveAudioAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_wav_importer_tests_triangle.wav";
    const std::filesystem::path destinationPath = "uve_wav_importer_tests_triangle.uvaudio";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, BuildWav16({-32768, 0, 16384, 32767}));

    Asset::AssetImporter importer;
    RegisterWavImporter(importer);
    Asset::AssetDatabase database;
    const auto classification = importer.ClassifySource(sourcePath);
    EXPECT_EQ(classification.kind, Asset::AssetImportSourceKind::RawAudio);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    EXPECT_EQ(classification.diagnostic, "format-specific parser is registered");
    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destinationPath);
    Asset::AudioAsset audio;
    ASSERT_TRUE(Asset::LoadAudioAsset(destinationPath, audio));
    EXPECT_EQ(audio.channels, 1U);
    EXPECT_EQ(audio.sampleRate, 48000U);
    ASSERT_EQ(audio.samples.size(), 4U);
    EXPECT_FLOAT_EQ(audio.samples[0], -1.0F);
    EXPECT_FLOAT_EQ(audio.samples[1], 0.0F);
    EXPECT_FLOAT_EQ(audio.samples[2], 0.5F);
    EXPECT_NEAR(audio.samples[3], 0.9999695F, 1.0e-6F);
    RemoveFiles({sourcePath, destinationPath});
}

TEST(WavImporterTest, Import_InvalidWavPreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_wav_importer_tests_invalid.wav";
    const std::filesystem::path destinationPath = "uve_wav_importer_tests_existing.uvaudio";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, {std::byte{'R'}, std::byte{'I'}, std::byte{'F'}});
    Asset::AudioAsset original{2U, 44100U, {-0.5F, 0.5F}};
    ASSERT_TRUE(Asset::SaveAudioAsset(original, destinationPath));

    Asset::AssetImporter importer;
    RegisterWavImporter(importer);
    Asset::AssetDatabase database;
    const Asset::AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, Asset::kInvalidAssetGuid);
    EXPECT_TRUE(database.Resolve(guid).empty());
    Asset::AudioAsset retained;
    ASSERT_TRUE(Asset::LoadAudioAsset(destinationPath, retained));
    EXPECT_EQ(retained.channels, original.channels);
    EXPECT_EQ(retained.sampleRate, original.sampleRate);
    EXPECT_EQ(retained.samples, original.samples);
    RemoveFiles({sourcePath, destinationPath});
}

TEST(WavImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_wav_importer_tests_wrong_destination.wav";
    const std::filesystem::path destinationPath = "uve_wav_importer_tests_wrong_destination.uvtex";
    RemoveFiles({sourcePath, destinationPath});
    WriteBytes(sourcePath, BuildWav16({0, 1}));

    Asset::AssetImporter importer;
    RegisterWavImporter(importer);
    Asset::AssetDatabase database;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, database), Asset::kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    RemoveFiles({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Audio::Tests
