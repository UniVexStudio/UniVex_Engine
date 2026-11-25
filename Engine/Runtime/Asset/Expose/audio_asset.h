// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace UVE::Asset {

inline constexpr std::size_t kMaximumAudioAssetSamples = 1U << 24U;

/// Schema version of the `.uvaudio` payload that follows the universal envelope header. Exposed
/// because a streaming reader parses the same header from disk without going through the
/// whole-file loader, and the version it accepts must be the version that loader writes.
inline constexpr std::uint32_t kAudioAssetPayloadVersion = 1U;

/// Typed CPU audio envelope for bounded interleaved normalized floating-point PCM samples. Runtime
/// devices, streaming cursors, mixer routing, compression, and platform codec ownership remain
/// outside this asset contract.
struct AudioAsset final {
    std::uint16_t channels = 0U;
    std::uint32_t sampleRate = 0U;
    std::vector<float> samples;
};

/// Loads a `.uvaudio` envelope as a validated AudioAsset, publishing output only after all
/// metadata, sample-count, finite-value, and payload checks succeed.
[[nodiscard]] bool LoadAudioAsset(const std::filesystem::path& path, AudioAsset& outAudio);

/// Saves a validated AudioAsset as an AssetKind::Audio envelope. Invalid metadata, non-finite
/// samples, mismatched interleaving, and oversized sample arrays are rejected before writing.
[[nodiscard]] bool SaveAudioAsset(const AudioAsset& audio, const std::filesystem::path& path);

} // namespace UVE::Asset
