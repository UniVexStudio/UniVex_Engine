// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_visibility_set_uve.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

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

namespace {

/// Spreads the low 10 bits of `value` so two zero bits sit between each - the standard Morton
/// interleave. Three of these, shifted apart, pack a 30-bit curve index.
[[nodiscard]] std::uint32_t SpreadBitsUVE(std::uint32_t value) noexcept {
    value &= 0x000003FFU;
    value = (value | (value << 16)) & 0x030000FFU;
    value = (value | (value << 8)) & 0x0300F00FU;
    value = (value | (value << 4)) & 0x030C30C3U;
    value = (value | (value << 2)) & 0x09249249U;
    return value;
}

/// Morton (Z-order) index of `point` within the box described by `origin` and `inverseExtent`.
/// Points close together in space get close indices, which is the whole property the clustering
/// relies on. Coordinates are clamped rather than asserted: the caller derives the box from the
/// same points, but a degenerate axis makes the reciprocal large and the product can still land
/// marginally outside on the boundary.
[[nodiscard]] std::uint32_t MortonIndexUVE(const Math::Vector3UVE& point, const Math::Vector3UVE& origin,
                                           const Math::Vector3UVE& inverseExtent) noexcept {
    const auto quantize = [](float value, float low, float inverse) {
        const float normalized = (value - low) * inverse;
        const float clamped = normalized < 0.0F ? 0.0F : (normalized > 1.0F ? 1.0F : normalized);
        return static_cast<std::uint32_t>(clamped * 1023.0F);
    };
    return (SpreadBitsUVE(quantize(point.x, origin.x, inverseExtent.x)) << 2) |
           (SpreadBitsUVE(quantize(point.y, origin.y, inverseExtent.y)) << 1) |
           SpreadBitsUVE(quantize(point.z, origin.z, inverseExtent.z));
}

/// Candidates per cluster. Large enough that the one-plane-test-per-cluster overhead is small
/// against the run it guards, small enough that a cluster's enclosing box stays tight - a cluster
/// spanning half the world is a cluster that never gets rejected.
constexpr std::size_t kCandidatesPerClusterUVE = 64U;

/// Buckets for the counting pass: the top 11 bits of the Morton index. A comparison sort produces
/// a slightly better ordering but costs more than the culling it saves - measured - so the
/// ordering is deliberately approximate and O(n).
constexpr std::size_t kMortonBucketCountUVE = 2048U;
constexpr std::uint32_t kMortonBucketShiftUVE = 19U;

} // namespace

void MeshVisibilitySetUVE::BuildSpatialClustersUVE() {
    clusters.clear();
    if (candidates.empty()) {
        return;
    }

    // The curve needs a box to quantize into, and it is derived from the candidates themselves so
    // the resolution follows the scene rather than some fixed world size.
    Math::Vector3UVE low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
    Math::Vector3UVE high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                          std::numeric_limits<float>::lowest()};
    for (const MeshVisibilityCandidateUVE& candidate : candidates) {
        const Math::Vector3UVE center = candidate.placement.worldBounds.GetCenterUVE();
        low.x = std::min(low.x, center.x);
        low.y = std::min(low.y, center.y);
        low.z = std::min(low.z, center.z);
        high.x = std::max(high.x, center.x);
        high.y = std::max(high.y, center.y);
        high.z = std::max(high.z, center.z);
    }
    // A scene with no extent on an axis - everything on one plane, or a single candidate - would
    // divide by zero. The floor keeps the reciprocal finite; every point then quantizes to the
    // same bucket on that axis, which is correct for a scene that genuinely has no extent there.
    constexpr float kMinimumExtentUVE = 1e-6F;
    const Math::Vector3UVE inverseExtent{1.0F / std::max(kMinimumExtentUVE, high.x - low.x),
                                         1.0F / std::max(kMinimumExtentUVE, high.y - low.y),
                                         1.0F / std::max(kMinimumExtentUVE, high.z - low.z)};

    // Counting sort on the bucket index. Two passes over the candidates and one over the bucket
    // table, no comparisons.
    m_clusterKeyScratch.resize(candidates.size());
    m_clusterOffsetScratch.assign(kMortonBucketCountUVE + 1U, 0U);
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        const std::uint32_t bucket =
            MortonIndexUVE(candidates[index].placement.worldBounds.GetCenterUVE(), low, inverseExtent) >>
            kMortonBucketShiftUVE;
        m_clusterKeyScratch[index] = bucket;
        ++m_clusterOffsetScratch[static_cast<std::size_t>(bucket) + 1U];
    }
    for (std::size_t bucket = 0U; bucket < kMortonBucketCountUVE; ++bucket) {
        m_clusterOffsetScratch[bucket + 1U] += m_clusterOffsetScratch[bucket];
    }

    // Rebuilt into a second buffer in destination order, then swapped in.
    //
    // NOT done as an in-place cycle of std::swap, which is the obvious implementation and is
    // wrong here. MeshVisibilityCandidateUVE holds AssetHandleUVE members, and that type's move
    // assignment releases the destination's reference and nulls the source rather than exchanging
    // them - it is a transfer, not a swap. std::swap's three moves therefore drop a live reference
    // and leave a nulled handle behind, which surfaces as a double release when the renderer is
    // destroyed, a long way from here. Confirmed under AddressSanitizer.
    //
    // Building a fresh vector avoids the problem entirely: each candidate is moved exactly once
    // into a slot that has never held anything, so no reference count is touched at all. It also
    // sidesteps AssetHandleUVE having no default constructor, which is what rules out resizing a
    // destination and assigning into it.
    m_clusterSlotScratch.resize(candidates.size());
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        m_clusterSlotScratch[index] = m_clusterOffsetScratch[m_clusterKeyScratch[index]]++;
    }

    // Inverted first: slot[i] says where candidate i belongs, but building in destination order
    // needs the opposite - which candidate belongs at each position.
    m_clusterSourceScratch.resize(candidates.size());
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        m_clusterSourceScratch[m_clusterSlotScratch[index]] = index;
    }

    m_candidateScratch.clear();
    m_candidateScratch.reserve(candidates.size());
    for (const std::size_t source : m_clusterSourceScratch) {
        m_candidateScratch.push_back(std::move(candidates[source]));
    }
    candidates.swap(m_candidateScratch);
    // The scratch now holds the moved-from husks. Cleared so those husks do not sit on the
    // renderer for a whole frame pretending to be candidates.
    m_candidateScratch.clear();

    // Clusters over the reordered candidates. The enclosing box is the union of the members'
    // world bounds, so a frustum that misses it provably misses all of them.
    clusters.reserve((candidates.size() + kCandidatesPerClusterUVE - 1U) / kCandidatesPerClusterUVE);
    for (std::size_t first = 0U; first < candidates.size(); first += kCandidatesPerClusterUVE) {
        const std::size_t count = std::min(kCandidatesPerClusterUVE, candidates.size() - first);
        Math::AabbUVE enclosing = candidates[first].placement.worldBounds;
        for (std::size_t offset = 1U; offset < count; ++offset) {
            const Math::AabbUVE& member = candidates[first + offset].placement.worldBounds;
            enclosing.min.x = std::min(enclosing.min.x, member.min.x);
            enclosing.min.y = std::min(enclosing.min.y, member.min.y);
            enclosing.min.z = std::min(enclosing.min.z, member.min.z);
            enclosing.max.x = std::max(enclosing.max.x, member.max.x);
            enclosing.max.y = std::max(enclosing.max.y, member.max.y);
            enclosing.max.z = std::max(enclosing.max.z, member.max.z);
        }
        clusters.push_back(CandidateClusterUVE{enclosing, first, count});
    }
}

void MeshVisibilitySetUVE::ClearUVE() noexcept {
    // clear(), not a fresh vector: the whole reason this is caller-owned is so a frame reuses last
    // frame's capacity instead of reallocating a scene-sized buffer every frame.
    //
    // placementCache is deliberately NOT cleared here - it is the one piece of state meant to
    // survive into the next frame. PruneUnseenPlacementsUVE bounds it instead.
    candidates.clear();
    // Cleared with the candidates they index into: a cluster naming a range of a list that no
    // longer exists is worse than no cluster at all.
    clusters.clear();
    // Released with the candidates that referred to them. This is the frame's only drop of asset
    // references - two per distinct asset, rather than two per entity as it used to be.
    assetPairs.clear();
    placementCacheHits = 0U;
    placementCacheMisses = 0U;
    hiddenEntities = 0U;
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
