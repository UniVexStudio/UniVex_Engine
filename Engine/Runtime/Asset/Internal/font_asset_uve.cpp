// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/font_asset_uve.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Asset {
namespace {

/// `.uvfont` payload version. The payload is [u32 version][u32 name bytes][name][u32 data
/// bytes][data], all integers little-endian via the binary-buffer codec.
inline constexpr std::uint32_t kFontPayloadVersionUVE = 1U;

[[nodiscard]] bool HasSfntMagicUVE(const std::vector<std::byte>& bytes) noexcept {
    if (bytes.size() < 4U) {
        return false;
    }
    const auto tag = [&bytes](const std::size_t i) noexcept {
        return std::to_integer<unsigned char>(bytes[i]);
    };
    const std::uint32_t magic = (static_cast<std::uint32_t>(tag(0U)) << 24U) |
                                (static_cast<std::uint32_t>(tag(1U)) << 16U) |
                                (static_cast<std::uint32_t>(tag(2U)) << 8U) |
                                static_cast<std::uint32_t>(tag(3U));
    // 0x00010000 (TrueType), 'OTTO' (CFF OpenType), 'true'/'typ1' (legacy Apple tags).
    return magic == 0x00010000U || magic == 0x4F54544FU || magic == 0x74727565U || magic == 0x74797031U;
}

} // namespace

bool IsFontAssetValidUVE(const FontAssetUVE& font) noexcept {
    return font.familyName.size() <= kMaximumFontFamilyNameBytesUVE && !font.fontData.empty() &&
           font.fontData.size() <= kMaximumFontAssetBytesUVE && HasSfntMagicUVE(font.fontData);
}

bool LoadFontAssetUVE(const std::filesystem::path& path, FontAssetUVE& outFont) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Font) {
        UVE_ERROR("FontAssetUVE: \"{}\" is not a font file (asset type {})", path.string(),
                  static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }
    const std::vector<std::byte>& payload = file->second;
    std::size_t offset = 0U;
    std::uint32_t version = 0U;
    std::uint32_t nameLength = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, version) ||
        !Utilities::ReadUint32LeFromBufferUVE(payload, offset, nameLength)) {
        UVE_ERROR("FontAssetUVE: \"{}\" has a truncated font header", path.string());
        return false;
    }
    if (version != kFontPayloadVersionUVE) {
        UVE_ERROR("FontAssetUVE: \"{}\" has an unknown payload version {}", path.string(), version);
        return false;
    }
    if (nameLength > kMaximumFontFamilyNameBytesUVE || payload.size() - offset < nameLength) {
        UVE_ERROR("FontAssetUVE: \"{}\" declares an impossible family-name length", path.string());
        return false;
    }
    FontAssetUVE font;
    font.familyName.assign(reinterpret_cast<const char*>(payload.data()) + offset, nameLength);
    offset += nameLength;
    std::uint32_t dataLength = 0U;
    if (!Utilities::ReadUint32LeFromBufferUVE(payload, offset, dataLength)) {
        UVE_ERROR("FontAssetUVE: \"{}\" has a truncated font-data header", path.string());
        return false;
    }
    // Exact fit: trailing bytes are corruption, not a newer version (versions bump the header).
    if (dataLength > kMaximumFontAssetBytesUVE || payload.size() - offset != dataLength) {
        UVE_ERROR("FontAssetUVE: \"{}\" declares an impossible font-data length", path.string());
        return false;
    }
    font.fontData.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
    if (!IsFontAssetValidUVE(font)) {
        UVE_ERROR("FontAssetUVE: \"{}\" holds unrecognised font bytes", path.string());
        return false;
    }
    outFont = std::move(font);
    return true;
}

bool SaveFontAssetUVE(const FontAssetUVE& font, const std::filesystem::path& path) {
    if (!IsFontAssetValidUVE(font)) {
        UVE_ERROR("FontAssetUVE: refusing to save an invalid font to {}", path.string());
        return false;
    }
    std::vector<std::byte> payload;
    Utilities::AppendUint32LeUVE(payload, kFontPayloadVersionUVE);
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(font.familyName.size()));
    const auto* const nameBytes = reinterpret_cast<const std::byte*>(font.familyName.data());
    payload.insert(payload.end(), nameBytes, nameBytes + font.familyName.size());
    Utilities::AppendUint32LeUVE(payload, static_cast<std::uint32_t>(font.fontData.size()));
    payload.insert(payload.end(), font.fontData.begin(), font.fontData.end());
    return WriteUveFileUVE(path, AssetKindUVE::Font, payload);
}

} // namespace UVE::Asset
