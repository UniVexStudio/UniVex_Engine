// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::UI {

/// What a UIQuadUVE actually samples when it reaches the GPU (Phase U3). Phase U2 only produces
/// this plain data - no texture is ever bound here.
enum class UIDrawItemKindUVE : std::uint8_t {
    /// Flat-tinted quad, no texture (a UIImageComponentUVE with a zero/invalid asset guid, or a
    /// UIButtonComponentUVE's own background).
    SolidColor,
    /// Samples `imageAssetGuid`'s imported texture (a UIImageComponentUVE with a real asset guid).
    Image,
    /// Samples the shared font atlas at (u0,v0)-(u1,v1) (one character from a UITextComponentUVE).
    Glyph,
};

/// One screen-space quad with a top-left origin. UIRuntimeUVE transforms authored window-coordinate
/// widgets into the renderer's current presentation-target coordinates before returning its batch.
/// `color`/`alpha` tint whatever `kind` samples (or is the flat fill color for SolidColor). Plain
/// data only - no GPU resource is referenced anywhere in this struct.
struct UIQuadUVE final {
    Math::Vector2UVE positionPixels{};
    Math::Vector2UVE sizePixels{};
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    UIDrawItemKindUVE kind = UIDrawItemKindUVE::SolidColor;
    Asset::AssetGuidUVE imageAssetGuid{};
};

/// The CPU-side output of one UIRuntimeUVE::TickUVE() call - a flat, ordered list of quads ready
/// to be uploaded and drawn. Paint order: canvas sortOrder (orphans at 0, under a sort-0 canvas),
/// then images, then buttons, then text on top inside that canvas.
struct UIDrawBatchUVE final {
    std::vector<UIQuadUVE> quads;
};

} // namespace UVE::UI
