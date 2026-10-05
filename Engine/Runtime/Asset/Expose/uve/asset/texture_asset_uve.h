// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace UVE::Asset {

/// Pixel formats a loadable TextureAssetUVE can use. This stays separate from
/// `Render::TextureFormatUVE`: the renderer translates asset formats to the RHI without creating
/// an Asset-to-RHI dependency cycle. Depth formats remain GPU render-target resources, not
/// imported texture-asset formats.
enum class TextureAssetFormatUVE : std::uint8_t {
    RGBA8Unorm = 0,
    RGBA16Float = 1,
};

/// Interpretation of color channels when a texture is sampled. The renderer maps this metadata to
/// the RHI's sampled color-space view; it does not change the stored pixel bytes.
enum class TextureAssetColorSpaceUVE : std::uint8_t {
    Linear = 0,
    Srgb = 1,
};

/// Broad intended role used by import/cook policy. It is deliberately independent from the
/// material slot so a texture can be validated or cooked before a material references it.
enum class TextureUsageUVE : std::uint8_t {
    Generic = 0,
    Color = 1,
    Normal = 2,
    Data = 3,
    Hdr = 4,
};

/// How texture bytes are stored in the asset payload. BasisUniversalKtx2 keeps one portable
/// KTX2/Basis bitstream; the renderer transcodes it to a device-supported GPU format at load time.
enum class TexturePayloadEncodingUVE : std::uint8_t {
    RawPixels = 0,
    BasisUniversalKtx2 = 1,
};

/// One additional level after level 0. Its dimensions are stored explicitly and validated against
/// the conventional max(1, previousDimension / 2) mip progression.
struct TextureMipLevelUVE {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> pixels;

    [[nodiscard]] bool operator==(const TextureMipLevelUVE&) const = default;
};

/// The CPU-side, engine-native representation of a `.uvtex` asset. Raw assets store level 0 in
/// `pixels` and levels 1..N in `mipLevels`. Basis assets store a portable KTX2 bitstream in
/// `basisKtx2Data` instead. Texture arrays, cubemaps, and volumes remain separate later increments.
struct TextureAssetUVE {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureAssetFormatUVE format = TextureAssetFormatUVE::RGBA8Unorm;
    std::vector<std::byte> pixels;
    // Appended after the legacy members so existing aggregate initialization of the first four
    // fields remains source-compatible.
    TextureAssetColorSpaceUVE colorSpace = TextureAssetColorSpaceUVE::Linear;
    TextureUsageUVE usage = TextureUsageUVE::Generic;
    // Additional mip levels only; level 0 remains in the legacy `pixels` member above.
    std::vector<TextureMipLevelUVE> mipLevels;
    // Appended to preserve aggregate initialization of the uncompressed/mipmap asset layout.
    TexturePayloadEncodingUVE payloadEncoding = TexturePayloadEncodingUVE::RawPixels;
    // Populated only for BasisUniversalKtx2. It contains a complete portable KTX2 file including
    // all encoded mip levels; `format` remains the logical decoded pixel format (RGBA8Unorm).
    std::vector<std::byte> basisKtx2Data;
};

/// The exact byte size of one pixel in `format` (4 for RGBA8Unorm, 8 for RGBA16Float). Returns
/// zero for an unsupported enumerator.
[[nodiscard]] std::uint32_t BytesPerPixelUVE(TextureAssetFormatUVE format) noexcept;

/// Validates the format/color-space/usage combination without inspecting dimensions or pixel bytes.
[[nodiscard]] bool IsTextureAssetMetadataValidUVE(const TextureAssetUVE& texture) noexcept;

/// Validates dimensions, metadata, format, the exact level-0 pixel byte count, and the dimensions/
/// bytes of every additional mip level.
[[nodiscard]] bool IsTextureAssetValidUVE(const TextureAssetUVE& texture) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::Texture`, filling `outTexture`. Both the
/// original metadata-free payload and the current versioned payload are accepted. Legacy payloads
/// receive the neutral `Linear`/`Generic` metadata defaults.
[[nodiscard]] bool LoadTextureAssetUVE(const std::filesystem::path& path, TextureAssetUVE& outTexture);

/// Writes `texture` to `path` as a versioned `.uve*` texture payload. Invalid descriptors are
/// rejected before opening the destination; publication failures are reported and return false.
[[nodiscard]] bool SaveTextureAssetUVE(const TextureAssetUVE& texture, const std::filesystem::path& path);

} // namespace UVE::Asset
