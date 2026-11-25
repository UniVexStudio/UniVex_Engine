// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "decal_draw_data.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Render {

namespace {

/// The squared distance from `camera` to the closest patch vertex, or infinity for a draw with no
/// patches left. Squared because the result is only ever compared with other squared distances.
[[nodiscard]] float NearestPatchDistanceSquared(const DecalDraw& draw,
                                                   const Math::Vector3& camera) noexcept {
    float nearest = std::numeric_limits<float>::infinity();
    for (const DecalPatch& patch : draw.patches) {
        for (std::size_t vertexIndex = 0; vertexIndex < patch.vertexCount; ++vertexIndex) {
            const Math::Vector3 offset = patch.worldPositions[vertexIndex] - camera;
            const float distanceSquared = Math::Dot(offset, offset);
            nearest = std::min(nearest, distanceSquared);
        }
    }
    return nearest;
}

} // namespace

void DecalDrawList::Clear() noexcept {
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

const Asset::AssetHandle<Asset::MaterialAsset>* DecalDrawList::TryGetMaterialHandle(
    const std::size_t materialIndex) const noexcept {
    if (materialIndex >= materialHandles.size()) {
        return nullptr;
    }
    return &materialHandles[materialIndex];
}

std::size_t DecalDrawList::GetPatchCount() const noexcept {
    std::size_t count = 0U;
    for (const DecalDraw& draw : draws) {
        count += draw.patches.size();
    }
    return count;
}

std::size_t DecalDrawList::GetTriangleCount() const noexcept {
    std::size_t triangles = 0U;
    for (const DecalDraw& draw : draws) {
        for (const DecalPatch& patch : draw.patches) {
            // A fan of n vertices is n-2 triangles, and a patch is only ever emitted with three or
            // more, so the count cannot underflow.
            triangles += patch.vertexCount - 2U;
        }
    }
    return triangles;
}

void AppendDecalPatch(const DecalPatch& patch, const Math::Vector3& projectionDirection,
                         std::vector<Asset::MeshVertex>& outVertices,
                         std::vector<std::uint32_t>& outIndices) {
    if (!patch.IsValid()) {
        return;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(outVertices.size());
    for (std::size_t vertexIndex = 0; vertexIndex < patch.vertexCount; ++vertexIndex) {
        Asset::MeshVertex vertex{};
        vertex.position = patch.worldPositions[vertexIndex];
        vertex.normal = patch.normal;
        // The volume's unit coordinates, remapped from [-1, 1] to the [0, 1] a texture is sampled
        // with. A material authored for a decal can therefore treat (0.5, 0.5) as the centre of the
        // projection without knowing the decal's size or mode.
        vertex.u = patch.unitCoords[vertexIndex].x * 0.5F + 0.5F;
        vertex.v = patch.unitCoords[vertexIndex].y * 0.5F + 0.5F;
        vertex.tangent = projectionDirection;
        vertex.tangentHandedness = 1.0F;
        outVertices.push_back(vertex);
    }
    for (std::size_t fanIndex = 1U; fanIndex + 1U < patch.vertexCount; ++fanIndex) {
        outIndices.push_back(base);
        outIndices.push_back(base + static_cast<std::uint32_t>(fanIndex));
        outIndices.push_back(base + static_cast<std::uint32_t>(fanIndex) + 1U);
    }
}

void DecalDrawList::AppendVertexStream(std::vector<Asset::MeshVertex>& outVertices,
                                             std::vector<std::uint32_t>& outIndices) const {
    for (const DecalDraw& draw : draws) {
        for (const DecalPatch& patch : draw.patches) {
            AppendDecalPatch(patch, draw.projection.projectionDirection, outVertices, outIndices);
        }
    }
}

void DecalDrawList::SortBackToFront(const Math::Vector3& cameraWorldPosition) {
    std::stable_sort(draws.begin(), draws.end(),
                     [&cameraWorldPosition](const DecalDraw& left, const DecalDraw& right) {
                         const float leftDistance = NearestPatchDistanceSquared(left, cameraWorldPosition);
                         const float rightDistance = NearestPatchDistanceSquared(right, cameraWorldPosition);
                         // Ties keep their extraction order: two decals whose nearest patches are
                         // equally far apart have no depth answer between them, and inventing one
                         // by address or by entity id would make the frame differ run to run.
                         return leftDistance > rightDistance;
                     });
}

} // namespace UVE::Render
