// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace UVE::Asset {

inline constexpr std::size_t kMaximumAudioAssetSamplesUVE = 1U << 20U;

/// Schema version of the `.uvaudio` payload that follows the universal envelope header. Exposed
/// because a streaming reader parses the same header from disk without going through the
/// whole-file loader, and the version it accepts must be the version that loader writes.
inline constexpr std::uint32_t kAudioAssetPayloadVersionUVE = 1U;

/// Typed CPU audio envelope for bounded interleaved normalized floating-point PCM samples. Runtime
/// devices, streaming cursors, mixer routing, compression, and platform codec ownership remain
/// outside this asset contract.
struct AudioAssetUVE final {
    std::uint16_t channels = 0U;
    std::uint32_t sampleRate = 0U;
    std::vector<float> samples;
};

/// Loads a `.uvaudio` envelope as a validated AudioAssetUVE, publishing output only after all
/// metadata, sample-count, finite-value, and payload checks succeed.
[[nodiscard]] bool LoadAudioAssetUVE(const std::filesystem::path& path, AudioAssetUVE& outAudio);

/// Saves a validated AudioAssetUVE as an AssetKindUVE::Audio envelope. Invalid metadata, non-finite
/// samples, mismatched interleaving, and oversized sample arrays are rejected before writing.
[[nodiscard]] bool SaveAudioAssetUVE(const AudioAssetUVE& audio, const std::filesystem::path& path);

} // namespace UVE::Asset
