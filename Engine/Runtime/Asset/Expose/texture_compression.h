// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "texture_asset.h"
#include "texture_import_settings.h"

namespace UVE::Asset {

/// Native output format requested from the Basis Universal transcoder. These names describe
/// block layouts, not graphics API enums; RenderSystems maps them to a supported RHI format.
enum class TextureTranscodeTarget : std::uint8_t {
    Rgba8Unorm = 0,
    Bc1Rgb = 1,
    Bc3Rgba = 2,
    Bc7Rgba = 3,
    Etc2Rgb = 4,
    Etc2Rgba = 5,
    Astc4x4Rgba = 6,
};

/// Validated metadata read from a portable Basis/KTX2 asset. mipLevels includes level 0.
struct TextureCompressionInfo {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t mipLevels = 0U;
    bool hasAlpha = false;
};

/// A tightly concatenated, device-native upload produced from a portable Basis texture.
/// `pixels` stores every mip level in order and uses `target`'s block layout (or RGBA8 texels).
struct TextureTranscodedMipChain {
    TextureTranscodeTarget target = TextureTranscodeTarget::Rgba8Unorm;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t mipLevels = 0U;
    std::vector<std::byte> pixels;
};

/// Compresses an already-decoded RGBA8 asset and its generated mip chain into an in-memory
/// portable Basis Universal KTX2 payload. `None` is a successful no-op. Failure leaves `texture`
/// unchanged; non-RGBA8, invalid quality/effort, and unsupported builds are rejected.
[[nodiscard]] bool CompressTextureAssetWithBasis(TextureAsset& texture,
                                                     TextureCompressionMode mode,
                                                     std::uint8_t quality,
                                                     std::uint8_t effort);

/// Initializes the Basis transcoder and validates its view of an encoded asset. Returns false for
/// raw assets, malformed KTX2 data, unsupported texture types, or inconsistent asset metadata.
[[nodiscard]] bool GetTextureCompressionInfo(const TextureAsset& texture,
                                                 TextureCompressionInfo& outInfo);

/// Transcodes all levels of a portable Basis/KTX2 asset to the requested GPU block/pixel layout.
/// The output is unchanged on failure. The caller must first check target-device support.
[[nodiscard]] bool TranscodeTextureAsset(const TextureAsset& texture,
                                             TextureTranscodeTarget target,
                                             TextureTranscodedMipChain& outMipChain);

} // namespace UVE::Asset
