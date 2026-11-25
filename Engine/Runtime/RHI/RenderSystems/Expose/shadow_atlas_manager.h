// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "matrix4x4.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "render_resource_descs.h"
#include "texture_handle.h"

namespace UVE::Render {

class IRenderDevice;

/// Dimensions of the unified 2D shadow atlas texture (4096 x 4096, Depth32Float).
inline constexpr std::uint32_t kShadowAtlasResolution = 4096U;

/// Minimum allocatable tile resolution in the shadow atlas (128 x 128).
inline constexpr std::uint32_t kShadowAtlasMinTileResolution = 128U;

/// Maximum allocatable tile resolution in the shadow atlas (4096 x 4096, the root node).
inline constexpr std::uint32_t kShadowAtlasMaxTileResolution = 4096U;

/// Tiles one shadow-casting point light needs: one per cube face.
inline constexpr std::uint32_t kPointShadowFaceCount = 6U;

/// One cube face's view basis, in the order `PointShadowFactor` indexes it.
struct PointShadowFaceBasis {
    /// Direction the face looks down. Also the face's local +Z in the cube's own frame.
    Math::Vector3 forward{1.0F, 0.0F, 0.0F};
    /// Face-local +X. `right`, `up` and `forward` are right-handed: right x up == forward.
    Math::Vector3 right{0.0F, 0.0F, -1.0F};
    /// Face-local +Y.
    Math::Vector3 up{0.0F, 1.0F, 0.0F};
};

/// The six faces, ordered +X, -X, +Y, -Y, +Z, -Z.
///
/// An explicit table rather than a runtime branch: the shader selects a face with the same
/// dominant-axis rule, and a table is the one representation both halves can be read against when
/// a sample lands on the wrong side of a seam.
[[nodiscard]] constexpr std::array<PointShadowFaceBasis, kPointShadowFaceCount>
PointLightShadowFaceTable() noexcept {
    return {{
        // +X: looking along +X, with +Y up and the cube's -Z as the face's right.
        {{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}},
        // -X
        {{-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}},
        // +Y: the pole, where +Z becomes "up" so the face does not start degenerate.
        {{0.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}},
        // -Y
        {{0.0F, -1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}},
        // +Z
        {{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
        // -Z
        {{0.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    }};
}

/// Texture unit slot reserved for sampling the unified shadow atlas in material shaders.
///
/// Deliberately the LAST slot inside GLES3's guaranteed 16 fragment texture units rather than the
/// first free one: the built-in lit path already packs slots 0-14 densely (three material maps,
/// three shadow cascades, the reflection-probe cube, two more material maps and the ambient
/// environment map), and a fixed slot in the middle of that range is one rebinding away from
/// shadowing a cascade. The `.glsl` shader declaration names 15 explicitly and the renderer binds
/// it there; the static assertion in the renderer's slot block is what keeps the two honest.
inline constexpr std::uint32_t kShadowAtlasTextureSlot = 15U;

/// Represents an allocated tile within the unified shadow atlas.
struct TileAlloc {
    std::uint32_t tileId = 0U;
    ViewportRect pixelRect{};
    Math::Vector4 normalizedRect{0.0F, 0.0F, 1.0F, 1.0F}; // (uMin, vMin, uMax, vMax)

    [[nodiscard]] constexpr float GetExtent() const noexcept {
        return normalizedRect.z - normalizedRect.x;
    }
};

using ShadowAtlasTile = TileAlloc;

/// Node in the quadtree buddy subdivision allocator.
struct ShadowAtlasNode {
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
    std::uint32_t size = kShadowAtlasResolution;
    bool isAllocated = false;
    bool isSplit = false;
    std::int32_t parentIndex = -1;
    std::array<std::int32_t, 4> childIndices{-1, -1, -1, -1};
};

/// Manages a unified 4096x4096 Depth32F shadow atlas for spot and point lights using a dynamic
/// quadtree buddy subdivision allocator.
///
/// A spot light owns one perspective tile. A point light owns six - one per cube face - because a
/// depth-only pass has to rasterise through a perspective matrix and no single 2D projection covers
/// an omnidirectional light. Six square tiles beat one larger square region holding a 3x2 grid: the
/// allocator only hands out power-of-two squares, so a 3x2 grid would either waste a third of a
/// 4x2 region or force a non-square allocation through an allocator that has no concept of one.
///
/// The quadtree supports power-of-two tile subdivisions from 4096 down to 128. Deallocations perform
/// recursive 4-way buddy coalescing into parent nodes to ensure zero memory fragmentation and zero leaks.
class ShadowAtlasManager {
public:
    /// Constructs the shadow atlas manager. If `renderDevice` is non-null, allocates the 4096x4096 Depth32F
    /// GPU texture target immediately. If null, operates in CPU-only allocation/math mode for testing.
    explicit ShadowAtlasManager(IRenderDevice* renderDevice = nullptr);
    explicit ShadowAtlasManager(IRenderDevice& renderDevice);
    ~ShadowAtlasManager();

    ShadowAtlasManager(const ShadowAtlasManager&) = delete;
    ShadowAtlasManager& operator=(const ShadowAtlasManager&) = delete;

    ShadowAtlasManager(ShadowAtlasManager&& other) noexcept;
    ShadowAtlasManager& operator=(ShadowAtlasManager&& other) noexcept;

    /// Returns the underlying 4096x4096 Depth32F atlas texture handle (or kInvalidTextureHandle if CPU-only).
    [[nodiscard]] TextureHandle GetAtlasTexture() const noexcept { return m_atlasTexture; }

    /// Allocates a tile of the requested pixel size (rounded up to nearest power-of-two in [128, 4096]).
    /// Returns std::nullopt if the atlas is out of space or requestedSize > 4096.
    [[nodiscard]] std::optional<TileAlloc> AllocateTile(std::uint32_t sizePixels);

    /// Frees an allocated tile and triggers recursive 4-way buddy coalescing on free siblings.
    void FreeTile(const TileAlloc& tile);
    void FreeTile(std::uint32_t tileId);

    /// Resets all tile allocations while preserving the allocated GPU texture handle across frames.
    void Reset() noexcept;

    /// Returns the number of currently active tile allocations.
    [[nodiscard]] std::uint32_t GetAllocatedTileCount() const noexcept { return m_allocatedTileCount; }

    /// Returns the total number of nodes currently instantiated in the quadtree pool.
    [[nodiscard]] std::uint32_t GetTotalNodeCount() const noexcept {
        return static_cast<std::uint32_t>(m_nodes.size());
    }

    /// Checks if a tile ID corresponds to a currently allocated tile.
    [[nodiscard]] bool IsTileAllocated(std::uint32_t tileId) const noexcept;

    /// Retrieves tile info for an allocated tile ID, or std::nullopt if unallocated/invalid.
    [[nodiscard]] std::optional<TileAlloc> GetTile(std::uint32_t tileId) const noexcept;

    /// Computes light-space perspective shadow matrix (Projection * View) for a spot light.
    [[nodiscard]] static Math::Matrix4x4 ComputeSpotLightMatrix(
        const Math::Vector3& position,
        const Math::Quaternion& rotation,
        float spotAngleDegrees,
        float range,
        float nearPlane = 0.05F) noexcept;

    /// World-to-clip matrix for one face of a point light's cube shadow.
    ///
    /// Every face uses the same 90-degree square projection; only the view basis differs. The basis
    /// is `PointLightShadowFaceBasis` - the exact table `PointShadowFactor` mirrors in
    /// `lit_shadowed_3d.glsl`, so the depth a face renders and the depth the material shader
    /// predicts cannot drift apart. `faceIndex` is clamped into [0, 5].
    [[nodiscard]] static Math::Matrix4x4 ComputePointLightFaceMatrix(
        const Math::Vector3& position, std::uint32_t faceIndex, float range,
        float nearPlane = 0.05F) noexcept;

    /// Cube face a direction lands on. Mirrors `PointShadowFace` in `lit_shadowed_3d.glsl`;
    /// the tests pin the two tables together, because a one-face disagreement silently samples a
    /// neighbouring face's depth - which reads as a plausible shadow rather than as a bug.
    [[nodiscard]] static std::uint32_t PointLightShadowFaceForDirection(
        const Math::Vector3& direction) noexcept;

    /// Helper to compute normalized UV rect (uMin, vMin, uMax, vMax) for a tile.
    [[nodiscard]] static Math::Vector4 ComputeTileRect(
        std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept;

    /// Helper to compute pixel viewport rect for a tile.
    [[nodiscard]] static ViewportRect ComputePixelRect(
        std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept;

private:
    [[nodiscard]] std::int32_t FindAndAllocateNode(std::int32_t nodeIndex, std::uint32_t targetSize);
    void CoalesceBuddies(std::int32_t nodeIndex);
    [[nodiscard]] std::int32_t AllocateNodeSlot();
    void ReleaseNodeSlot(std::int32_t nodeIndex);

    IRenderDevice* m_renderDevice = nullptr;
    TextureHandle m_atlasTexture = kInvalidTextureHandle;
    std::vector<ShadowAtlasNode> m_nodes;
    std::vector<std::int32_t> m_freeNodeIndices;
    std::uint32_t m_allocatedTileCount = 0U;
};

} // namespace UVE::Render
