// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_asset.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Asset {
namespace {

constexpr std::uint32_t kAudioAssetVersion = kAudioAssetPayloadVersion;

[[nodiscard]] bool IsValidAudio(const std::uint16_t channels, const std::uint32_t sampleRate,
                                   const std::vector<float>& samples) noexcept {
    if (channels == 0U || sampleRate == 0U || samples.empty() || samples.size() > kMaximumAudioAssetSamples ||
        samples.size() % channels != 0U) {
        return false;
    }
    for (const float sample : samples) {
        if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) return false;
    }
    return true;
}

} // namespace

bool LoadAudioAsset(const std::filesystem::path& path, AudioAsset& outAudio) {
    const auto file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::Audio) {
        if (file.has_value()) {
            UVE_ERROR("AudioAsset: \"{}\" is not an audio file", path.string());
        }
        return false;
    }
    const auto& payload = file->second;
    std::size_t offset = 0U;
    std::uint32_t version = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t sampleRate = 0U;
    std::uint64_t sampleCount = 0U;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, version) ||
        !Utilities::ReadUint16LeFromBuffer(payload, offset, channels) ||
        !Utilities::ReadUint32LeFromBuffer(payload, offset, sampleRate) ||
        !Utilities::ReadUint64LeFromBuffer(payload, offset, sampleCount) ||
        version != kAudioAssetVersion || sampleCount == 0U || sampleCount > kMaximumAudioAssetSamples ||
        sampleCount > std::numeric_limits<std::size_t>::max() ||
        sampleCount > (payload.size() - std::min(offset, payload.size())) / sizeof(float)) {
        UVE_ERROR("AudioAsset: \"{}\" has an invalid or truncated payload header", path.string());
        return false;
    }
    // Samples are stored little-endian per sample (not as one host-order bulk copy), so the
    // format is fully portable. Byte-identical to the old bulk read on little-endian targets.
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    bool samplesOk = true;
    for (float& sample : samples) {
        if (!Utilities::ReadFloatLeFromBuffer(payload, offset, sample)) {
            samplesOk = false;
            break;
        }
    }
    if (!samplesOk || offset != payload.size() || !IsValidAudio(channels, sampleRate, samples)) {
        UVE_ERROR("AudioAsset: \"{}\" has invalid sample payload data", path.string());
        return false;
    }
    AudioAsset candidate;
    candidate.channels = channels;
    candidate.sampleRate = sampleRate;
    candidate.samples = std::move(samples);
    outAudio = std::move(candidate);
    return true;
}

bool SaveAudioAsset(const AudioAsset& audio, const std::filesystem::path& path) {
    if (!IsValidAudio(audio.channels, audio.sampleRate, audio.samples)) {
        UVE_ERROR("AudioAsset: refusing to save invalid audio asset \"{}\"", path.string());
        return false;
    }
    std::vector<std::byte> payload;
    Utilities::AppendUint32Le(payload, kAudioAssetVersion);
    Utilities::AppendUint16Le(payload, audio.channels);
    Utilities::AppendUint32Le(payload, audio.sampleRate);
    Utilities::AppendUint64Le(payload, static_cast<std::uint64_t>(audio.samples.size()));
    for (const float sample : audio.samples) {
        Utilities::AppendFloatLe(payload, sample);
    }
    return WriteUveFile(path, AssetKind::Audio, payload);
}

} // namespace UVE::Asset
