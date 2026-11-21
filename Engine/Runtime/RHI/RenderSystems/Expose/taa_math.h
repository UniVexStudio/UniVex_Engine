// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "matrix4x4.h"

namespace UVE::Render {

inline constexpr std::uint32_t kTaaJitterSampleCountUVE = 16U;

/// Radical-inverse Halton sequence in (0, 1) for low-discrepancy temporal sub-pixel jitter.
[[nodiscard]] inline float HaltonSequenceUVE(std::uint32_t index, const std::uint32_t base) noexcept {
    if (base < 2U) {
        return 0.0F;
    }
    float result = 0.0F;
    float fraction = 1.0F / static_cast<float>(base);
    while (index > 0U) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

/// Returns a deterministic sub-pixel NDC offset in (-1/width, +1/width) x (-1/height, +1/height).
[[nodiscard]] inline std::array<float, 2> ComputeTaaJitterNdcUVE(const std::uint32_t frameIndex,
                                                                 const std::uint32_t width,
                                                                 const std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return {0.0F, 0.0F};
    }
    const std::uint32_t sample = (frameIndex % kTaaJitterSampleCountUVE) + 1U;
    const float jitterPixelX = HaltonSequenceUVE(sample, 2U) - 0.5F;
    const float jitterPixelY = HaltonSequenceUVE(sample, 3U) - 0.5F;
    return {
        (2.0F * jitterPixelX) / static_cast<float>(width),
        (2.0F * jitterPixelY) / static_cast<float>(height),
    };
}

/// Applies sub-pixel TAA jitter directly to a projection matrix without modifying clip planes.
[[nodiscard]] inline Math::Matrix4x4UVE ApplyTaaProjectionJitterUVE(const Math::Matrix4x4UVE& projection,
                                                                    const std::uint32_t frameIndex,
                                                                    const std::uint32_t width,
                                                                    const std::uint32_t height) noexcept {
    Math::Matrix4x4UVE jittered = projection;
    const std::array<float, 2> ndc = ComputeTaaJitterNdcUVE(frameIndex, width, height);
    const bool isPerspective = std::abs(projection.m[3][2]) > 1.0e-5F;
    if (isPerspective) {
        jittered.m[0][2] += ndc[0];
        jittered.m[1][2] += ndc[1];
    } else {
        jittered.m[0][3] += ndc[0];
        jittered.m[1][3] += ndc[1];
    }
    return jittered;
}

} // namespace UVE::Render
