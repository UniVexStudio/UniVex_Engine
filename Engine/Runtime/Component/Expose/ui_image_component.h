// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "asset_guid.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIImageSizePixels = 1.0F;
inline constexpr float kMaximumUIImageSizePixels = 8192.0F;

/// A screen-space UI image quad covering `rect` (raw window pixel coordinates, top-left origin).
/// `textureAssetGuid` names an imported
/// texture asset the same way MeshComponent::materialGuid names a material - a zero/invalid guid
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
struct UIImageComponent final {
    Asset::AssetGuid textureAssetGuid{};
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{64.0F, 64.0F}};
    Math::Vector3 tintColor{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    bool nineSliceEnabled = false;
    bool sliceFillCenter = true;
    Math::Vector2 sliceMarginMin{0.0F, 0.0F};
    Math::Vector2 sliceMarginMax{0.0F, 0.0F};
    Math::Vector2 sliceUVMin{0.0F, 0.0F};
    Math::Vector2 sliceUVMax{0.0F, 0.0F};
    [[nodiscard]] bool operator==(const UIImageComponent&) const = default;
};

[[nodiscard]] bool IsUIImageComponentValid(const UIImageComponent& component) noexcept;

/// Sanitizes `component` so `rect`, `tintColor`, `alpha`, and 9-slice margins/UVs pass `IsUIImageComponentValid`.
[[nodiscard]] UIImageComponent SanitizeUIImageComponent(
    const UIImageComponent& component) noexcept;

/// True when `component.textureAssetGuid != Asset::kInvalidAssetGuid`.
[[nodiscard]] bool HasAssignedUIImageTexture(const UIImageComponent& component) noexcept;

/// True when `nineSliceEnabled` is true, a texture is assigned, and the component is valid.
[[nodiscard]] bool UsesUIImageNineSliceSplitting(
    const UIImageComponent& component) noexcept;

/// Computes the inner center-slice rectangle of `component.rect` (after proportional margin clamping).
[[nodiscard]] Math::Rect ResolveUIImageNineSliceCenterRect(
    const UIImageComponent& component) noexcept;

} // namespace UVE::Scene
