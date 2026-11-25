// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "asset_guid.h"
#include "rect.h"
#include "vector3.h"

namespace UVE::UI {

/// What a UIQuad actually samples when it reaches the GPU (Phase U3). Phase U2 only produces
/// this plain data - no texture is ever bound here.
enum class UIDrawItemKind : std::uint8_t {
    /// Flat-tinted quad, no texture (a UIImageComponent with a zero/invalid asset guid, or a
    /// UIButtonComponent's own background).
    SolidColor,
    /// Samples `imageAssetGuid`'s imported texture (a UIImageComponent with a real asset guid).
    Image,
    /// Samples the shared font atlas at (u0,v0)-(u1,v1) (one character from a UITextComponent).
    Glyph,
    /// A 3D View's picture: the renderer's own capture for `viewKey` (UIView3DComponent).
    View3D,
};

/// One screen-space quad with a top-left origin. UIRuntime transforms authored window-coordinate
/// widgets into the renderer's current presentation-target coordinates before returning its batch.
/// `color`/`alpha` tint whatever `kind` samples (or is the flat fill color for SolidColor). Plain
/// data only - no GPU resource is referenced anywhere in this struct.
struct UIQuad final {
    Math::Rect rect{};
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    Math::Vector3 color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    UIDrawItemKind kind = UIDrawItemKind::SolidColor;
    Asset::AssetGuid imageAssetGuid{};
    /// View3D: which 3D View's capture (its widget, packed).
    std::uint64_t viewKey = 0U;
};

/// The CPU-side output of one UIRuntime::Tick() call - a flat, ordered list of quads ready
/// to be uploaded and drawn. Paint order: canvas sortOrder (orphans at 0, under a sort-0 canvas),
/// then images and progress bars, then buttons, sliders, checkboxes, dropdown boxes, and
/// text-input boxes, then text (including text-input glyphs and carets), tooltips, and dropdown
/// popups on top inside that canvas. Scroll containers clip their descendants in software
/// after sorting: quads are intersected against ancestor scroll boxes (texture coordinates
/// remapped), fully-outside quads dropped.
struct UIDrawBatch final {
    std::vector<UIQuad> quads;
};

} // namespace UVE::UI
