// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/presentation_layout_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Window {
namespace {

[[nodiscard]] std::uint32_t RoundPositiveDimensionUVE(const double value) noexcept {
    const double maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    if (!(value > 0.0) || !std::isfinite(value) || value > maximum) {
        return 0U;
    }
    const double rounded = std::floor(value + 0.5);
    return rounded <= maximum ? static_cast<std::uint32_t>(rounded) : 0U;
}

} // namespace

PresentationLayoutUVE ComputePresentationLayoutUVE(
    const std::uint32_t framebufferWidth, const std::uint32_t framebufferHeight,
    const std::uint32_t referenceWidth, const std::uint32_t referenceHeight,
    const Platform::AspectPolicyUVE aspectPolicy, const bool integerOnlyScaling) noexcept {
    PresentationLayoutUVE result;
    if (framebufferWidth == 0U || framebufferHeight == 0U || referenceWidth == 0U || referenceHeight == 0U ||
        Platform::GetAspectPolicyNameUVE(aspectPolicy).empty()) {
        return result;
    }

    const double frameWidth = static_cast<double>(framebufferWidth);
    const double frameHeight = static_cast<double>(framebufferHeight);
    const double baseWidth = static_cast<double>(referenceWidth);
    const double baseHeight = static_cast<double>(referenceHeight);
    const double framebufferAspect = frameWidth / frameHeight;
    const double referenceAspect = baseWidth / baseHeight;

    result.width = framebufferWidth;
    result.height = framebufferHeight;
    result.logicalWidth = baseWidth;
    result.logicalHeight = baseHeight;

    switch (aspectPolicy) {
    case Platform::AspectPolicyUVE::Ignore:
        // Fill the output exactly; scaleX/scaleY intentionally differ when aspect ratios do.
        break;
    case Platform::AspectPolicyUVE::Keep:
        // Logical dimensions stay at the reference size; a fitted output rectangle is calculated below.
        break;
    case Platform::AspectPolicyUVE::KeepWidth:
        result.logicalHeight = baseWidth / framebufferAspect;
        break;
    case Platform::AspectPolicyUVE::KeepHeight:
        result.logicalWidth = baseHeight * framebufferAspect;
        break;
    case Platform::AspectPolicyUVE::Expand:
        if (framebufferAspect > referenceAspect) {
            result.logicalWidth = baseHeight * framebufferAspect;
        } else {
            result.logicalHeight = baseWidth / framebufferAspect;
        }
        break;
    }

    if (!(result.logicalWidth > 0.0) || !(result.logicalHeight > 0.0) ||
        !std::isfinite(result.logicalWidth) || !std::isfinite(result.logicalHeight)) {
        return PresentationLayoutUVE{};
    }

    if (aspectPolicy == Platform::AspectPolicyUVE::Ignore) {
        if (integerOnlyScaling) {
            // Ignore intentionally permits different horizontal and vertical scales, so pixel-perfect
            // mode floors each axis independently rather than distorting that policy into uniform Keep.
            const double integerScaleX = std::floor(frameWidth / baseWidth);
            const double integerScaleY = std::floor(frameHeight / baseHeight);
            if (integerScaleX < 1.0 || integerScaleY < 1.0) {
                result.integerScaleAvailable = false;
                result.scaleX = frameWidth / baseWidth;
                result.scaleY = frameHeight / baseHeight;
                return result;
            }
            result.width = std::min(framebufferWidth, RoundPositiveDimensionUVE(baseWidth * integerScaleX));
            result.height = std::min(framebufferHeight, RoundPositiveDimensionUVE(baseHeight * integerScaleY));
        }
    } else {
        const double scale = std::min(frameWidth / result.logicalWidth, frameHeight / result.logicalHeight);
        double effectiveScale = scale;
        if (integerOnlyScaling) {
            effectiveScale = std::floor(scale);
            if (effectiveScale < 1.0) {
                // Keep the useful fractional layout so callers can degrade gracefully while still
                // preserving the selected aspect policy.
                result.integerScaleAvailable = false;
                effectiveScale = scale;
            }
        }
        if (aspectPolicy == Platform::AspectPolicyUVE::Keep || integerOnlyScaling) {
            result.width = std::min(framebufferWidth,
                                    RoundPositiveDimensionUVE(result.logicalWidth * effectiveScale));
            result.height = std::min(framebufferHeight,
                                     RoundPositiveDimensionUVE(result.logicalHeight * effectiveScale));
        }
    }

    if (result.width == 0U || result.height == 0U) {
        return PresentationLayoutUVE{};
    }
    result.x = (framebufferWidth - result.width) / 2U;
    result.y = (framebufferHeight - result.height) / 2U;
    result.scaleX = static_cast<double>(result.width) / result.logicalWidth;
    result.scaleY = static_cast<double>(result.height) / result.logicalHeight;
    return result;
}

} // namespace UVE::Window
