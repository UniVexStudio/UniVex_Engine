// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIImageSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIImageSizePixelsUVE = 8192.0F;

/// A screen-space UI image quad covering `rect` (raw window pixel coordinates, top-left origin).
/// `textureAssetGuid` names an imported
/// texture asset the same way MeshComponentUVE::materialGuid names a material - a zero/invalid guid
/// is a valid authored state (renders as a flat `tintColor` quad with no texture sampled), matching
/// how an unset asset reference is handled elsewhere in this codebase rather than being rejected.
///
/// With `nineSliceEnabled`, a textured image draws as a 9-slice: the drawn rect splits at
/// `sliceMarginMin` ({left, top}) and `sliceMarginMax` ({right, bottom}) pixels, the texture splits
/// at `sliceUVMin`/`sliceUVMax` fractions, and each drawn cell samples its texture cell - corners
/// hold their authored pixel size however the panel resizes while edges and center stretch.
/// v fractions run top-down (v 0 is the texture's top row), matching the overlay's own glyph
/// convention. Zero-area cells are skipped, so zero margins collapse to the center cell alone;
/// `sliceFillCenter` false skips the center for hollow frames. Over-wide margins shrink
/// proportionally rather than overlapping, flat fills (zero guid) always draw one quad, and
/// non-finite margins fall back to the plain stretched quad - slicing never emits garbage.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIImageComponentUVE final {
    Asset::AssetGuidUVE textureAssetGuid{};
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{64.0F, 64.0F}};
    Math::Vector3UVE tintColor{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    bool nineSliceEnabled = false;
    bool sliceFillCenter = true;
    Math::Vector2UVE sliceMarginMin{0.0F, 0.0F};
    Math::Vector2UVE sliceMarginMax{0.0F, 0.0F};
    Math::Vector2UVE sliceUVMin{0.0F, 0.0F};
    Math::Vector2UVE sliceUVMax{0.0F, 0.0F};
};

[[nodiscard]] bool IsUIImageComponentValidUVE(const UIImageComponentUVE& component) noexcept;

} // namespace UVE::Scene
