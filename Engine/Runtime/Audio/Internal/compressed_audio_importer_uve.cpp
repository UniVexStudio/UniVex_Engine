// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/audio/compressed_audio_importer_uve.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <miniaudio.h>
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/scope_guard_uve.h"

namespace UVE::Audio {
namespace {
ma_encoding_format NativeFormatUVE(const EncodedAudioFormatUVE format) noexcept {
    switch (format) {
    case EncodedAudioFormatUVE::Flac:
        return ma_encoding_format_flac;
    case EncodedAudioFormatUVE::Mp3:
        return ma_encoding_format_mp3;
    case EncodedAudioFormatUVE::Vorbis:
        return ma_encoding_format_vorbis;
    }
    return ma_encoding_format_unknown;
}

bool ImportEncodedUVE(const std::filesystem::path &source, const std::filesystem::path &destination,
                      const EncodedAudioFormatUVE format) {
    try {
        if (destination.extension() != ".uvaudio")
            return false;
        std::ifstream input(source, std::ios::binary | std::ios::ate);
        if (!input)
            return false;
        const auto size = input.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > kMaximumEncodedAudioSourceBytesUVE)
            return false;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input)
            return false;
        Asset::AudioAssetUVE audio;
        if (!DecodeEncodedAudioUVE(bytes, format, audio))
            return false;
        std::error_code error;
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            return false;
        static std::atomic<std::uint64_t> sequence{0U};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path temporary = destination;
        temporary +=
            ".uve_audio_" + std::to_string(stamp) + "_" + std::to_string(sequence.fetch_add(1U)) + ".tmp";
        Utilities::ScopeGuardUVE cleanup{[&] {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            if (cleanupError)
                UVE_WARNING("Audio importer: cannot remove temporary {}: {}", temporary.string(),
                            cleanupError.message());
        }};
        if (!Asset::SaveAudioAssetUVE(audio, temporary))
            return false;
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            UVE_ERROR("Audio importer: cannot publish {}: {}", destination.string(), error.message());
            return false;
        }
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
} // namespace

bool DecodeEncodedAudioUVE(const std::span<const std::byte> bytes, const EncodedAudioFormatUVE format,
                           Asset::AudioAssetUVE &outAudio) {
    if (bytes.empty() || bytes.size() > kMaximumEncodedAudioSourceBytesUVE)
        return false;
    const auto encoding = NativeFormatUVE(format);
    if (encoding == ma_encoding_format_unknown)
        return false;
    try {
        ma_decoder decoder{};
        auto config = ma_decoder_config_init(ma_format_f32, 0U, 0U);
        config.encodingFormat = encoding;
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS)
            return false;
        Utilities::ScopeGuardUVE cleanup{[&] { ma_decoder_uninit(&decoder); }};
        ma_format decodedFormat{};
        ma_uint32 channels = 0U, sampleRate = 0U;
        if (ma_decoder_get_data_format(&decoder, &decodedFormat, &channels, &sampleRate, nullptr, 0U) !=
                MA_SUCCESS ||
            decodedFormat != ma_format_f32 || channels == 0U || channels > 32U || sampleRate == 0U)
            return false;
        const ma_uint64 frameLimit = Asset::kMaximumAudioAssetSamplesUVE / channels;
        ma_uint64 declaredFrames = 0U;
        const bool knownLength =
            ma_decoder_get_length_in_pcm_frames(&decoder, &declaredFrames) == MA_SUCCESS &&
            declaredFrames != 0U;
        if (knownLength && declaredFrames > frameLimit)
            return false;
        const ma_uint64 requestedFrames = (knownLength ? declaredFrames : frameLimit) + 1U;
        std::vector<float> samples(static_cast<std::size_t>(requestedFrames * channels));
        ma_uint64 actualFrames = 0U;
        const auto result =
            ma_decoder_read_pcm_frames(&decoder, samples.data(), requestedFrames, &actualFrames);
        if ((result != MA_SUCCESS && result != MA_AT_END) || actualFrames == 0U ||
            actualFrames > frameLimit || (knownLength && actualFrames != declaredFrames))
            return false;
        samples.resize(static_cast<std::size_t>(actualFrames * channels));
        for (float &sample : samples) {
            if (!std::isfinite(sample))
                return false;
            sample = std::clamp(sample, -1.0F, 1.0F);
        }
        Asset::AudioAssetUVE candidate;
        candidate.channels = static_cast<std::uint16_t>(channels);
        candidate.sampleRate = sampleRate;
        candidate.samples = std::move(samples);
        outAudio = std::move(candidate);
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
void RegisterCompressedAudioImportersUVE(Asset::IAssetImporterUVE &importer) {
    importer.RegisterImporterUVE("flac", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncodedUVE(source, destination, EncodedAudioFormatUVE::Flac);
    });
    importer.RegisterImporterUVE("mp3", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncodedUVE(source, destination, EncodedAudioFormatUVE::Mp3);
    });
    importer.RegisterImporterUVE("ogg", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncodedUVE(source, destination, EncodedAudioFormatUVE::Vorbis);
    });
}
} // namespace UVE::Audio
