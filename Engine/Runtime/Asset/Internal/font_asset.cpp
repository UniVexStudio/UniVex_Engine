// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "font_asset.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Asset {
namespace {

/// `.uvfont` payload version. The payload is [u32 version][u32 name bytes][name][u32 data
/// bytes][data], all integers little-endian via the binary-buffer codec.
inline constexpr std::uint32_t kFontPayloadVersion = 1U;

[[nodiscard]] bool HasSfntMagic(const std::vector<std::byte>& bytes) noexcept {
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

bool IsFontAssetValid(const FontAsset& font) noexcept {
    return font.familyName.size() <= kMaximumFontFamilyNameBytes && !font.fontData.empty() &&
           font.fontData.size() <= kMaximumFontAssetBytes && HasSfntMagic(font.fontData);
}

bool LoadFontAsset(const std::filesystem::path& path, FontAsset& outFont) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false; // ReadUveFile already logged the specific reason.
    }
    if (file->first.assetType != AssetKind::Font) {
        UVE_ERROR("FontAsset: \"{}\" is not a font file (asset type {})", path.string(),
                  static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }
    const std::vector<std::byte>& payload = file->second;
    std::size_t offset = 0U;
    std::uint32_t version = 0U;
    std::uint32_t nameLength = 0U;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, version) ||
        !Utilities::ReadUint32LeFromBuffer(payload, offset, nameLength)) {
        UVE_ERROR("FontAsset: \"{}\" has a truncated font header", path.string());
        return false;
    }
    if (version != kFontPayloadVersion) {
        UVE_ERROR("FontAsset: \"{}\" has an unknown payload version {}", path.string(), version);
        return false;
    }
    if (nameLength > kMaximumFontFamilyNameBytes || payload.size() - offset < nameLength) {
        UVE_ERROR("FontAsset: \"{}\" declares an impossible family-name length", path.string());
        return false;
    }
    FontAsset font;
    font.familyName.assign(reinterpret_cast<const char*>(payload.data()) + offset, nameLength);
    offset += nameLength;
    std::uint32_t dataLength = 0U;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, dataLength)) {
        UVE_ERROR("FontAsset: \"{}\" has a truncated font-data header", path.string());
        return false;
    }
    // Exact fit: trailing bytes are corruption, not a newer version (versions bump the header).
    if (dataLength > kMaximumFontAssetBytes || payload.size() - offset != dataLength) {
        UVE_ERROR("FontAsset: \"{}\" declares an impossible font-data length", path.string());
        return false;
    }
    font.fontData.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
    if (!IsFontAssetValid(font)) {
        UVE_ERROR("FontAsset: \"{}\" holds unrecognised font bytes", path.string());
        return false;
    }
    outFont = std::move(font);
    return true;
}

bool SaveFontAsset(const FontAsset& font, const std::filesystem::path& path) {
    if (!IsFontAssetValid(font)) {
        UVE_ERROR("FontAsset: refusing to save an invalid font to {}", path.string());
        return false;
    }
    std::vector<std::byte> payload;
    Utilities::AppendUint32Le(payload, kFontPayloadVersion);
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(font.familyName.size()));
    const auto* const nameBytes = reinterpret_cast<const std::byte*>(font.familyName.data());
    payload.insert(payload.end(), nameBytes, nameBytes + font.familyName.size());
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(font.fontData.size()));
    payload.insert(payload.end(), font.fontData.begin(), font.fontData.end());
    return WriteUveFile(path, AssetKind::Font, payload);
}

} // namespace UVE::Asset
