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

/// Tiles one shadow-casting point light needs: one per cube face.
inline constexpr std::uint32_t kPointShadowFaceCountUVE = 6U;

/// One cube face's view basis, in the order `PointShadowFactorUVE` indexes it.
struct PointShadowFaceBasisUVE {
    /// Direction the face looks down. Also the face's local +Z in the cube's own frame.
    Math::Vector3UVE forward{1.0F, 0.0F, 0.0F};
    /// Face-local +X. `right`, `up` and `forward` are right-handed: right x up == forward.
    Math::Vector3UVE right{0.0F, 0.0F, -1.0F};
    /// Face-local +Y.
    Math::Vector3UVE up{0.0F, 1.0F, 0.0F};
};

/// The six faces, ordered +X, -X, +Y, -Y, +Z, -Z.
///
/// An explicit table rather than a runtime branch: the shader selects a face with the same
/// dominant-axis rule, and a table is the one representation both halves can be read against when
/// a sample lands on the wrong side of a seam.
[[nodiscard]] constexpr std::array<PointShadowFaceBasisUVE, kPointShadowFaceCountUVE>
PointLightShadowFaceTableUVE() noexcept {
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
inline constexpr std::uint32_t kShadowAtlasTextureSlotUVE = 15U;

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

    /// World-to-clip matrix for one face of a point light's cube shadow.
    ///
    /// Every face uses the same 90-degree square projection; only the view basis differs. The basis
    /// is `PointLightShadowFaceBasisUVE` - the exact table `PointShadowFactorUVE` mirrors in
    /// `lit_shadowed_3d.glsl`, so the depth a face renders and the depth the material shader
    /// predicts cannot drift apart. `faceIndex` is clamped into [0, 5].
    [[nodiscard]] static Math::Matrix4x4UVE ComputePointLightFaceMatrixUVE(
        const Math::Vector3UVE& position, std::uint32_t faceIndex, float range,
        float nearPlane = 0.05F) noexcept;

    /// Cube face a direction lands on. Mirrors `PointShadowFaceUVE` in `lit_shadowed_3d.glsl`;
    /// the tests pin the two tables together, because a one-face disagreement silently samples a
    /// neighbouring face's depth - which reads as a plausible shadow rather than as a bug.
    [[nodiscard]] static std::uint32_t PointLightShadowFaceForDirectionUVE(
        const Math::Vector3UVE& direction) noexcept;

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
