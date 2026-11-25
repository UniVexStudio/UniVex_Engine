// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shadow_atlas_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "i_render_device.h"

namespace UVE::Render {

namespace {

[[nodiscard]] constexpr std::uint32_t ClampToPowerOfTwoTileSize(std::uint32_t size) noexcept {
    if (size <= 128U) return 128U;
    if (size <= 256U) return 256U;
    if (size <= 512U) return 512U;
    if (size <= 1024U) return 1024U;
    if (size <= 2048U) return 2048U;
    if (size <= 4096U) return 4096U;
    return 0U;
}

} // namespace

ShadowAtlasManager::ShadowAtlasManager(IRenderDevice* renderDevice)
    : m_renderDevice(renderDevice) {
    if (m_renderDevice != nullptr) {
        TextureDesc desc{};
        desc.width = kShadowAtlasResolution;
        desc.height = kShadowAtlasResolution;
        desc.format = TextureFormat::Depth32Float;
        desc.mipLevels = 1U;
        desc.type = TextureType::Texture2D;
        desc.colorSpace = TextureColorSpace::Linear;
        desc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        m_atlasTexture = m_renderDevice->CreateTexture(desc);
    }
    Reset();
}

ShadowAtlasManager::ShadowAtlasManager(IRenderDevice& renderDevice)
    : ShadowAtlasManager(&renderDevice) {}

ShadowAtlasManager::~ShadowAtlasManager() {
    if (m_renderDevice != nullptr && m_atlasTexture != kInvalidTextureHandle) {
        m_renderDevice->DestroyTexture(m_atlasTexture);
        m_atlasTexture = kInvalidTextureHandle;
    }
}

ShadowAtlasManager::ShadowAtlasManager(ShadowAtlasManager&& other) noexcept
    : m_renderDevice(other.m_renderDevice),
      m_atlasTexture(other.m_atlasTexture),
      m_nodes(std::move(other.m_nodes)),
      m_freeNodeIndices(std::move(other.m_freeNodeIndices)),
      m_allocatedTileCount(other.m_allocatedTileCount) {
    other.m_renderDevice = nullptr;
    other.m_atlasTexture = kInvalidTextureHandle;
    other.m_allocatedTileCount = 0U;
}

ShadowAtlasManager& ShadowAtlasManager::operator=(ShadowAtlasManager&& other) noexcept {
    if (this != &other) {
        if (m_renderDevice != nullptr && m_atlasTexture != kInvalidTextureHandle) {
            m_renderDevice->DestroyTexture(m_atlasTexture);
        }
        m_renderDevice = other.m_renderDevice;
        m_atlasTexture = other.m_atlasTexture;
        m_nodes = std::move(other.m_nodes);
        m_freeNodeIndices = std::move(other.m_freeNodeIndices);
        m_allocatedTileCount = other.m_allocatedTileCount;

        other.m_renderDevice = nullptr;
        other.m_atlasTexture = kInvalidTextureHandle;
        other.m_allocatedTileCount = 0U;
    }
    return *this;
}

void ShadowAtlasManager::Reset() noexcept {
    m_nodes.clear();
    m_freeNodeIndices.clear();
    m_allocatedTileCount = 0U;
    m_nodes.reserve(1365U);

    ShadowAtlasNode root{};
    root.x = 0U;
    root.y = 0U;
    root.size = kShadowAtlasResolution;
    root.isAllocated = false;
    root.isSplit = false;
    root.parentIndex = -1;
    root.childIndices = {-1, -1, -1, -1};
    m_nodes.push_back(root);
}

std::optional<TileAlloc> ShadowAtlasManager::AllocateTile(std::uint32_t sizePixels) {
    const std::uint32_t targetSize = ClampToPowerOfTwoTileSize(sizePixels);
    if (targetSize == 0U) {
        return std::nullopt;
    }

    const std::int32_t allocatedIndex = FindAndAllocateNode(0, targetSize);
    if (allocatedIndex < 0) {
        return std::nullopt;
    }

    ++m_allocatedTileCount;
    const ShadowAtlasNode& node = m_nodes[static_cast<std::size_t>(allocatedIndex)];

    TileAlloc tile{};
    tile.tileId = static_cast<std::uint32_t>(allocatedIndex);
    tile.pixelRect = ComputePixelRect(node.x, node.y, node.size);
    tile.normalizedRect = ComputeTileRect(node.x, node.y, node.size);
    return tile;
}

void ShadowAtlasManager::FreeTile(const TileAlloc& tile) {
    FreeTile(tile.tileId);
}

void ShadowAtlasManager::FreeTile(std::uint32_t tileId) {
    if (static_cast<std::size_t>(tileId) >= m_nodes.size()) {
        return;
    }
    ShadowAtlasNode& node = m_nodes[tileId];
    if (!node.isAllocated || node.isSplit) {
        return;
    }

    node.isAllocated = false;
    if (m_allocatedTileCount > 0U) {
        --m_allocatedTileCount;
    }

    CoalesceBuddies(node.parentIndex);
}

bool ShadowAtlasManager::IsTileAllocated(std::uint32_t tileId) const noexcept {
    if (static_cast<std::size_t>(tileId) >= m_nodes.size()) {
        return false;
    }
    return m_nodes[tileId].isAllocated && !m_nodes[tileId].isSplit;
}

std::optional<TileAlloc> ShadowAtlasManager::GetTile(std::uint32_t tileId) const noexcept {
    if (!IsTileAllocated(tileId)) {
        return std::nullopt;
    }
    const ShadowAtlasNode& node = m_nodes[tileId];
    TileAlloc tile{};
    tile.tileId = tileId;
    tile.pixelRect = ComputePixelRect(node.x, node.y, node.size);
    tile.normalizedRect = ComputeTileRect(node.x, node.y, node.size);
    return tile;
}

std::int32_t ShadowAtlasManager::AllocateNodeSlot() {
    if (!m_freeNodeIndices.empty()) {
        const std::int32_t idx = m_freeNodeIndices.back();
        m_freeNodeIndices.pop_back();
        m_nodes[static_cast<std::size_t>(idx)] = ShadowAtlasNode{};
        return idx;
    }
    const std::int32_t idx = static_cast<std::int32_t>(m_nodes.size());
    m_nodes.push_back(ShadowAtlasNode{});
    return idx;
}

void ShadowAtlasManager::ReleaseNodeSlot(std::int32_t nodeIndex) {
    if (nodeIndex >= 0 && static_cast<std::size_t>(nodeIndex) < m_nodes.size()) {
        m_nodes[static_cast<std::size_t>(nodeIndex)] = ShadowAtlasNode{};
        m_freeNodeIndices.push_back(nodeIndex);
    }
}

std::int32_t ShadowAtlasManager::FindAndAllocateNode(std::int32_t nodeIndex, std::uint32_t targetSize) {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= m_nodes.size()) {
        return -1;
    }

    ShadowAtlasNode& node = m_nodes[static_cast<std::size_t>(nodeIndex)];
    if (node.isAllocated || node.size < targetSize) {
        return -1;
    }

    if (node.isSplit) {
        if (node.size <= targetSize) {
            return -1;
        }
        for (std::int32_t childIndex : node.childIndices) {
            std::int32_t allocated = FindAndAllocateNode(childIndex, targetSize);
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
    for (std::size_t i = 0U; i < 4U; ++i) {
        children[i] = AllocateNodeSlot();
    }

    // Re-acquire reference after potential reallocations
    ShadowAtlasNode& parentNode = m_nodes[static_cast<std::size_t>(nodeIndex)];
    parentNode.isSplit = true;
    parentNode.childIndices = children;

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> offsets = {{
        {px, py},
        {px + halfSize, py},
        {px, py + halfSize},
        {px + halfSize, py + halfSize}
    }};

    for (std::size_t i = 0; i < 4; ++i) {
        ShadowAtlasNode& child = m_nodes[static_cast<std::size_t>(children[i])];
        child.x = offsets[i].first;
        child.y = offsets[i].second;
        child.size = halfSize;
        child.isAllocated = false;
        child.isSplit = false;
        child.parentIndex = nodeIndex;
        child.childIndices = {-1, -1, -1, -1};
    }

    return FindAndAllocateNode(children[0], targetSize);
}

void ShadowAtlasManager::CoalesceBuddies(std::int32_t parentIndex) {
    if (parentIndex < 0 || static_cast<std::size_t>(parentIndex) >= m_nodes.size()) {
        return;
    }

    ShadowAtlasNode& parent = m_nodes[static_cast<std::size_t>(parentIndex)];
    if (!parent.isSplit) {
        return;
    }

    for (std::int32_t childIndex : parent.childIndices) {
        if (childIndex < 0 || static_cast<std::size_t>(childIndex) >= m_nodes.size()) {
            return;
        }
        const ShadowAtlasNode& child = m_nodes[static_cast<std::size_t>(childIndex)];
        if (child.isAllocated || child.isSplit) {
            return;
        }
    }

    // All four children are unallocated and non-split: coalesce into parent
    const std::array<std::int32_t, 4> childrenToRelease = parent.childIndices;
    parent.isSplit = false;
    parent.childIndices = {-1, -1, -1, -1};

    for (std::int32_t childIndex : childrenToRelease) {
        ReleaseNodeSlot(childIndex);
    }

    CoalesceBuddies(parent.parentIndex);
}

Math::Matrix4x4 ShadowAtlasManager::ComputeSpotLightMatrix(
    const Math::Vector3& position,
    const Math::Quaternion& rotation,
    float spotAngleDegrees,
    float range,
    float nearPlane) noexcept {
    const float clampedAngle = std::clamp(spotAngleDegrees, 1.0F, 89.0F);
    const float fovY = 2.0F * clampedAngle * (3.14159265358979323846F / 180.0F);
    const float safeNear = std::max(nearPlane, 0.01F);
    const float safeFar = std::max(range, safeNear + 0.1F);
    const Math::Matrix4x4 view = Math::Matrix4x4::ViewFromPositionAndRotation(position, rotation);
    const Math::Matrix4x4 proj = Math::Matrix4x4::Perspective(fovY, 1.0F, safeNear, safeFar);
    return proj * view;
}

Math::Matrix4x4 ShadowAtlasManager::ComputePointLightFaceMatrix(
    const Math::Vector3& position, const std::uint32_t faceIndex, const float range,
    const float nearPlane) noexcept {
    constexpr float kFaceFovYRadians = 3.14159265358979323846F * 0.5F;
    const auto table = PointLightShadowFaceTable();
    const PointShadowFaceBasis& basis =
        table[std::min(faceIndex, kPointShadowFaceCount - 1U)];

    // View matrix built straight from the basis instead of a quaternion: the basis is already
    // orthonormal, and going through an Euler/quaternion round trip would only add a place for the
    // face table and the rendered image to disagree.
    Math::Matrix4x4 view = Math::Matrix4x4::Identity();
    const Math::Vector3 rows[3] = {basis.right, basis.up, Math::Vector3{-basis.forward.x,
                                                                            -basis.forward.y,
                                                                            -basis.forward.z}};
    for (int row = 0; row < 3; ++row) {
        view.m[row][0] = rows[row].x;
        view.m[row][1] = rows[row].y;
        view.m[row][2] = rows[row].z;
        view.m[row][3] = -(rows[row].x * position.x + rows[row].y * position.y +
                           rows[row].z * position.z);
    }

    const float safeNear = std::max(nearPlane, 0.01F);
    const float safeFar = std::max(range, safeNear + 0.1F);
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(kFaceFovYRadians, 1.0F, safeNear, safeFar);
    return projection * view;
}

std::uint32_t ShadowAtlasManager::PointLightShadowFaceForDirection(
    const Math::Vector3& direction) noexcept {
    const float ax = std::abs(direction.x);
    const float ay = std::abs(direction.y);
    const float az = std::abs(direction.z);
    if (ax >= ay && ax >= az) {
        return direction.x >= 0.0F ? 0U : 1U;
    }
    if (ay >= az) {
        return direction.y >= 0.0F ? 2U : 3U;
    }
    return direction.z >= 0.0F ? 4U : 5U;
}

Math::Vector4 ShadowAtlasManager::ComputeTileRect(
    std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept {
    const float invAtlas = 1.0F / static_cast<float>(kShadowAtlasResolution);
    const float uMin = static_cast<float>(x) * invAtlas;
    const float vMin = static_cast<float>(y) * invAtlas;
    const float uMax = static_cast<float>(x + size) * invAtlas;
    const float vMax = static_cast<float>(y + size) * invAtlas;
    return Math::Vector4{uMin, vMin, uMax, vMax};
}

ViewportRect ShadowAtlasManager::ComputePixelRect(
    std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept {
    ViewportRect rect{};
    rect.position = Math::Vector2i{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
    rect.size = Math::Vector2i{static_cast<std::int32_t>(size), static_cast<std::int32_t>(size)};
    return rect;
}

} // namespace UVE::Render
