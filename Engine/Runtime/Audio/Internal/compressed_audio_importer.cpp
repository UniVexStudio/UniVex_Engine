// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "compressed_audio_importer.h"
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
#include "logging_macros.h"
#include "scope_guard.h"

namespace UVE::Audio {
namespace {
ma_encoding_format NativeFormat(const EncodedAudioFormat format) noexcept {
    switch (format) {
    case EncodedAudioFormat::Flac:
        return ma_encoding_format_flac;
    case EncodedAudioFormat::Mp3:
        return ma_encoding_format_mp3;
    case EncodedAudioFormat::Vorbis:
        return ma_encoding_format_vorbis;
    }
    return ma_encoding_format_unknown;
}

bool ImportEncoded(const std::filesystem::path &source, const std::filesystem::path &destination,
                      const EncodedAudioFormat format) {
    try {
        if (destination.extension() != ".uvaudio")
            return false;
        std::ifstream input(source, std::ios::binary | std::ios::ate);
        if (!input)
            return false;
        const auto size = input.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > kMaximumEncodedAudioSourceBytes)
            return false;
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input)
            return false;
        Asset::AudioAsset audio;
        if (!DecodeEncodedAudio(bytes, format, audio))
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
        Utilities::ScopeGuard cleanup{[&] {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            if (cleanupError)
                UVE_WARNING("Audio importer: cannot remove temporary {}: {}", temporary.string(),
                            cleanupError.message());
        }};
        if (!Asset::SaveAudioAsset(audio, temporary))
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

bool DecodeEncodedAudio(const std::span<const std::byte> bytes, const EncodedAudioFormat format,
                           Asset::AudioAsset &outAudio) {
    if (bytes.empty() || bytes.size() > kMaximumEncodedAudioSourceBytes)
        return false;
    const auto encoding = NativeFormat(format);
    if (encoding == ma_encoding_format_unknown)
        return false;
    try {
        ma_decoder decoder{};
        auto config = ma_decoder_config_init(ma_format_f32, 0U, 0U);
        config.encodingFormat = encoding;
        if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS)
            return false;
        Utilities::ScopeGuard cleanup{[&] { ma_decoder_uninit(&decoder); }};
        ma_format decodedFormat{};
        ma_uint32 channels = 0U, sampleRate = 0U;
        if (ma_decoder_get_data_format(&decoder, &decodedFormat, &channels, &sampleRate, nullptr, 0U) !=
                MA_SUCCESS ||
            decodedFormat != ma_format_f32 || channels == 0U || channels > 32U || sampleRate == 0U)
            return false;
        const ma_uint64 frameLimit = Asset::kMaximumAudioAssetSamples / channels;
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
        Asset::AudioAsset candidate;
        candidate.channels = static_cast<std::uint16_t>(channels);
        candidate.sampleRate = sampleRate;
        candidate.samples = std::move(samples);
        outAudio = std::move(candidate);
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}
void RegisterCompressedAudioImporters(Asset::IAssetImporter &importer) {
    importer.RegisterImporter("flac", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncoded(source, destination, EncodedAudioFormat::Flac);
    });
    importer.RegisterImporter("mp3", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncoded(source, destination, EncodedAudioFormat::Mp3);
    });
    importer.RegisterImporter("ogg", [](const auto &source, const auto &destination, const auto &) {
        return ImportEncoded(source, destination, EncodedAudioFormat::Vorbis);
    });
}
} // namespace UVE::Audio
