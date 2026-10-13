// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumSprite2DSizePixelsUVE = 0.0F;
inline constexpr float kMaximumSprite2DSizePixelsUVE = 8192.0F;

/// A 2D textured quad in world space: the first 2D scene node. `textureAssetGuid` names
/// an imported texture the same way MeshComponentUVE::materialGuid names a material - a
/// zero/invalid guid is a valid authored state (renders as a flat `modulate` quad with no
/// texture sampled), matching UIImageComponentUVE rather than being rejected.
///
/// `sourceRect` is the pixel region drawn: a zero-area rect draws the WHOLE texture (no
/// region authored), anything else draws that region. The quad is centered on `offset`
/// (local pixels from the node origin) at the region's pixel size; flips mirror the
/// sampled UVs, never the corners. v fractions run top-down (v 0 is the texture's top
/// row), matching the UI overlay's own glyph convention.
/// Thread-safety: value type; trivially safe to copy/move.
struct Sprite2DComponentUVE final {
    Asset::AssetGuidUVE textureAssetGuid{};
    Math::RectUVE sourceRect{};
    bool flipH = false;
    bool flipV = false;
    Math::Vector3UVE modulate{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    Math::Vector2UVE offset{};
};

[[nodiscard]] bool IsSprite2DComponentValidUVE(const Sprite2DComponentUVE& component) noexcept;

} // namespace UVE::Scene
