// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "render_queue.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Render {

namespace {

[[nodiscard]] bool RenderItemTieBreakLess(const RenderItem& lhs, const RenderItem& rhs) noexcept {
    const std::uint64_t lhsMaterialGuid = lhs.materialHandle.GetGuid().value;
    const std::uint64_t rhsMaterialGuid = rhs.materialHandle.GetGuid().value;
    if (lhsMaterialGuid != rhsMaterialGuid) {
        return lhsMaterialGuid < rhsMaterialGuid;
    }
    return lhs.meshHandle.GetGuid().value < rhs.meshHandle.GetGuid().value;
}

} // namespace

void RenderQueue::Clear() noexcept {
    opaqueItems.clear();
    transparentItems.clear();
    particleItems.clear();
    particleItemsTruncated = false;
    invalidAssetReferences = 0U;
    pendingAssetLoads = 0U;
    failedAssetLoads = 0U;
    invalidRenderEligibility = 0U;
}

void RenderQueue::Reserve(const std::size_t opaqueCapacity, const std::size_t transparentCapacity,
                                const std::size_t particleCapacity) {
    opaqueItems.reserve(opaqueCapacity);
    transparentItems.reserve(transparentCapacity);
    particleItems.reserve(particleCapacity);
}

void RenderQueue::SortForDepthOnlyPass() {
    // Mesh first - that is the whole point, since the batcher only merges adjacent same-mesh runs.
    // Depth second so that within one mesh the pass still draws nearest-first and the depth test
    // rejects as much as possible; material last purely to keep the order total and therefore
    // stable across frames.
    //
    // Only opaqueItems is reordered. A depth-only pass does not draw transparent geometry or
    // particles, so touching those buckets would be work with no consumer - and they keep whatever
    // order they had, which nothing in this pass reads.
    std::sort(opaqueItems.begin(), opaqueItems.end(), [](const RenderItem& lhs, const RenderItem& rhs) {
        const std::uint64_t lhsMeshGuid = lhs.meshHandle.GetGuid().value;
        const std::uint64_t rhsMeshGuid = rhs.meshHandle.GetGuid().value;
        if (lhsMeshGuid != rhsMeshGuid) {
            return lhsMeshGuid < rhsMeshGuid;
        }
        // Same non-finite handling as Sort: a NaN depth must not make the comparator
        // inconsistent, which is undefined behaviour in std::sort rather than merely a wrong order.
        const bool lhsFinite = std::isfinite(lhs.sortDepth);
        const bool rhsFinite = std::isfinite(rhs.sortDepth);
        if (lhsFinite != rhsFinite) {
            return lhsFinite;
        }
        if (lhsFinite && lhs.sortDepth != rhs.sortDepth) {
            return lhs.sortDepth < rhs.sortDepth;
        }
        return lhs.materialHandle.GetGuid().value < rhs.materialHandle.GetGuid().value;
    });
}

void RenderQueue::Sort() {
    std::sort(opaqueItems.begin(), opaqueItems.end(), [](const RenderItem& lhs, const RenderItem& rhs) {
        const bool lhsFinite = std::isfinite(lhs.sortDepth);
        const bool rhsFinite = std::isfinite(rhs.sortDepth);
        if (lhsFinite != rhsFinite) {
            return lhsFinite;
        }
        if (lhsFinite && lhs.sortDepth != rhs.sortDepth) {
            return lhs.sortDepth < rhs.sortDepth;
        }
        return RenderItemTieBreakLess(lhs, rhs);
    });
    std::sort(transparentItems.begin(), transparentItems.end(), [](const RenderItem& lhs, const RenderItem& rhs) {
        const bool lhsFinite = std::isfinite(lhs.sortDepth);
        const bool rhsFinite = std::isfinite(rhs.sortDepth);
        if (lhsFinite != rhsFinite) {
            return lhsFinite;
        }
        if (lhsFinite && lhs.sortDepth != rhs.sortDepth) {
            return lhs.sortDepth > rhs.sortDepth;
        }
        return RenderItemTieBreakLess(lhs, rhs);
    });
    std::sort(particleItems.begin(), particleItems.end(), [](const ParticleRenderItem& lhs,
                                                             const ParticleRenderItem& rhs) {
        if (lhs.sortDepth != rhs.sortDepth) {
            return lhs.sortDepth > rhs.sortDepth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        if (lhs.entity.generation != rhs.entity.generation) {
            return lhs.entity.generation < rhs.entity.generation;
        }
        return lhs.sequence < rhs.sequence;
    });
}

void RenderQueue::AppendParticleSnapshot(const ParticleRenderSnapshot& snapshot) {
    if (!ValidateParticleRenderSnapshot(snapshot)) {
        particleItemsTruncated = true;
        return;
    }
    particleItems.insert(particleItems.end(), snapshot.items.begin(), snapshot.items.end());
    particleItemsTruncated = particleItemsTruncated || snapshot.truncated;
}

} // namespace UVE::Render
