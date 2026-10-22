// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/physics_islands_uve.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>

#include "uve/component/entity_uve.h"

namespace UVE::Physics {

namespace {

/// Union-find over dense body indices with path halving. Iterative: a recursive find would let a
/// long chain of touching bodies - which a stack of crates produces - decide the stack depth.
[[nodiscard]] std::uint32_t FindRootUVE(std::vector<std::uint32_t>& parents, std::uint32_t index) noexcept {
    while (parents[index] != index) {
        const std::uint32_t grandparent = parents[parents[index]];
        parents[index] = grandparent;
        index = grandparent;
    }
    return index;
}

void UnionUVE(std::vector<std::uint32_t>& parents, std::vector<std::uint32_t>& sizes, std::uint32_t first,
              std::uint32_t second) noexcept {
    std::uint32_t firstRoot = FindRootUVE(parents, first);
    std::uint32_t secondRoot = FindRootUVE(parents, second);
    if (firstRoot == secondRoot) {
        return;
    }
    // Union by size keeps the tree shallow. Ties go to the lower index so the resulting root does
    // not depend on which argument happened to be passed first.
    if (sizes[firstRoot] < sizes[secondRoot] || (sizes[firstRoot] == sizes[secondRoot] && secondRoot < firstRoot)) {
        std::swap(firstRoot, secondRoot);
    }
    parents[secondRoot] = firstRoot;
    sizes[firstRoot] += sizes[secondRoot];
}

} // namespace

PhysicsIslandPartitionUVE BuildPhysicsIslandPartitionUVE(const std::vector<CollisionPairUVE>& pairs) {
    PhysicsIslandPartitionUVE partition;
    if (pairs.empty()) {
        return partition;
    }

    // Dense body indices in pair-scan order. The scan order is the caller's pair order, so the
    // numbering - and therefore the island numbering - is a pure function of the input list.
    std::unordered_map<Scene::EntityUVE, std::uint32_t> bodyIndices;
    bodyIndices.reserve(pairs.size() * 2U);
    const auto bodyIndexOfUVE = [&bodyIndices](const Scene::EntityUVE entity) {
        const auto inserted = bodyIndices.try_emplace(entity, static_cast<std::uint32_t>(bodyIndices.size()));
        return inserted.first->second;
    };

    // Self-pairs are not collisions, so they must neither union anything nor appear in the result.
    // `resolvable` holds indices into `pairs`, not a filtered copy: `pairOrderUVE` promises indices
    // the caller can use against the list it passed in.
    std::vector<std::uint32_t> resolvable;
    resolvable.reserve(pairs.size());
    for (std::size_t index = 0U; index < pairs.size(); ++index) {
        const CollisionPairUVE& pair = pairs[index];
        if (pair.first == pair.second) {
            continue;
        }
        static_cast<void>(bodyIndexOfUVE(pair.first));
        static_cast<void>(bodyIndexOfUVE(pair.second));
        resolvable.push_back(static_cast<std::uint32_t>(index));
    }
    if (resolvable.empty()) {
        return partition;
    }

    std::vector<std::uint32_t> parents(bodyIndices.size());
    std::vector<std::uint32_t> sizes(bodyIndices.size(), 1U);
    for (std::uint32_t index = 0U; index < parents.size(); ++index) {
        parents[index] = index;
    }
    for (const std::uint32_t pairIndex : resolvable) {
        UnionUVE(parents, sizes, bodyIndices[pairs[pairIndex].first], bodyIndices[pairs[pairIndex].second]);
    }

    // Island ids are assigned from the FINAL roots, in first-appearance order. Assigning them
    // during the merge would bind an island to a root that a later union can absorb into a larger
    // one, splitting what should be a single island in two.
    std::unordered_map<std::uint32_t, std::uint32_t> islandOfRoot;
    std::vector<std::uint32_t> islandBodyCounts;
    std::vector<std::uint32_t> pairIsland(resolvable.size(), 0U);
    for (std::size_t index = 0U; index < resolvable.size(); ++index) {
        const std::uint32_t root = FindRootUVE(parents, bodyIndices[pairs[resolvable[index]].first]);
        const auto inserted = islandOfRoot.try_emplace(root, static_cast<std::uint32_t>(islandBodyCounts.size()));
        if (inserted.second) {
            islandBodyCounts.push_back(0U);
        }
        pairIsland[index] = inserted.first->second;
    }

    // One body counts towards exactly one island - the one its final root names.
    for (std::uint32_t body = 0U; body < parents.size(); ++body) {
        const auto found = islandOfRoot.find(FindRootUVE(parents, body));
        if (found != islandOfRoot.end()) {
            ++islandBodyCounts[found->second];
        }
    }

    // Stable bucketing: count per island, prefix-sum the offsets, then place each pair index in
    // input order. This keeps every island's pairs in their original relative order without a
    // per-island vector, and keeps the whole partition a pure function of the input.
    std::vector<std::uint32_t> islandOffsets(islandBodyCounts.size() + 1U, 0U);
    for (const std::uint32_t island : pairIsland) {
        ++islandOffsets[island + 1U];
    }
    for (std::size_t island = 0U; island < islandBodyCounts.size(); ++island) {
        islandOffsets[island + 1U] += islandOffsets[island];
    }

    partition.pairOrderUVE.resize(islandOffsets.back());
    partition.islandsUVE.resize(islandBodyCounts.size());

    std::vector<std::uint32_t> cursors(islandOffsets.begin(), islandOffsets.end() - 1);
    for (std::size_t index = 0U; index < resolvable.size(); ++index) {
        partition.pairOrderUVE[cursors[pairIsland[index]]++] = resolvable[index];
    }

    for (std::size_t island = 0U; island < islandBodyCounts.size(); ++island) {
        PhysicsIslandUVE& entry = partition.islandsUVE[island];
        entry.firstPairUVE = islandOffsets[island];
        entry.pairCountUVE = islandOffsets[island + 1U] - islandOffsets[island];
        entry.bodyCountUVE = islandBodyCounts[island];
    }
    partition.bodyCountUVE = static_cast<std::uint32_t>(parents.size());

    return partition;
}

} // namespace UVE::Physics
