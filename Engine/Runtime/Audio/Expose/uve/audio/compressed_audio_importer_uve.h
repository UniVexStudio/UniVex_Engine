// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/i_asset_importer_uve.h"

namespace UVE::Audio {
enum class EncodedAudioFormatUVE : std::uint8_t { Flac = 0, Mp3, Vorbis };
inline constexpr std::size_t kMaximumEncodedAudioSourceBytesUVE = 64U * 1024U * 1024U;
/// Decode using the already pinned miniaudio dependency (including its bundled stb_vorbis).
/// No device is opened. Explicit format selection prevents extension/content mismatch fallback.
/// Output is published only after validation: <=64 MiB source, 1..32 channels, nonzero rate,
/// <=kMaximumAudioAssetSamplesUVE finite samples. Lossy overshoot is clipped to normalized PCM.
/// Truncated known-length streams, malformed/empty input and unsupported enum values fail with
/// outAudio unchanged. Metadata with unknown duration is decoded with a hard output-frame cap.
[[nodiscard]] bool DecodeEncodedAudioUVE(std::span<const std::byte> bytes, EncodedAudioFormatUVE format,
                                         Asset::AudioAssetUVE &outAudio);
/// Registers flac/mp3/ogg -> the existing .uvaudio envelope. Existing WAV behavior is unchanged.
/// Synchronous like other importers; callers serialize writes to the same destination.
/// A failed read/decode/publication preserves any existing destination and cleans its temporary.
void RegisterCompressedAudioImportersUVE(Asset::IAssetImporterUVE &importer);
} // namespace UVE::Audio
