// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Render {

/// A convex polygon of one receiving surface that lies inside a decal's volume, in world space and
/// with the decal's own unit coordinates per vertex.
///
/// WHY PATCHES AND NOT A VOLUME DRAW. A volume draw needs the receiving surface's depth per pixel
/// to reconstruct where the decal lands, which this engine's forward path does not have. Building
/// the patch on the CPU instead asks the same question once per surface rather than once per pixel
/// and answers it with the volume's own mathematics: clip the surface against the volume in unit
/// space, map the surviving vertices back to the world, and hand the shader a polygon that is
/// already exactly where the decal belongs. The unit coordinates ARE the material's UVs, so the
/// shader needs no projection matrix at all.
///
/// The vertices are in the canonical MeshVertexUVE order - position, normal, uv, tangent - so a
/// decal patch can be consumed by the same vertex layout and upload path an asset-backed mesh uses.
struct DecalPatchUVE final {
    /// A receiving face clipped by the volume. Four planes of a box or the cylinder's wall can each
    /// only add a vertex, and a rectangle clipped by a convex volume has at most eight.
    static constexpr std::size_t kMaximumVerticesUVE = 8U;

    std::array<Math::Vector3UVE, kMaximumVerticesUVE> worldPositions{};
    /// The decal's unit coordinates of each vertex, parallel to worldPositions. (0,0) is the
    /// volume's centre and the surface of the volume is at 1 in every direction, so the material
    /// can be authored against a unit disc/square with no knowledge of the decal's size.
    std::array<Math::Vector2UVE, kMaximumVerticesUVE> unitCoords{};
    std::size_t vertexCount = 0U;
    /// The receiving face's outward normal - the surface normal the fade was evaluated with, and
    /// the patch's shading normal.
    Math::Vector3UVE normal{};
    /// The combined fade weight at the patch's centre: 0 means the patch was not emitted at all.
    float weight = 0.0F;

    [[nodiscard]] bool IsValidUVE() const noexcept { return vertexCount >= 3U && weight > 0.0F; }
};

/// One visible decal this frame: the material to paint with, the volume's world transform for the
/// shader's own use (origin and projection direction), and the patches it lands on.
struct DecalDrawUVE final {
    Scene::EntityUVE decal = Scene::kInvalidEntityUVE;
    /// Index into DecalDrawListUVE::materialHandles. An index rather than a handle for the reason
    /// the mesh visibility set stores one: several decals commonly share one material, and a
    /// reference-count increment and decrement per decal per frame is the same record counted many
    /// times over. The list holds one reference per distinct material, which is what keeps the
    /// material alive across AssetManagerUVE::CollectGarbageUVE().
    std::size_t materialIndex = 0U;
    /// Unit direction the decal projects along - the patch tangent, so a shader can tell which way
    /// the decal is looking without a second uniform.
    Math::Vector3UVE projectionDirection{};
    float sortDepth = 0.0F;
    std::vector<DecalPatchUVE> patches;
};

/// This frame's projected decals, plus what building them cost and rejected.
///
/// The counters exist because a decal that paints nothing has several entirely different causes -
/// it expired, its layers matched no receiver, every receiver was out of its volume, every sample
/// faded to zero - and "no decals drawn" on its own cannot tell them apart.
struct DecalDrawListUVE final {
    std::vector<DecalDrawUVE> draws;
    /// One reference per distinct decal material used this frame. Parallel to the indices in
    /// DecalDrawUVE::materialIndex, and valid for as long as this list is.
    std::vector<Asset::AssetHandleUVE<Asset::MaterialAssetUVE>> materialHandles;
    std::size_t decalsConsidered = 0U;
    std::size_t decalsSkippedNotPainting = 0U;
    std::size_t decalsOutsideView = 0U;
    std::size_t decalsCulledByLayer = 0U;
    std::size_t decalsFadedOut = 0U;
    std::size_t decalsWithoutMaterial = 0U;
    std::size_t decalsWithoutReceivers = 0U;
    std::size_t receiversTested = 0U;
    std::size_t receiversOverlapped = 0U;
    std::size_t patchesEmitted = 0U;
    std::size_t patchesDiscardedByFade = 0U;
    std::size_t patchesDiscardedBackFacing = 0U;
    std::size_t patchesDiscardedDegenerate = 0U;

    void ClearUVE() noexcept;

    /// The material a draw names, or nullptr for an index that is not in this list.
    [[nodiscard]] const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>* TryGetMaterialHandleUVE(
        std::size_t materialIndex) const noexcept;

    [[nodiscard]] std::size_t GetPatchCountUVE() const noexcept;

    /// The number of triangles the patch set contains, without building the index buffer: the
    /// question a budget check asks before anything allocates.
    [[nodiscard]] std::size_t GetTriangleCountUVE() const noexcept;

    /// Appends every patch in the list to a canonical MeshVertexUVE/index stream, so the decal pass
    /// hands the GPU exactly the layout an asset-backed mesh uses. Each patch becomes a triangle
    /// fan. Vertices are appended, not replaced, so several draws can share one buffer; each patch's
    /// tangent is the decal's projection direction, which is what a shader needs to know which way
    /// the decal is looking without a second uniform.
    void AppendVertexStreamUVE(std::vector<Asset::MeshVertexUVE>& outVertices,
                               std::vector<std::uint32_t>& outIndices) const;

    /// Back-to-front by distance from `cameraWorldPosition`, so overlapping decals blend in the
    /// order the artist sees them. Depth is measured to the nearest patch rather than to the decal
    /// origin: a decal projecting along a wall has its origin beside that wall, not on it.
    void SortBackToFrontUVE(const Math::Vector3UVE& cameraWorldPosition);
};

} // namespace UVE::Render
