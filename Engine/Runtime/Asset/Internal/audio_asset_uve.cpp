// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/audio_asset_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Asset {
namespace {

constexpr std::uint32_t kAudioAssetVersionUVE = 1U;

[[nodiscard]] bool IsValidAudioUVE(const std::uint16_t channels, const std::uint32_t sampleRate,
                                   const std::vector<float>& samples) noexcept {
    if (channels == 0U || sampleRate == 0U || samples.empty() || samples.size() > kMaximumAudioAssetSamplesUVE ||
        samples.size() % channels != 0U) {
        return false;
    }
    for (const float sample : samples) {
        if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) return false;
    }
    return true;
}

} // namespace

bool LoadAudioAssetUVE(const std::filesystem::path& path, AudioAssetUVE& outAudio) {
    const auto file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::Audio) {
        if (file.has_value()) {
            UVE_ERROR("AudioAssetUVE: \"{}\" is not an audio file", path.string());
        }
        return false;
    }
    const auto& payload = file->second;
    std::size_t offset = 0U;
    std::uint32_t version = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t sampleRate = 0U;
    std::uint64_t sampleCount = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, version) ||
        !Utilities::ReadUint16LeFromBufferUVE(payload, offset, channels) ||
        !Utilities::ReadUint32LeFromBufferUVE(payload, offset, sampleRate) ||
        !Utilities::ReadUint64LeFromBufferUVE(payload, offset, sampleCount) ||
        version != kAudioAssetVersionUVE || sampleCount == 0U || sampleCount > kMaximumAudioAssetSamplesUVE ||
        sampleCount > std::numeric_limits<std::size_t>::max() ||
        sampleCount > (payload.size() - std::min(offset, payload.size())) / sizeof(float)) {
        UVE_ERROR("AudioAssetUVE: \"{}\" has an invalid or truncated payload header", path.string());
        return false;
    }
    // Samples are stored little-endian per sample (not as one host-order bulk copy), so the
    // format is fully portable. Byte-identical to the old bulk read on little-endian targets.
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    bool samplesOk = true;
    for (float& sample : samples) {
        if (!Utilities::ReadFloatLeFromBufferUVE(payload, offset, sample)) {
            samplesOk = false;
            break;
        }
    }
    if (!samplesOk || offset != payload.size() || !IsValidAudioUVE(channels, sampleRate, samples)) {
        UVE_ERROR("AudioAssetUVE: \"{}\" has invalid sample payload data", path.string());
        return false;
    }
    AudioAssetUVE candidate;
    candidate.channels = channels;
    candidate.sampleRate = sampleRate;
    candidate.samples = std::move(samples);
    outAudio = std::move(candidate);
    return true;
}

bool SaveAudioAssetUVE(const AudioAssetUVE& audio, const std::filesystem::path& path) {
    if (!IsValidAudioUVE(audio.channels, audio.sampleRate, audio.samples)) {
        UVE_ERROR("AudioAssetUVE: refusing to save invalid audio asset \"{}\"", path.string());
        return false;
    }
    std::vector<std::byte> payload;
    Utilities::AppendUint32LeUVE(payload, kAudioAssetVersionUVE);
    Utilities::AppendUint16LeUVE(payload, audio.channels);
    Utilities::AppendUint32LeUVE(payload, audio.sampleRate);
    Utilities::AppendUint64LeUVE(payload, static_cast<std::uint64_t>(audio.samples.size()));
    for (const float sample : audio.samples) {
        Utilities::AppendFloatLeUVE(payload, sample);
    }
    return WriteUveFileUVE(path, AssetKindUVE::Audio, payload);
}

} // namespace UVE::Asset
