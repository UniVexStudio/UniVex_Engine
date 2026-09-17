// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIImageSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIImageSizePixelsUVE = 8192.0F;

/// A screen-space UI image quad, anchored at `positionPixels` (raw window pixel coordinates,
/// top-left origin) with an explicit `sizePixels` footprint. `textureAssetGuid` names an imported
/// texture asset the same way MeshComponentUVE::materialGuid names a material - a zero/invalid guid
/// is a valid authored state (renders as a flat `tintColor` quad with no texture sampled), matching
/// how an unset asset reference is handled elsewhere in this codebase rather than being rejected.
/// 9-slice/scalable borders are explicitly out of scope for this pass - the whole quad always
/// stretches to `sizePixels`.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIImageComponentUVE final {
    Asset::AssetGuidUVE textureAssetGuid{};
    Math::Vector2UVE positionPixels{};
    Math::Vector2UVE sizePixels{64.0F, 64.0F};
    Math::Vector3UVE tintColor{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
};

[[nodiscard]] inline bool IsUIImageComponentValidUVE(const UIImageComponentUVE& component) noexcept {
    return std::isfinite(component.positionPixels.x) && std::isfinite(component.positionPixels.y) &&
           std::isfinite(component.sizePixels.x) && std::isfinite(component.sizePixels.y) &&
           component.sizePixels.x >= kMinimumUIImageSizePixelsUVE &&
           component.sizePixels.x <= kMaximumUIImageSizePixelsUVE &&
           component.sizePixels.y >= kMinimumUIImageSizePixelsUVE &&
           component.sizePixels.y <= kMaximumUIImageSizePixelsUVE && std::isfinite(component.tintColor.x) &&
           std::isfinite(component.tintColor.y) && std::isfinite(component.tintColor.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F;
}

} // namespace UVE::Scene
