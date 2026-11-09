// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Window {

struct AdaptiveRenderResolutionLimitsUVE final {
    std::uint32_t maximumAxis = 1U;
    std::uint64_t maximumPixels = 1ULL;
};

struct AdaptiveRenderResolutionUVE final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

/// Converts a positive drawable size into an aspect-preserving render-target size bounded by both
/// an axis limit and a total-pixel budget. `renderScale` is a fixed, caller-selected multiplier
/// applied before those safety limits; it does not change the presentation surface. Invalid sizes,
/// scales, or limits return {0,0}. The helper is deterministic and platform-neutral so Android
/// startup clamping and live desktop/Android target resizing cannot drift into different policies.
[[nodiscard]] inline AdaptiveRenderResolutionUVE ComputeAdaptiveRenderResolutionUVE(
    const std::uint32_t drawableWidth, const std::uint32_t drawableHeight,
    const AdaptiveRenderResolutionLimitsUVE limits, const double renderScale = 1.0) noexcept {
    if (drawableWidth == 0U || drawableHeight == 0U || limits.maximumAxis == 0U || limits.maximumPixels == 0ULL ||
        !std::isfinite(renderScale) || renderScale <= 0.0) {
        return {};
    }

    const double width = static_cast<double>(drawableWidth) * renderScale;
    const double height = static_cast<double>(drawableHeight) * renderScale;
    const double pixelCount = width * height;
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(pixelCount) || pixelCount <= 0.0) {
        return {};
    }

    const double pixelScale = std::sqrt(
        std::min(1.0, static_cast<double>(limits.maximumPixels) / pixelCount));
    const double axisScale = std::min(
        1.0, std::min(static_cast<double>(limits.maximumAxis) / width,
                      static_cast<double>(limits.maximumAxis) / height));
    const double safetyScale = std::min(pixelScale, axisScale);
    if (!std::isfinite(safetyScale) || safetyScale <= 0.0) {
        return {};
    }

    return AdaptiveRenderResolutionUVE{
        std::max(1U, static_cast<std::uint32_t>(std::floor(width * safetyScale))),
        std::max(1U, static_cast<std::uint32_t>(std::floor(height * safetyScale))),
    };
}

} // namespace UVE::Window
