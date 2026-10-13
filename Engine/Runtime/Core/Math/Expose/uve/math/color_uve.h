// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "uve/math/vector3_uve.h"

namespace UVE::Math {

/// A linear-working-space RGB color: the renderer's lights, flat material colors, and everything
/// else the HDR pipeline shades are stored in this type, in the same linear space the lighting
/// math runs in (tone mapping consumes linear; the sRGB swapchain encodes on output, and sRGB
/// textures decode on import — so a flat color and a texture of the same authored shade now agree
/// instead of disagreeing by a gamma curve).
/// RGB only, no alpha: alpha travels alongside on every current consumer (an `alphaCutoff`, a UI
/// `alpha`, the editor picker's channel), and inventing a fourth channel here would leave every
/// one of them converting. Values are NOT clamped to [0,1]: lights and emissives are HDR (their
/// validators reject negatives, not brightness); albedo additionally clamps at ITS validator.
/// The display boundary is explicit and one-directional per call: display-authored values (the
/// inspector picker, MTL `Kd`/`Ke`, hex triplets) enter linear storage through
/// ColorFromDisplayUVE(), and stored linear values leave for display through DisplayFromColorUVE().
/// There are deliberately no implicit conversions either way — the old bug was a display value
/// silently consumed as linear, and this type exists to make that a compile error instead.
/// Thread-safety: value type; safe to copy/pass freely, no shared state.
struct ColorUVE {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

[[nodiscard]] constexpr bool operator==(const ColorUVE& lhs, const ColorUVE& rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

[[nodiscard]] constexpr bool operator!=(const ColorUVE& lhs, const ColorUVE& rhs) noexcept {
    return !(lhs == rhs);
}

/// Lossless channel shuffle into a Vector3, for the vec3-only sinks this color still feeds (the
/// `SetVector3UVE` uniform upload, renderer staging, decals). No colorspace change: linear stays
/// linear, exactly as the old `Vector3UVE` color fields consumed it.
[[nodiscard]] constexpr Vector3UVE ToVector3UVE(const ColorUVE& color) noexcept {
    return Vector3UVE{color.r, color.g, color.b};
}

/// sRGB display-encoded `value` to linear, per the sRGB transfer function (piecewise, matching
/// what the swapchain's hardware decode does — NOT a gamma-2.2 approximation). Honestly extended
/// past [0,1] in both directions (the formula is continuous through 0 and monotonic past 1), and
/// never clamped: clamping is the caller's policy, and HDR emissives legitimately exceed 1.
/// Non-constexpr: uses std::pow.
[[nodiscard]] float DisplayToLinearUVE(float value) noexcept;

/// Linear `value` to sRGB display-encoded, the inverse of DisplayToLinearUVE(). Same honest
/// extension, same no-clamping contract. Non-constexpr: uses std::pow.
[[nodiscard]] float LinearToDisplayUVE(float value) noexcept;

/// Per-channel DisplayToLinearUVE(): the single front door through which display-authored RGB
/// enters linear storage.
[[nodiscard]] ColorUVE ColorFromDisplayUVE(const Vector3UVE& displayColor) noexcept;

/// Per-channel LinearToDisplayUVE(): stored linear out to a display triple (for the inspector
/// picker, thumbnails, anything an author looks at).
[[nodiscard]] Vector3UVE DisplayFromColorUVE(const ColorUVE& color) noexcept;

/// Formats `color` as `"(r, g, b)"`, for logging/debugging.
[[nodiscard]] std::string ToStringUVE(const ColorUVE& color);

} // namespace UVE::Math
