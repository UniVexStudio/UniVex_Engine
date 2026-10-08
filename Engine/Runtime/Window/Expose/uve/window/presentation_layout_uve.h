// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Window {

/// Pixel-space destination rectangle plus the logical content extent used by stretch/aspect policy.
/// `x`/`y` are top-left-origin coordinates, matching the engine's editor viewport rectangles.
struct PresentationLayoutUVE final {
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    double logicalWidth = 0.0;
    double logicalHeight = 0.0;
    double scaleX = 0.0;
    double scaleY = 0.0;
    bool integerScaleAvailable = true;

    [[nodiscard]] bool operator==(const PresentationLayoutUVE&) const = default;
};

/// Computes a letterboxed viewport and logical content extent without touching the graphics API.
/// The caller supplies the project's reference (virtual) resolution and current framebuffer size.
/// Integer-only scaling keeps whole-number magnification; when the output is smaller than the
/// reference resolution, the returned flag is false and the caller should report/fallback rather
/// than silently inventing a fractional pixel scale.
[[nodiscard]] PresentationLayoutUVE ComputePresentationLayoutUVE(
    std::uint32_t framebufferWidth, std::uint32_t framebufferHeight,
    std::uint32_t referenceWidth, std::uint32_t referenceHeight,
    Platform::AspectPolicyUVE aspectPolicy, bool integerOnlyScaling) noexcept;

} // namespace UVE::Window
