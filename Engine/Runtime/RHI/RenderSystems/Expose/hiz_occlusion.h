// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "aabb.h"
#include "matrix4x4.h"

namespace UVE::Render {

/// Conservative hierarchical max-depth (Hi-Z) pyramid built from a previous frame's depth buffer.
/// Tests world-space AABBs via previous-frame reprojection so occlusion culling stays valid even
/// while the camera moves.
class HiZDepthPyramidUVE final {
public:
    struct MipLevelUVE final {
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<float> maxDepth;
    };

    /// Builds a full max-depth mip pyramid from a tightly-packed [0, 1] depth buffer (0 = near, 1 = far).
    bool BuildFromDepthBufferUVE(std::span<const float> depth01, std::uint32_t width, std::uint32_t height);

    void ClearUVE() noexcept { m_mips.clear(); }
    [[nodiscard]] bool IsValidUVE() const noexcept { return !m_mips.empty(); }
    [[nodiscard]] std::uint32_t MipCountUVE() const noexcept { return static_cast<std::uint32_t>(m_mips.size()); }
    [[nodiscard]] const MipLevelUVE& GetMipUVE(std::uint32_t mip) const noexcept { return m_mips[mip]; }

    /// Returns false ONLY when `worldBounds` is provably occluded behind all covered Hi-Z texels
    /// in `prevViewProjection`. Boxes crossing the near plane or outside screen return true (safe).
    [[nodiscard]] bool TestWorldAabbVisibleUVE(const Math::AabbUVE& worldBounds,
                                               const Math::Matrix4x4UVE& prevViewProjection,
                                               float depthEpsilon = 0.0005F) const noexcept;

private:
    std::vector<MipLevelUVE> m_mips;
};

} // namespace UVE::Render
