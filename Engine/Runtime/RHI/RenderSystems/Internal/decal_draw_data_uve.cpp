// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/decal_draw_data_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Render {

namespace {

/// The squared distance from `camera` to the closest patch vertex, or infinity for a draw with no
/// patches left. Squared because the result is only ever compared with other squared distances.
[[nodiscard]] float NearestPatchDistanceSquaredUVE(const DecalDrawUVE& draw,
                                                   const Math::Vector3UVE& camera) noexcept {
    float nearest = std::numeric_limits<float>::infinity();
    for (const DecalPatchUVE& patch : draw.patches) {
        for (std::size_t vertexIndex = 0; vertexIndex < patch.vertexCount; ++vertexIndex) {
            const Math::Vector3UVE offset = patch.worldPositions[vertexIndex] - camera;
            const float distanceSquared = Math::DotUVE(offset, offset);
            nearest = std::min(nearest, distanceSquared);
        }
    }
    return nearest;
}

} // namespace

void DecalDrawListUVE::ClearUVE() noexcept {
    draws.clear();
    materialHandles.clear();
    decalsConsidered = 0U;
    decalsSkippedNotPainting = 0U;
    decalsOutsideView = 0U;
    decalsCulledByLayer = 0U;
    decalsFadedOut = 0U;
    decalsWithoutMaterial = 0U;
    decalsWithoutReceivers = 0U;
    receiversTested = 0U;
    receiversOverlapped = 0U;
    patchesEmitted = 0U;
    patchesDiscardedByFade = 0U;
    patchesDiscardedBackFacing = 0U;
    patchesDiscardedDegenerate = 0U;
}

const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>* DecalDrawListUVE::TryGetMaterialHandleUVE(
    const std::size_t materialIndex) const noexcept {
    if (materialIndex >= materialHandles.size()) {
        return nullptr;
    }
    return &materialHandles[materialIndex];
}

std::size_t DecalDrawListUVE::GetPatchCountUVE() const noexcept {
    std::size_t count = 0U;
    for (const DecalDrawUVE& draw : draws) {
        count += draw.patches.size();
    }
    return count;
}

std::size_t DecalDrawListUVE::GetTriangleCountUVE() const noexcept {
    std::size_t triangles = 0U;
    for (const DecalDrawUVE& draw : draws) {
        for (const DecalPatchUVE& patch : draw.patches) {
            // A fan of n vertices is n-2 triangles, and a patch is only ever emitted with three or
            // more, so the count cannot underflow.
            triangles += patch.vertexCount - 2U;
        }
    }
    return triangles;
}

void DecalDrawListUVE::AppendVertexStreamUVE(std::vector<Asset::MeshVertexUVE>& outVertices,
                                             std::vector<std::uint32_t>& outIndices) const {
    for (const DecalDrawUVE& draw : draws) {
        for (const DecalPatchUVE& patch : draw.patches) {
            if (!patch.IsValidUVE()) {
                continue;
            }
            const std::uint32_t base = static_cast<std::uint32_t>(outVertices.size());
            for (std::size_t vertexIndex = 0; vertexIndex < patch.vertexCount; ++vertexIndex) {
                Asset::MeshVertexUVE vertex{};
                vertex.position = patch.worldPositions[vertexIndex];
                vertex.normal = patch.normal;
                // The volume's unit coordinates, remapped from [-1, 1] to the [0, 1] a texture is
                // sampled with. A material authored for a decal can therefore treat (0.5, 0.5) as
                // the centre of the projection without knowing the decal's size or mode.
                vertex.u = patch.unitCoords[vertexIndex].x * 0.5F + 0.5F;
                vertex.v = patch.unitCoords[vertexIndex].y * 0.5F + 0.5F;
                vertex.tangent = draw.projectionDirection;
                vertex.tangentHandedness = 1.0F;
                outVertices.push_back(vertex);
            }
            for (std::size_t fanIndex = 1U; fanIndex + 1U < patch.vertexCount; ++fanIndex) {
                outIndices.push_back(base);
                outIndices.push_back(base + static_cast<std::uint32_t>(fanIndex));
                outIndices.push_back(base + static_cast<std::uint32_t>(fanIndex) + 1U);
            }
        }
    }
}

void DecalDrawListUVE::SortBackToFrontUVE(const Math::Vector3UVE& cameraWorldPosition) {
    std::stable_sort(draws.begin(), draws.end(),
                     [&cameraWorldPosition](const DecalDrawUVE& left, const DecalDrawUVE& right) {
                         const float leftDistance = NearestPatchDistanceSquaredUVE(left, cameraWorldPosition);
                         const float rightDistance = NearestPatchDistanceSquaredUVE(right, cameraWorldPosition);
                         // Ties keep their extraction order: two decals whose nearest patches are
                         // equally far apart have no depth answer between them, and inventing one
                         // by address or by entity id would make the frame differ run to run.
                         return leftDistance > rightDistance;
                     });
}

} // namespace UVE::Render
