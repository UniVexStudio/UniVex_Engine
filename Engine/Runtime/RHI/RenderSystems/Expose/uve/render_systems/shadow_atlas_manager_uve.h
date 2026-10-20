// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/vector4_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {

class IRenderDeviceUVE;

/// Dimensions of the unified 2D shadow atlas texture (4096 x 4096, Depth32Float).
inline constexpr std::uint32_t kShadowAtlasResolutionUVE = 4096U;

/// Minimum allocatable tile resolution in the shadow atlas (128 x 128).
inline constexpr std::uint32_t kShadowAtlasMinTileResolutionUVE = 128U;

/// Maximum allocatable tile resolution in the shadow atlas (4096 x 4096, the root node).
inline constexpr std::uint32_t kShadowAtlasMaxTileResolutionUVE = 4096U;

/// Texture unit slot reserved for binding the unified shadow atlas in material shaders.
inline constexpr std::uint32_t kShadowAtlasTextureSlotUVE = 7U;

/// Represents an allocated tile within the unified shadow atlas.
struct TileAllocUVE {
    std::uint32_t tileId = 0U;
    ViewportRectUVE pixelRect{};
    Math::Vector4UVE normalizedRect{0.0F, 0.0F, 1.0F, 1.0F}; // (uMin, vMin, uMax, vMax)

    [[nodiscard]] constexpr float GetExtentUVE() const noexcept {
        return normalizedRect.z - normalizedRect.x;
    }
};

using ShadowAtlasTileUVE = TileAllocUVE;

/// Node in the quadtree buddy subdivision allocator.
struct ShadowAtlasNodeUVE {
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t size = kShadowAtlasResolutionUVE;
    bool isAllocated = false;
    bool isSplit = false;
    std::int32_t parentIndex = -1;
    std::array<std::int32_t, 4> childIndices{-1, -1, -1, -1};
};

/// Manages a unified 4096x4096 Depth32F shadow atlas for spot and point lights using a dynamic
/// quadtree buddy subdivision allocator. Spot lights project perspective depth into allocated tiles,
/// and omnidirectional point lights project depth into single 2D tiles via octahedral parameterization.
///
/// The quadtree supports power-of-two tile subdivisions from 4096 down to 128. Deallocations perform
/// recursive 4-way buddy coalescing into parent nodes to ensure zero memory fragmentation and zero leaks.
class ShadowAtlasManagerUVE {
public:
    /// Constructs the shadow atlas manager. If `renderDevice` is non-null, allocates the 4096x4096 Depth32F
    /// GPU texture target immediately. If null, operates in CPU-only allocation/math mode for testing.
    explicit ShadowAtlasManagerUVE(IRenderDeviceUVE* renderDevice = nullptr);
    explicit ShadowAtlasManagerUVE(IRenderDeviceUVE& renderDevice);
    ~ShadowAtlasManagerUVE();

    ShadowAtlasManagerUVE(const ShadowAtlasManagerUVE&) = delete;
    ShadowAtlasManagerUVE& operator=(const ShadowAtlasManagerUVE&) = delete;

    ShadowAtlasManagerUVE(ShadowAtlasManagerUVE&& other) noexcept;
    ShadowAtlasManagerUVE& operator=(ShadowAtlasManagerUVE&& other) noexcept;

    /// Returns the underlying 4096x4096 Depth32F atlas texture handle (or kInvalidTextureHandleUVE if CPU-only).
    [[nodiscard]] TextureHandleUVE GetAtlasTextureUVE() const noexcept { return m_atlasTexture; }

    /// Allocates a tile of the requested pixel size (rounded up to nearest power-of-two in [128, 4096]).
    /// Returns std::nullopt if the atlas is out of space or requestedSize > 4096.
    [[nodiscard]] std::optional<TileAllocUVE> AllocateTileUVE(std::uint32_t sizePixels);

    /// Frees an allocated tile and triggers recursive 4-way buddy coalescing on free siblings.
    void FreeTileUVE(const TileAllocUVE& tile);
    void FreeTileUVE(std::uint32_t tileId);

    /// Resets all tile allocations while preserving the allocated GPU texture handle across frames.
    void ResetUVE() noexcept;

    /// Returns the number of currently active tile allocations.
    [[nodiscard]] std::uint32_t GetAllocatedTileCountUVE() const noexcept { return m_allocatedTileCount; }

    /// Returns the total number of nodes currently instantiated in the quadtree pool.
    [[nodiscard]] std::uint32_t GetTotalNodeCountUVE() const noexcept {
        return static_cast<std::uint32_t>(m_nodes.size());
    }

    /// Checks if a tile ID corresponds to a currently allocated tile.
    [[nodiscard]] bool IsTileAllocatedUVE(std::uint32_t tileId) const noexcept;

    /// Retrieves tile info for an allocated tile ID, or std::nullopt if unallocated/invalid.
    [[nodiscard]] std::optional<TileAllocUVE> GetTileUVE(std::uint32_t tileId) const noexcept;

    /// Computes light-space perspective shadow matrix (Projection * View) for a spot light.
    [[nodiscard]] static Math::Matrix4x4UVE ComputeSpotLightMatrixUVE(
        const Math::Vector3UVE& position,
        const Math::QuaternionUVE& rotation,
        float spotAngleDegrees,
        float range,
        float nearPlane = 0.05F) noexcept;

    /// Computes normalized octahedral coordinates [0, 1]^2 from a normalized 3D direction vector.
    [[nodiscard]] static Math::Vector2UVE OctahedralEncodeUVE(const Math::Vector3UVE& direction) noexcept;

    /// Decodes normalized octahedral coordinates [0, 1]^2 back into a normalized 3D direction vector.
    [[nodiscard]] static Math::Vector3UVE OctahedralDecodeUVE(const Math::Vector2UVE& uv) noexcept;

    /// Helper to compute normalized UV rect (uMin, vMin, uMax, vMax) for a tile.
    [[nodiscard]] static Math::Vector4UVE ComputeTileRectUVE(
        std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept;

    /// Helper to compute pixel viewport rect for a tile.
    [[nodiscard]] static ViewportRectUVE ComputePixelRectUVE(
        std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept;

private:
    [[nodiscard]] std::int32_t FindAndAllocateNodeUVE(std::int32_t nodeIndex, std::uint32_t targetSize);
    void CoalesceBuddiesUVE(std::int32_t nodeIndex);
    [[nodiscard]] std::int32_t AllocateNodeSlotUVE();
    void ReleaseNodeSlotUVE(std::int32_t nodeIndex);

    IRenderDeviceUVE* m_renderDevice = nullptr;
    TextureHandleUVE m_atlasTexture = kInvalidTextureHandleUVE;
    std::vector<ShadowAtlasNodeUVE> m_nodes;
    std::vector<std::int32_t> m_freeNodeIndices;
    std::uint32_t m_allocatedTileCount = 0U;
};

} // namespace UVE::Render
