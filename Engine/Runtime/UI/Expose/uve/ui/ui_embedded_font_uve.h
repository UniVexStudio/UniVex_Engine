// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>

namespace UVE::UI {

/// The engine's embedded Liberation Sans subset (the build-time embedding of
/// `UI/assets/fonts/liberation-sans-subset.ttf`) as raw TTF bytes: the default font behind
/// `UIFontAtlasUVE()` and the fallback whenever no `FontAssetUVE` is assigned. Exposed (rather
/// than kept inside the atlas) so tests can prove an asset-baked atlas matches the default one,
/// and so a future font picker can offer "engine default" as real bytes.
[[nodiscard]] std::span<const std::uint8_t> GetEmbeddedUIFontBytesUVE() noexcept;

} // namespace UVE::UI
