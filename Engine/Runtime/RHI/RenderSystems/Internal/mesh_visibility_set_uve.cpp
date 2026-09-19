// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_visibility_set_uve.h"

namespace UVE::Render {
namespace {

/// Exact equality, on purpose. See MeshPlacementKeyUVE's doc comment: this is an identity test,
/// not a tolerance test.
///
/// NaN deserves a word. A NaN component compares unequal to itself, so an entity with a NaN
/// transform misses the cache every frame and is recomputed every frame. That is the correct
/// outcome twice over: recomputation is what rejects it (placement validation fails on non-finite
/// input), and a cache that "matched" two NaNs would be claiming a broken transform is unchanged.
[[nodiscard]] bool SameVectorUVE(const Math::Vector3UVE& lhs, const Math::Vector3UVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool SameQuaternionUVE(const Math::QuaternionUVE& lhs, const Math::QuaternionUVE& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool SameAabbUVE(const Math::AabbUVE& lhs, const Math::AabbUVE& rhs) noexcept {
    return SameVectorUVE(lhs.min, rhs.min) && SameVectorUVE(lhs.max, rhs.max);
}

} // namespace

bool MeshPlacementKeyUVE::MatchesUVE(const MeshPlacementKeyUVE& other) const noexcept {
    // Mesh guid first: it is a single integer compare and the field most likely to differ when a
    // cache entry is genuinely stale (a swapped mesh), so it rejects earliest for least work.
    return meshGuid == other.meshGuid && SameVectorUVE(worldPosition, other.worldPosition) &&
           SameQuaternionUVE(worldRotation, other.worldRotation) && SameVectorUVE(worldScale, other.worldScale) &&
           SameAabbUVE(localBounds, other.localBounds);
}

void MeshVisibilitySetUVE::ClearUVE() noexcept {
    // clear(), not a fresh vector: the whole reason this is caller-owned is so a frame reuses last
    // frame's capacity instead of reallocating a scene-sized buffer every frame.
    //
    // placementCache is deliberately NOT cleared here - it is the one piece of state meant to
    // survive into the next frame. PruneUnseenPlacementsUVE bounds it instead.
    candidates.clear();
    placementCacheHits = 0U;
    placementCacheMisses = 0U;
    invalidAssetReferences = 0U;
    pendingAssetLoads = 0U;
    failedAssetLoads = 0U;
    invalidRenderEligibility = 0U;
}

void MeshVisibilitySetUVE::PruneUnseenPlacementsUVE() {
    // Erase-while-iterating over an unordered_map, which is safe for the erased element only - so
    // the iterator is advanced before the erase, never after.
    for (auto it = placementCache.begin(); it != placementCache.end();) {
        if (it->second.lastSeenFrame != frameIndex) {
            it = placementCache.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace UVE::Render
