// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_import_settings_uve.h"

namespace UVE::Asset {

/// Native output format requested from the Basis Universal transcoder. These names describe
/// block layouts, not graphics API enums; RenderSystems maps them to a supported RHI format.
enum class TextureTranscodeTargetUVE : std::uint8_t {
    Rgba8Unorm = 0,
    Bc1Rgb = 1,
    Bc3Rgba = 2,
    Bc7Rgba = 3,
    Etc2Rgb = 4,
    Etc2Rgba = 5,
    Astc4x4Rgba = 6,
};

/// Validated metadata read from a portable Basis/KTX2 asset. mipLevels includes level 0.
struct TextureCompressionInfoUVE {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t mipLevels = 0U;
    bool hasAlpha = false;
};

/// A tightly concatenated, device-native upload produced from a portable Basis texture.
/// `pixels` stores every mip level in order and uses `target`'s block layout (or RGBA8 texels).
struct TextureTranscodedMipChainUVE {
    TextureTranscodeTargetUVE target = TextureTranscodeTargetUVE::Rgba8Unorm;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t mipLevels = 0U;
    std::vector<std::byte> pixels;
};

/// Compresses an already-decoded RGBA8 asset and its generated mip chain into an in-memory
/// portable Basis Universal KTX2 payload. `None` is a successful no-op. Failure leaves `texture`
/// unchanged; non-RGBA8, invalid quality/effort, and unsupported builds are rejected.
[[nodiscard]] bool CompressTextureAssetWithBasisUVE(TextureAssetUVE& texture,
                                                     TextureCompressionModeUVE mode,
                                                     std::uint8_t quality,
                                                     std::uint8_t effort);

/// Initializes the Basis transcoder and validates its view of an encoded asset. Returns false for
/// raw assets, malformed KTX2 data, unsupported texture types, or inconsistent asset metadata.
[[nodiscard]] bool GetTextureCompressionInfoUVE(const TextureAssetUVE& texture,
                                                 TextureCompressionInfoUVE& outInfo);

/// Transcodes all levels of a portable Basis/KTX2 asset to the requested GPU block/pixel layout.
/// The output is unchanged on failure. The caller must first check target-device support.
[[nodiscard]] bool TranscodeTextureAssetUVE(const TextureAssetUVE& texture,
                                             TextureTranscodeTargetUVE target,
                                             TextureTranscodedMipChainUVE& outMipChain);

} // namespace UVE::Asset
