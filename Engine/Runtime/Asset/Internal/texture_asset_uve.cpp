// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_asset_uve.h"

#include <cstdint>
#include <cstring>
#include <limits>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Asset {

std::uint32_t BytesPerPixelUVE(TextureFormatUVE format) noexcept {
    switch (format) {
        case TextureFormatUVE::RGBA8Unorm:
            return 4;
        case TextureFormatUVE::RGBA16Float:
            return 8;
    }
    return 0; // unreachable for a valid enumerator; keeps this function total.
}

bool LoadTextureAssetUVE(const std::filesystem::path& path, TextureAssetUVE& outTexture) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Texture) {
        UVE_ERROR("TextureAssetUVE: \"{}\" is not a texture file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payload = file->second;
    std::size_t offset = 0;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t formatValue = 0;
    std::uint64_t pixelByteCount = 0;
    if (!Utilities::ReadUint32FromBufferUVE(payload, offset, width) || !Utilities::ReadUint32FromBufferUVE(payload, offset, height) ||
        !Utilities::ReadUint32FromBufferUVE(payload, offset, formatValue) ||
        !Utilities::ReadUint64FromBufferUVE(payload, offset, pixelByteCount)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has a truncated header", path.string());
        return false;
    }
    if (formatValue > static_cast<std::uint32_t>(TextureFormatUVE::RGBA16Float)) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has an unknown texture format {}", path.string(), formatValue);
        return false;
    }
    const auto format = static_cast<TextureFormatUVE>(formatValue);

    if (offset > payload.size() || pixelByteCount > payload.size() - offset) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has truncated pixel data", path.string());
        return false;
    }

    const std::uint64_t bytesPerPixel = BytesPerPixelUVE(format);
    if (width != 0U && height > std::numeric_limits<std::uint64_t>::max() / width) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has overflowing texture dimensions", path.string());
        return false;
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has overflowing expected pixel bytes", path.string());
        return false;
    }
    const std::uint64_t expectedByteCount = pixelCount * bytesPerPixel;
    if (pixelByteCount != expectedByteCount) {
        UVE_ERROR("TextureAssetUVE: \"{}\" has {} pixel bytes, expected {} for a {}x{} texture", path.string(),
                   pixelByteCount, expectedByteCount, width, height);
        return false;
    }

    std::vector<std::byte> pixels(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                   payload.begin() + static_cast<std::ptrdiff_t>(offset + pixelByteCount));

    outTexture.width = width;
    outTexture.height = height;
    outTexture.format = format;
    outTexture.pixels = std::move(pixels);
    return true;
}

bool SaveTextureAssetUVE(const TextureAssetUVE& texture, const std::filesystem::path& path) {
    const std::uint32_t bytesPerPixel = BytesPerPixelUVE(texture.format);
    if (bytesPerPixel == 0U) {
        UVE_ERROR("TextureAssetUVE: rejected invalid texture format before writing {}", path.string());
        return false;
    }
    if (texture.width != 0U && texture.height > std::numeric_limits<std::uint64_t>::max() / texture.width) {
        UVE_ERROR("TextureAssetUVE: rejected overflowing dimensions before writing {}", path.string());
        return false;
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(texture.width) * texture.height;
    if (pixelCount > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel ||
        pixelCount * bytesPerPixel != texture.pixels.size()) {
        UVE_ERROR("TextureAssetUVE: rejected pixel byte count before writing {}", path.string());
        return false;
    }

    std::vector<std::byte> payload;
    Utilities::AppendUint32UVE(payload, texture.width);
    Utilities::AppendUint32UVE(payload, texture.height);
    Utilities::AppendUint32UVE(payload, static_cast<std::uint32_t>(texture.format));
    Utilities::AppendUint64UVE(payload, static_cast<std::uint64_t>(texture.pixels.size()));
    Utilities::AppendBytesUVE(payload, texture.pixels.data(), texture.pixels.size());

    return WriteUveFileUVE(path, AssetKindUVE::Texture, payload);
}

} // namespace UVE::Asset
