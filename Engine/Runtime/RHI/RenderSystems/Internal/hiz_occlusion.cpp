// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hiz_occlusion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace UVE::Render {

bool HiZDepthPyramidUVE::BuildFromDepthBufferUVE(const std::span<const float> depth01,
                                                 const std::uint32_t width, const std::uint32_t height) {
    m_mips.clear();
    if (width == 0U || height == 0U ||
        depth01.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }

    MipLevelUVE base{};
    base.width = width;
    base.height = height;
    base.maxDepth.assign(depth01.begin(), depth01.end());
    m_mips.push_back(std::move(base));

    while (m_mips.back().width > 1U || m_mips.back().height > 1U) {
        const MipLevelUVE& prev = m_mips.back();
        MipLevelUVE next{};
        next.width = std::max(prev.width / 2U, 1U);
        next.height = std::max(prev.height / 2U, 1U);
        next.maxDepth.resize(static_cast<std::size_t>(next.width) * next.height, 1.0F);
        for (std::uint32_t y = 0U; y < next.height; ++y) {
            const std::uint32_t sy0 = std::min(y * 2U, prev.height - 1U);
            const std::uint32_t sy1 = std::min(sy0 + 1U, prev.height - 1U);
            for (std::uint32_t x = 0U; x < next.width; ++x) {
                const std::uint32_t sx0 = std::min(x * 2U, prev.width - 1U);
                const std::uint32_t sx1 = std::min(sx0 + 1U, prev.width - 1U);
                const float d00 = prev.maxDepth[static_cast<std::size_t>(sy0) * prev.width + sx0];
                const float d10 = prev.maxDepth[static_cast<std::size_t>(sy0) * prev.width + sx1];
                const float d01 = prev.maxDepth[static_cast<std::size_t>(sy1) * prev.width + sx0];
                const float d11 = prev.maxDepth[static_cast<std::size_t>(sy1) * prev.width + sx1];
                // Conservative max depth: if the odd edge exists, include extra column/row too.
                float maxD = std::max(std::max(d00, d10), std::max(d01, d11));
                if ((prev.width & 1U) != 0U && x + 1U == next.width && sx1 + 1U < prev.width) {
                    maxD = std::max(maxD, prev.maxDepth[static_cast<std::size_t>(sy0) * prev.width + sx1 + 1U]);
                    maxD = std::max(maxD, prev.maxDepth[static_cast<std::size_t>(sy1) * prev.width + sx1 + 1U]);
                }
                if ((prev.height & 1U) != 0U && y + 1U == next.height && sy1 + 1U < prev.height) {
                    maxD = std::max(maxD, prev.maxDepth[static_cast<std::size_t>(sy1 + 1U) * prev.width + sx0]);
                    maxD = std::max(maxD, prev.maxDepth[static_cast<std::size_t>(sy1 + 1U) * prev.width + sx1]);
                }
                next.maxDepth[static_cast<std::size_t>(y) * next.width + x] = maxD;
            }
        }
        m_mips.push_back(std::move(next));
    }
    return true;
}

bool HiZDepthPyramidUVE::TestWorldAabbVisibleUVE(const Math::AabbUVE& worldBounds,
                                                 const Math::Matrix4x4UVE& prevViewProjection,
                                                 const float depthEpsilon) const noexcept {
    if (m_mips.empty()) {
        return true;
    }
    const std::array<Math::Vector3UVE, 8U> corners{{
        {worldBounds.min.x, worldBounds.min.y, worldBounds.min.z},
        {worldBounds.max.x, worldBounds.min.y, worldBounds.min.z},
        {worldBounds.min.x, worldBounds.max.y, worldBounds.min.z},
        {worldBounds.max.x, worldBounds.max.y, worldBounds.min.z},
        {worldBounds.min.x, worldBounds.min.y, worldBounds.max.z},
        {worldBounds.max.x, worldBounds.min.y, worldBounds.max.z},
        {worldBounds.min.x, worldBounds.max.y, worldBounds.max.z},
        {worldBounds.max.x, worldBounds.max.y, worldBounds.max.z},
    }};

    float minU = 1.0F;
    float maxU = 0.0F;
    float minV = 1.0F;
    float maxV = 0.0F;
    float nearestDepth01 = 1.0F;

    for (const Math::Vector3UVE& c : corners) {
        const float cx = prevViewProjection.m[0][0] * c.x + prevViewProjection.m[0][1] * c.y +
                         prevViewProjection.m[0][2] * c.z + prevViewProjection.m[0][3];
        const float cy = prevViewProjection.m[1][0] * c.x + prevViewProjection.m[1][1] * c.y +
                         prevViewProjection.m[1][2] * c.z + prevViewProjection.m[1][3];
        const float cz = prevViewProjection.m[2][0] * c.x + prevViewProjection.m[2][1] * c.y +
                         prevViewProjection.m[2][2] * c.z + prevViewProjection.m[2][3];
        const float cw = prevViewProjection.m[3][0] * c.x + prevViewProjection.m[3][1] * c.y +
                         prevViewProjection.m[3][2] * c.z + prevViewProjection.m[3][3];
        // Any corner behind/on the near plane makes projection conservative -> visible.
        if (cw <= 1.0e-4F) {
            return true;
        }
        const float invW = 1.0F / cw;
        const float u = cx * invW * 0.5F + 0.5F;
        const float v = cy * invW * 0.5F + 0.5F;
        // Matrix4x4UVE::PerspectiveUVE and OrthographicUVE already map [near, far] -> [0, 1].
        const float depth01 = cz * invW;
        if (depth01 <= 0.0F) {
            return true;
        }
        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        nearestDepth01 = std::min(nearestDepth01, depth01);
    }

    if (maxU < 0.0F || minU > 1.0F || maxV < 0.0F || minV > 1.0F) {
        return true;
    }
    minU = std::clamp(minU, 0.0F, 1.0F);
    maxU = std::clamp(maxU, 0.0F, 1.0F);
    minV = std::clamp(minV, 0.0F, 1.0F);
    maxV = std::clamp(maxV, 0.0F, 1.0F);

    const float baseW = static_cast<float>(m_mips[0].width);
    const float baseH = static_cast<float>(m_mips[0].height);
    const float spanPx = std::max((maxU - minU) * baseW, (maxV - minV) * baseH);
    const std::uint32_t desiredMip =
        spanPx <= 1.0F ? 0U : static_cast<std::uint32_t>(std::ceil(std::log2(spanPx)));
    const std::uint32_t mipIndex = std::min(desiredMip, static_cast<std::uint32_t>(m_mips.size() - 1U));
    const MipLevelUVE& mip = m_mips[mipIndex];

    const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(minU * static_cast<float>(mip.width)), mip.width - 1U);
    const std::uint32_t x1 = std::min(static_cast<std::uint32_t>(maxU * static_cast<float>(mip.width)), mip.width - 1U);
    const std::uint32_t y0 = std::min(static_cast<std::uint32_t>(minV * static_cast<float>(mip.height)), mip.height - 1U);
    const std::uint32_t y1 = std::min(static_cast<std::uint32_t>(maxV * static_cast<float>(mip.height)), mip.height - 1U);

    float maxOccluderDepth = 0.0F;
    for (std::uint32_t y = y0; y <= y1; ++y) {
        for (std::uint32_t x = x0; x <= x1; ++x) {
            maxOccluderDepth = std::max(maxOccluderDepth, mip.maxDepth[static_cast<std::size_t>(y) * mip.width + x]);
        }
    }
    return (nearestDepth01 - depthEpsilon) <= maxOccluderDepth;
}

} // namespace UVE::Render
