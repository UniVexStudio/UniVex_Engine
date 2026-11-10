// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UVE::Asset {

/// Raw sfnt font bytes (TTF/OTF) plus the family name they were imported under — the asset
/// behind UI text. `UIFontAtlasUVE` used to bake only the engine's embedded Liberation Sans
/// subset; it now bakes whatever bytes a `FontAssetUVE` carries, with the embedded font kept as
/// the default.
///
/// WHAT THIS IS NOT. It is not a parsed font: no glyph table, no shaping, no per-size
/// rasterization. The bytes pass through untouched from the imported `.ttf`/`.otf` to the
/// consumer (`stb_truetype`, in UI), and validation here is deliberately shallow — non-empty,
/// bounded, correct sfnt magic — because the Asset module must not depend on a rasterizer to
/// hold a font. Full shaping stays the consumer's job.
struct FontAssetUVE final {
    /// Informational family name (the import source's stem); never parsed from the name table.
    std::string familyName;
    /// The raw font file bytes, byte-identical to the imported source.
    std::vector<std::byte> fontData;
};

/// Largest font file the asset accepts (64 MiB): fonts are kilobytes-to-megabytes, and an
/// unbounded read is a corrupted-file away from an allocation failure.
inline constexpr std::uint64_t kMaximumFontAssetBytesUVE = 64ULL * 1024ULL * 1024ULL;

/// Longest family name stored (bytes, not glyphs): informational only, so it stays short.
inline constexpr std::size_t kMaximumFontFamilyNameBytesUVE = 256U;

/// True when the asset is worth handing to a rasterizer: a bounded family name and non-empty,
/// bounded font bytes starting with a recognised sfnt version tag (`0x00010000`, `OTTO`,
/// `true` or `typ1`).
[[nodiscard]] bool IsFontAssetValidUVE(const FontAssetUVE& font) noexcept;

/// Loads a `.uvfont` (Font-kind envelope) into `outFont`, leaving it untouched on failure.
[[nodiscard]] bool LoadFontAssetUVE(const std::filesystem::path& path, FontAssetUVE& outFont);

/// Saves `font` as a `.uvfont` (Font-kind envelope). Refuses invalid assets.
[[nodiscard]] bool SaveFontAssetUVE(const FontAssetUVE& font, const std::filesystem::path& path);

} // namespace UVE::Asset
