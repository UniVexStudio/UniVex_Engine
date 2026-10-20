// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/shadow_atlas_manager_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "uve/rhi/i_render_device_uve.h"

namespace UVE::Render {

namespace {

[[nodiscard]] constexpr std::uint32_t ClampToPowerOfTwoTileSizeUVE(std::uint32_t size) noexcept {
    if (size <= 128U) return 128U;
    if (size <= 256U) return 256U;
    if (size <= 512U) return 512U;
    if (size <= 1024U) return 1024U;
    if (size <= 2048U) return 2048U;
    if (size <= 4096U) return 4096U;
    return 0U;
}

} // namespace

ShadowAtlasManagerUVE::ShadowAtlasManagerUVE(IRenderDeviceUVE* renderDevice)
    : m_renderDevice(renderDevice) {
    if (m_renderDevice != nullptr) {
        TextureDescUVE desc{};
        desc.width = kShadowAtlasResolutionUVE;
        desc.height = kShadowAtlasResolutionUVE;
        desc.format = TextureFormatUVE::Depth32Float;
        desc.mipLevels = 1U;
        desc.type = TextureTypeUVE::Texture2D;
        desc.colorSpace = TextureColorSpaceUVE::Linear;
        desc.usage = TextureUsageUVE::RenderTarget | TextureUsageUVE::Sampled;
        m_atlasTexture = m_renderDevice->CreateTextureUVE(desc);
    }
    ResetUVE();
}

ShadowAtlasManagerUVE::ShadowAtlasManagerUVE(IRenderDeviceUVE& renderDevice)
    : ShadowAtlasManagerUVE(&renderDevice) {}

ShadowAtlasManagerUVE::~ShadowAtlasManagerUVE() {
    if (m_renderDevice != nullptr && m_atlasTexture != kInvalidTextureHandleUVE) {
        m_renderDevice->DestroyTextureUVE(m_atlasTexture);
        m_atlasTexture = kInvalidTextureHandleUVE;
    }
}

ShadowAtlasManagerUVE::ShadowAtlasManagerUVE(ShadowAtlasManagerUVE&& other) noexcept
    : m_renderDevice(other.m_renderDevice),
      m_atlasTexture(other.m_atlasTexture),
      m_nodes(std::move(other.m_nodes)),
      m_freeNodeIndices(std::move(other.m_freeNodeIndices)),
      m_allocatedTileCount(other.m_allocatedTileCount) {
    other.m_renderDevice = nullptr;
    other.m_atlasTexture = kInvalidTextureHandleUVE;
    other.m_allocatedTileCount = 0U;
}

ShadowAtlasManagerUVE& ShadowAtlasManagerUVE::operator=(ShadowAtlasManagerUVE&& other) noexcept {
    if (this != &other) {
        if (m_renderDevice != nullptr && m_atlasTexture != kInvalidTextureHandleUVE) {
            m_renderDevice->DestroyTextureUVE(m_atlasTexture);
        }
        m_renderDevice = other.m_renderDevice;
        m_atlasTexture = other.m_atlasTexture;
        m_nodes = std::move(other.m_nodes);
        m_freeNodeIndices = std::move(other.m_freeNodeIndices);
        m_allocatedTileCount = other.m_allocatedTileCount;

        other.m_renderDevice = nullptr;
        other.m_atlasTexture = kInvalidTextureHandleUVE;
        other.m_allocatedTileCount = 0U;
    }
    return *this;
}

void ShadowAtlasManagerUVE::ResetUVE() noexcept {
    m_nodes.clear();
    m_freeNodeIndices.clear();
    m_allocatedTileCount = 0U;
    m_nodes.reserve(1365U);

    ShadowAtlasNodeUVE root{};
    root.x = 0U;
    root.y = 0U;
    root.size = kShadowAtlasResolutionUVE;
    root.isAllocated = false;
    root.isSplit = false;
    root.parentIndex = -1;
    root.childIndices = {-1, -1, -1, -1};
    m_nodes.push_back(root);
}

std::optional<TileAllocUVE> ShadowAtlasManagerUVE::AllocateTileUVE(std::uint32_t sizePixels) {
    const std::uint32_t targetSize = ClampToPowerOfTwoTileSizeUVE(sizePixels);
    if (targetSize == 0U) {
        return std::nullopt;
    }

    const std::int32_t allocatedIndex = FindAndAllocateNodeUVE(0, targetSize);
    if (allocatedIndex < 0) {
        return std::nullopt;
    }

    ++m_allocatedTileCount;
    const ShadowAtlasNodeUVE& node = m_nodes[static_cast<std::size_t>(allocatedIndex)];

    TileAllocUVE tile{};
    tile.tileId = static_cast<std::uint32_t>(allocatedIndex);
    tile.pixelRect = ComputePixelRectUVE(node.x, node.y, node.size);
    tile.normalizedRect = ComputeTileRectUVE(node.x, node.y, node.size);
    return tile;
}

void ShadowAtlasManagerUVE::FreeTileUVE(const TileAllocUVE& tile) {
    FreeTileUVE(tile.tileId);
}

void ShadowAtlasManagerUVE::FreeTileUVE(std::uint32_t tileId) {
    if (static_cast<std::size_t>(tileId) >= m_nodes.size()) {
        return;
    }
    ShadowAtlasNodeUVE& node = m_nodes[tileId];
    if (!node.isAllocated || node.isSplit) {
        return;
    }

    node.isAllocated = false;
    if (m_allocatedTileCount > 0U) {
        --m_allocatedTileCount;
    }

    CoalesceBuddiesUVE(node.parentIndex);
}

bool ShadowAtlasManagerUVE::IsTileAllocatedUVE(std::uint32_t tileId) const noexcept {
    if (static_cast<std::size_t>(tileId) >= m_nodes.size()) {
        return false;
    }
    return m_nodes[tileId].isAllocated && !m_nodes[tileId].isSplit;
}

std::optional<TileAllocUVE> ShadowAtlasManagerUVE::GetTileUVE(std::uint32_t tileId) const noexcept {
    if (!IsTileAllocatedUVE(tileId)) {
        return std::nullopt;
    }
    const ShadowAtlasNodeUVE& node = m_nodes[tileId];
    TileAllocUVE tile{};
    tile.tileId = tileId;
    tile.pixelRect = ComputePixelRectUVE(node.x, node.y, node.size);
    tile.normalizedRect = ComputeTileRectUVE(node.x, node.y, node.size);
    return tile;
}

std::int32_t ShadowAtlasManagerUVE::AllocateNodeSlotUVE() {
    if (!m_freeNodeIndices.empty()) {
        const std::int32_t idx = m_freeNodeIndices.back();
        m_freeNodeIndices.pop_back();
        m_nodes[static_cast<std::size_t>(idx)] = ShadowAtlasNodeUVE{};
        return idx;
    }
    const std::int32_t idx = static_cast<std::int32_t>(m_nodes.size());
    m_nodes.push_back(ShadowAtlasNodeUVE{});
    return idx;
}

void ShadowAtlasManagerUVE::ReleaseNodeSlotUVE(std::int32_t nodeIndex) {
    if (nodeIndex >= 0 && static_cast<std::size_t>(nodeIndex) < m_nodes.size()) {
        m_nodes[static_cast<std::size_t>(nodeIndex)] = ShadowAtlasNodeUVE{};
        m_freeNodeIndices.push_back(nodeIndex);
    }
}

std::int32_t ShadowAtlasManagerUVE::FindAndAllocateNodeUVE(std::int32_t nodeIndex, std::uint32_t targetSize) {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= m_nodes.size()) {
        return -1;
    }

    ShadowAtlasNodeUVE& node = m_nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.isAllocated || node.size < targetSize) {
        return -1;
    }

    if (node.isSplit) {
        if (node.size <= targetSize) {
            return -1;
        }
        for (std::int32_t childIndex : node.childIndices) {
            std::int32_t allocated = FindAndAllocateNodeUVE(childIndex, targetSize);
            if (allocated >= 0) {
                return allocated;
            }
        }
        return -1;
    }

    // Node is unallocated and not split
    if (node.size == targetSize) {
        node.isAllocated = true;
        return nodeIndex;
    }

    // Node is larger than targetSize and not split: subdivide into 4 buddies
    const std::uint32_t halfSize = node.size / 2U;
    const std::uint32_t px = node.x;
    const std::uint32_t py = node.y;

    std::array<std::int32_t, 4> children{-1, -1, -1, -1};
    for (int i = 0; i < 4; ++i) {
        children[i] = AllocateNodeSlotUVE();
    }

    // Re-acquire reference after potential reallocations
    ShadowAtlasNodeUVE& parentNode = m_nodes[static_cast<std::size_t>(nodeIndex)];
    parentNode.isSplit = true;
    parentNode.childIndices = children;

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> offsets = {{
        {px, py},
        {px + halfSize, py},
        {px, py + halfSize},
        {px + halfSize, py + halfSize}
    }};

    for (std::size_t i = 0; i < 4; ++i) {
        ShadowAtlasNodeUVE& child = m_nodes[static_cast<std::size_t>(children[i])];
        child.x = offsets[i].first;
        child.y = offsets[i].second;
        child.size = halfSize;
        child.isAllocated = false;
        child.isSplit = false;
        child.parentIndex = nodeIndex;
        child.childIndices = {-1, -1, -1, -1};
    }

    return FindAndAllocateNodeUVE(children[0], targetSize);
}

void ShadowAtlasManagerUVE::CoalesceBuddiesUVE(std::int32_t parentIndex) {
    if (parentIndex < 0 || static_cast<std::size_t>(parentIndex) >= m_nodes.size()) {
        return;
    }

    ShadowAtlasNodeUVE& parent = m_nodes[static_cast<std::size_t>(parentIndex)];
    if (!parent.isSplit) {
        return;
    }

    for (std::int32_t childIndex : parent.childIndices) {
        if (childIndex < 0 || static_cast<std::size_t>(childIndex) >= m_nodes.size()) {
            return;
        }
        const ShadowAtlasNodeUVE& child = m_nodes[static_cast<std::size_t>(childIndex)];
        if (child.isAllocated || child.isSplit) {
            return;
        }
    }

    // All four children are unallocated and non-split: coalesce into parent
    const std::array<std::int32_t, 4> childrenToRelease = parent.childIndices;
    parent.isSplit = false;
    parent.childIndices = {-1, -1, -1, -1};

    for (std::int32_t childIndex : childrenToRelease) {
        ReleaseNodeSlotUVE(childIndex);
    }

    CoalesceBuddiesUVE(parent.parentIndex);
}

Math::Matrix4x4UVE ShadowAtlasManagerUVE::ComputeSpotLightMatrixUVE(
    const Math::Vector3UVE& position,
    const Math::QuaternionUVE& rotation,
    float spotAngleDegrees,
    float range,
    float nearPlane) noexcept {
    const float clampedAngle = std::clamp(spotAngleDegrees, 1.0F, 89.0F);
    const float fovY = 2.0F * clampedAngle * (3.14159265358979323846F / 180.0F);
    const float safeNear = std::max(nearPlane, 0.01F);
    const float safeFar = std::max(range, safeNear + 0.1F);
    const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(position, rotation);
    const Math::Matrix4x4UVE proj = Math::Matrix4x4UVE::PerspectiveUVE(fovY, 1.0F, safeNear, safeFar);
    return proj * view;
}

Math::Vector2UVE ShadowAtlasManagerUVE::OctahedralEncodeUVE(const Math::Vector3UVE& direction) noexcept {
    const float l1 = std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
    if (l1 <= 1.0e-5F) {
        return Math::Vector2UVE{0.5F, 0.5F};
    }
    float px = direction.x / l1;
    float py = direction.y / l1;
    if (direction.z < 0.0F) {
        const float oldPx = px;
        px = (1.0F - std::abs(py)) * (oldPx >= 0.0F ? 1.0F : -1.0F);
        py = (1.0F - std::abs(oldPx)) * (py >= 0.0F ? 1.0F : -1.0F);
    }
    return Math::Vector2UVE{px * 0.5F + 0.5F, py * 0.5F + 0.5F};
}

Math::Vector3UVE ShadowAtlasManagerUVE::OctahedralDecodeUVE(const Math::Vector2UVE& uv) noexcept {
    float px = uv.x * 2.0F - 1.0F;
    float py = uv.y * 2.0F - 1.0F;
    float pz = 1.0F - std::abs(px) - std::abs(py);
    if (pz < 0.0F) {
        const float oldPx = px;
        px = (1.0F - std::abs(py)) * (oldPx >= 0.0F ? 1.0F : -1.0F);
        py = (1.0F - std::abs(oldPx)) * (py >= 0.0F ? 1.0F : -1.0F);
    }
    const Math::Vector3UVE dir{px, py, pz};
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len <= 1.0e-5F) {
        return Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    }
    return Math::Vector3UVE{dir.x / len, dir.y / len, dir.z / len};
}

Math::Vector4UVE ShadowAtlasManagerUVE::ComputeTileRectUVE(
    std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept {
    const float invAtlas = 1.0F / static_cast<float>(kShadowAtlasResolutionUVE);
    const float uMin = static_cast<float>(x) * invAtlas;
    const float vMin = static_cast<float>(y) * invAtlas;
    const float uMax = static_cast<float>(x + size) * invAtlas;
    const float vMax = static_cast<float>(y + size) * invAtlas;
    return Math::Vector4UVE{uMin, vMin, uMax, vMax};
}

ViewportRectUVE ShadowAtlasManagerUVE::ComputePixelRectUVE(
    std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept {
    ViewportRectUVE rect{};
    rect.position = Math::Vector2iUVE{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
    rect.size = Math::Vector2iUVE{static_cast<std::int32_t>(size), static_cast<std::int32_t>(size)};
    return rect;
}

} // namespace UVE::Render
