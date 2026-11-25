// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "wav_importer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <utility>
#include <vector>

#include "audio_asset.h"
#include "wav_metadata.h"
#include "wav_pcm16_decoder.h"
#include "logging_macros.h"

namespace UVE::Audio {
namespace {

constexpr std::uint64_t kMaximumWavImporterSourceBytes = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] bool ReadWavSourceBytes(const std::filesystem::path& sourcePath,
                                         std::vector<std::byte>& outBytes) {
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        UVE_ERROR("WavImporter: failed to open source \"{}\"", sourcePath.string());
        return false;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kMaximumWavImporterSourceBytes ||
        static_cast<std::uint64_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("WavImporter: source \"{}\" exceeds the bounded source-size limit", sourcePath.string());
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            UVE_ERROR("WavImporter: source \"{}\" could not be read completely", sourcePath.string());
            return false;
        }
    }
    outBytes = std::move(bytes);
    return true;
}

[[nodiscard]] bool SaveAudioAssetAtomically(const Asset::AudioAsset& audio,
                                               const std::filesystem::path& destinationPath) {
    std::error_code errorCode;
    if (const std::filesystem::path parent = destinationPath.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode) {
            UVE_ERROR("WavImporter: failed to create destination directory \"{}\": {}", parent.string(),
                      errorCode.message());
            return false;
        }
    }
    const std::filesystem::path temporaryPath = destinationPath.string() + ".uve_wav_tmp";
    std::filesystem::remove(temporaryPath, errorCode);
    if (!Asset::SaveAudioAsset(audio, temporaryPath)) {
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, destinationPath, errorCode);
    if (errorCode) {
        UVE_ERROR("WavImporter: failed to publish destination \"{}\": {}", destinationPath.string(),
                  errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

[[nodiscard]] bool ImportWavSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const Asset::AssetImportSettings& /*settings*/) {
    try {
        if (destinationPath.extension() != ".uvaudio") {
            UVE_ERROR("WavImporter: destination \"{}\" must use the .uvaudio extension",
                      destinationPath.string());
            return false;
        }
        std::vector<std::byte> sourceBytes;
        if (!ReadWavSourceBytes(sourcePath, sourceBytes)) return false;
        const auto metadata = ParseWavMetadata(sourceBytes);
        if (!metadata.has_value()) {
            UVE_ERROR("WavImporter: source \"{}\" failed bounded PCM WAV metadata parsing", sourcePath.string());
            return false;
        }
        std::vector<float> samples;
        if (!DecodeWavPcm16Samples(sourceBytes, samples)) {
            UVE_ERROR("WavImporter: source \"{}\" failed bounded PCM16 sample decoding", sourcePath.string());
            return false;
        }
        if (metadata->channels == 0U || metadata->dataBytes / sizeof(std::int16_t) != samples.size()) {
            UVE_ERROR("WavImporter: source \"{}\" published inconsistent PCM16 sample facts",
                      sourcePath.string());
            return false;
        }
        Asset::AudioAsset audio;
        audio.channels = metadata->channels;
        audio.sampleRate = metadata->sampleRate;
        audio.samples = std::move(samples);
        return SaveAudioAssetAtomically(audio, destinationPath);
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterWavImporter(Asset::IAssetImporter& importer) {
    importer.RegisterImporter("wav", &ImportWavSource);
}

} // namespace UVE::Audio
