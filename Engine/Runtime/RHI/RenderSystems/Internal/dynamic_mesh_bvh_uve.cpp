// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/dynamic_mesh_bvh_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace UVE::Render {

namespace {

[[nodiscard]] bool IsValidAabbUVE(const Math::AabbUVE& aabb) noexcept {
    return std::isfinite(aabb.min.x) && std::isfinite(aabb.min.y) && std::isfinite(aabb.min.z) &&
           std::isfinite(aabb.max.x) && std::isfinite(aabb.max.y) && std::isfinite(aabb.max.z) &&
           aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
}

[[nodiscard]] float ComputeSurfaceAreaUVE(const Math::AabbUVE& box) noexcept {
    const float dx = std::max(0.0F, box.max.x - box.min.x);
    const float dy = std::max(0.0F, box.max.y - box.min.y);
    const float dz = std::max(0.0F, box.max.z - box.min.z);
    return 2.0F * (dx * dy + dy * dz + dz * dx);
}

} // namespace

DynamicMeshBvhUVE::DynamicMeshBvhUVE(std::vector<EntityEntry> entries) {
    BuildUVE(std::move(entries));
}

std::int32_t DynamicMeshBvhUVE::AllocateNode() {
    if (!m_freeNodeIndices.empty()) {
        const std::int32_t index = m_freeNodeIndices.back();
        m_freeNodeIndices.pop_back();
        m_nodes[static_cast<std::size_t>(index)] = Node{};
        return index;
    }
    const std::int32_t index = static_cast<std::int32_t>(m_nodes.size());
    m_nodes.push_back(Node{});
    return index;
}

void DynamicMeshBvhUVE::FreeNode(const std::int32_t index) noexcept {
    if (index >= 0 && static_cast<std::size_t>(index) < m_nodes.size()) {
        m_nodes[static_cast<std::size_t>(index)] = Node{};
        m_freeNodeIndices.push_back(index);
    }
}

void DynamicMeshBvhUVE::ClearUVE() noexcept {
    m_nodes.clear();
    m_entityToLeafNode.clear();
    m_freeNodeIndices.clear();
    m_root = kInvalidNodeIndex;
    m_entityCount = 0U;
}

void DynamicMeshBvhUVE::BuildUVE(std::vector<EntityEntry> entries) {
    ClearUVE();

    const auto validEnd = std::remove_if(entries.begin(), entries.end(), [](const EntityEntry& entry) {
        return entry.entity == Scene::kInvalidEntityUVE || !IsValidAabbUVE(entry.bounds);
    });
    entries.erase(validEnd, entries.end());

    if (entries.empty()) {
        return;
    }

    // Deduplicate entities (keeping the last authored bounds per entity)
    std::unordered_map<Scene::EntityUVE, std::size_t> uniqueEntities;
    std::vector<EntityEntry> deduplicated;
    deduplicated.reserve(entries.size());
    for (const EntityEntry& entry : entries) {
        uniqueEntities[entry.entity] = deduplicated.size();
        deduplicated.push_back(entry);
    }

    std::vector<EntityEntry> finalEntries;
    finalEntries.reserve(uniqueEntities.size());
    std::vector<bool> included(deduplicated.size(), false);
    for (const auto& [entity, index] : uniqueEntities) {
        included[index] = true;
    }
    for (std::size_t i = 0U; i < deduplicated.size(); ++i) {
        if (included[i]) {
            finalEntries.push_back(deduplicated[i]);
        }
    }

    m_nodes.reserve(finalEntries.size() * 2U);
    m_root = BuildRecursive(finalEntries, 0U, finalEntries.size(), kInvalidNodeIndex);
    m_entityCount = finalEntries.size();
}

std::int32_t DynamicMeshBvhUVE::BuildRecursive(std::vector<EntityEntry>& entries, const std::size_t begin,
                                              const std::size_t end, const std::int32_t parent) {
    const std::size_t count = end - begin;

    // Leaf node creation
    if (count <= kLeafCapacity) {
        const std::int32_t leafIndex = AllocateNode();
        Node& leaf = m_nodes[static_cast<std::size_t>(leafIndex)];
        leaf.parentIndex = parent;
        leaf.leftChild = kInvalidNodeIndex;
        leaf.rightChild = kInvalidNodeIndex;
        leaf.entryCount = static_cast<std::uint32_t>(count);

        Math::AabbUVE bounds = entries[begin].bounds;
        for (std::size_t i = begin; i < end; ++i) {
            const std::size_t slot = i - begin;
            leaf.entries[slot] = entries[i];
            if (i > begin) {
                bounds = bounds.UnionUVE(entries[i].bounds);
            }
            m_entityToLeafNode[entries[i].entity] = leafIndex;
        }
        leaf.bounds = bounds;
        return leafIndex;
    }

    // Internal node creation: calculate bounding box of all entries
    Math::AabbUVE bounds = entries[begin].bounds;
    for (std::size_t i = begin + 1U; i < end; ++i) {
        bounds = bounds.UnionUVE(entries[i].bounds);
    }

    // Split on axis with longest extent
    const Math::Vector3UVE extents = bounds.GetExtentsUVE();
    std::size_t splitAxis = 0U;
    if (extents.y > extents.x && extents.y >= extents.z) {
        splitAxis = 1U;
    } else if (extents.z > extents.x && extents.z > extents.y) {
        splitAxis = 2U;
    }

    const auto CenterOnAxis = [splitAxis](const EntityEntry& entry) noexcept {
        const Math::Vector3UVE center = entry.bounds.GetCenterUVE();
        return splitAxis == 0U ? center.x : (splitAxis == 1U ? center.y : center.z);
    };

    std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(begin),
                     entries.begin() + static_cast<std::ptrdiff_t>(end),
                     [&](const EntityEntry& lhs, const EntityEntry& rhs) {
                         const float leftCenter = CenterOnAxis(lhs);
                         const float rightCenter = CenterOnAxis(rhs);
                         if (leftCenter != rightCenter) {
                             return leftCenter < rightCenter;
                         }
                         return lhs.entity.index < rhs.entity.index;
                     });

    const std::size_t middle = begin + count / 2U;
    const std::int32_t internalIndex = AllocateNode();
    m_nodes[static_cast<std::size_t>(internalIndex)].parentIndex = parent;

    const std::int32_t leftChild = BuildRecursive(entries, begin, middle, internalIndex);
    const std::int32_t rightChild = BuildRecursive(entries, middle, end, internalIndex);

    Node& internalNode = m_nodes[static_cast<std::size_t>(internalIndex)];
    internalNode.leftChild = leftChild;
    internalNode.rightChild = rightChild;
    internalNode.entryCount = 0U;
    internalNode.bounds = m_nodes[static_cast<std::size_t>(leftChild)].bounds.UnionUVE(
        m_nodes[static_cast<std::size_t>(rightChild)].bounds);
    const std::int32_t leftHeight = m_nodes[static_cast<std::size_t>(leftChild)].height;
    const std::int32_t rightHeight = m_nodes[static_cast<std::size_t>(rightChild)].height;
    internalNode.height = 1 + (leftHeight > rightHeight ? leftHeight : rightHeight);

    return internalIndex;
}

void DynamicMeshBvhUVE::RebuildUVE() {
    if (m_entityCount == 0U) {
        ClearUVE();
        return;
    }
    std::vector<EntityEntry> currentEntries;
    currentEntries.reserve(m_entityCount);
    for (const auto& [entity, leafIndex] : m_entityToLeafNode) {
        if (leafIndex >= 0 && static_cast<std::size_t>(leafIndex) < m_nodes.size()) {
            const Node& leaf = m_nodes[static_cast<std::size_t>(leafIndex)];
            for (std::uint32_t i = 0U; i < leaf.entryCount; ++i) {
                if (leaf.entries[i].entity == entity) {
                    currentEntries.push_back(leaf.entries[i]);
                    break;
                }
            }
        }
    }
    BuildUVE(std::move(currentEntries));
}

void DynamicMeshBvhUVE::RefitFromLeafUVE(const std::size_t leafIndex) noexcept {
    if (leafIndex >= m_nodes.size()) {
        return;
    }

    std::int32_t current = static_cast<std::int32_t>(leafIndex);
    while (current != kInvalidNodeIndex && static_cast<std::size_t>(current) < m_nodes.size()) {
        Node& node = m_nodes[static_cast<std::size_t>(current)];
        if (node.IsLeaf()) {
            node.height = 0;
            if (node.entryCount > 0U) {
                Math::AabbUVE bounds = node.entries[0].bounds;
                for (std::uint32_t i = 1U; i < node.entryCount; ++i) {
                    bounds = bounds.UnionUVE(node.entries[i].bounds);
                }
                node.bounds = bounds;
            }
        } else {
            const bool hasLeft = node.leftChild != kInvalidNodeIndex &&
                                 static_cast<std::size_t>(node.leftChild) < m_nodes.size();
            const bool hasRight = node.rightChild != kInvalidNodeIndex &&
                                  static_cast<std::size_t>(node.rightChild) < m_nodes.size();
            if (hasLeft && hasRight) {
                const Node& leftNode = m_nodes[static_cast<std::size_t>(node.leftChild)];
                const Node& rightNode = m_nodes[static_cast<std::size_t>(node.rightChild)];
                node.bounds = leftNode.bounds.UnionUVE(rightNode.bounds);
                node.height = 1 + (leftNode.height > rightNode.height ? leftNode.height : rightNode.height);
            } else if (hasLeft) {
                const Node& leftNode = m_nodes[static_cast<std::size_t>(node.leftChild)];
                node.bounds = leftNode.bounds;
                node.height = 1 + leftNode.height;
            } else if (hasRight) {
                const Node& rightNode = m_nodes[static_cast<std::size_t>(node.rightChild)];
                node.bounds = rightNode.bounds;
                node.height = 1 + rightNode.height;
            }
        }
        current = node.parentIndex;
    }
}

std::int32_t DynamicMeshBvhUVE::FindBestLeafForInsertion(const Math::AabbUVE& bounds) const noexcept {
    if (m_root == kInvalidNodeIndex) {
        return kInvalidNodeIndex;
    }

    std::int32_t current = m_root;
    while (!m_nodes[static_cast<std::size_t>(current)].IsLeaf()) {
        const Node& currNode = m_nodes[static_cast<std::size_t>(current)];
        const std::int32_t left = currNode.leftChild;
        const std::int32_t right = currNode.rightChild;

        if (left == kInvalidNodeIndex) {
            current = right;
            continue;
        }
        if (right == kInvalidNodeIndex) {
            current = left;
            continue;
        }

        const Math::AabbUVE leftUnion = m_nodes[static_cast<std::size_t>(left)].bounds.UnionUVE(bounds);
        const float leftEnlargement = ComputeSurfaceAreaUVE(leftUnion) -
                                      ComputeSurfaceAreaUVE(m_nodes[static_cast<std::size_t>(left)].bounds);

        const Math::AabbUVE rightUnion = m_nodes[static_cast<std::size_t>(right)].bounds.UnionUVE(bounds);
        const float rightEnlargement = ComputeSurfaceAreaUVE(rightUnion) -
                                       ComputeSurfaceAreaUVE(m_nodes[static_cast<std::size_t>(right)].bounds);

        if (leftEnlargement < rightEnlargement) {
            current = left;
        } else {
            current = right;
        }
    }
    return current;
}

void DynamicMeshBvhUVE::SplitLeaf(const std::int32_t leafIndex, const EntityEntry& newEntry) {
    std::array<EntityEntry, kLeafCapacity + 1U> splitEntries{};
    const Node& oldLeaf = m_nodes[static_cast<std::size_t>(leafIndex)];
    for (std::size_t i = 0U; i < kLeafCapacity; ++i) {
        splitEntries[i] = oldLeaf.entries[i];
    }
    splitEntries[kLeafCapacity] = newEntry;

    Math::AabbUVE unionBounds = splitEntries[0].bounds;
    for (std::size_t i = 1U; i <= kLeafCapacity; ++i) {
        unionBounds = unionBounds.UnionUVE(splitEntries[i].bounds);
    }

    const Math::Vector3UVE extents = unionBounds.GetExtentsUVE();
    std::size_t splitAxis = 0U;
    if (extents.y > extents.x && extents.y >= extents.z) {
        splitAxis = 1U;
    } else if (extents.z > extents.x && extents.z > extents.y) {
        splitAxis = 2U;
    }

    const auto CenterOnAxis = [splitAxis](const EntityEntry& entry) noexcept {
        const Math::Vector3UVE center = entry.bounds.GetCenterUVE();
        return splitAxis == 0U ? center.x : (splitAxis == 1U ? center.y : center.z);
    };

    std::stable_sort(splitEntries.begin(), splitEntries.end(),
                     [&](const EntityEntry& lhs, const EntityEntry& rhs) {
                         const float leftCenter = CenterOnAxis(lhs);
                         const float rightCenter = CenterOnAxis(rhs);
                         if (leftCenter != rightCenter) {
                             return leftCenter < rightCenter;
                         }
                         return lhs.entity.index < rhs.entity.index;
                     });

    const std::int32_t leftChildIndex = AllocateNode();
    const std::int32_t rightChildIndex = AllocateNode();

    constexpr std::size_t leftCount = 2U;
    constexpr std::size_t rightCount = (kLeafCapacity + 1U) - leftCount;

    // Configure left child
    {
        Node& leftChild = m_nodes[static_cast<std::size_t>(leftChildIndex)];
        leftChild.parentIndex = leafIndex;
        leftChild.leftChild = kInvalidNodeIndex;
        leftChild.rightChild = kInvalidNodeIndex;
        leftChild.entryCount = static_cast<std::uint32_t>(leftCount);
        Math::AabbUVE leftBounds = splitEntries[0].bounds;
        for (std::size_t i = 0U; i < leftCount; ++i) {
            leftChild.entries[i] = splitEntries[i];
            if (i > 0U) {
                leftBounds = leftBounds.UnionUVE(splitEntries[i].bounds);
            }
            m_entityToLeafNode[splitEntries[i].entity] = leftChildIndex;
        }
        leftChild.bounds = leftBounds;
    }

    // Configure right child
    {
        Node& rightChild = m_nodes[static_cast<std::size_t>(rightChildIndex)];
        rightChild.parentIndex = leafIndex;
        rightChild.leftChild = kInvalidNodeIndex;
        rightChild.rightChild = kInvalidNodeIndex;
        rightChild.entryCount = static_cast<std::uint32_t>(rightCount);
        Math::AabbUVE rightBounds = splitEntries[leftCount].bounds;
        for (std::size_t i = 0U; i < rightCount; ++i) {
            rightChild.entries[i] = splitEntries[leftCount + i];
            if (i > 0U) {
                rightBounds = rightBounds.UnionUVE(splitEntries[leftCount + i].bounds);
            }
            m_entityToLeafNode[splitEntries[leftCount + i].entity] = rightChildIndex;
        }
        rightChild.bounds = rightBounds;
    }

    // Convert leafIndex to an internal node
    {
        Node& internalNode = m_nodes[static_cast<std::size_t>(leafIndex)];
        internalNode.leftChild = leftChildIndex;
        internalNode.rightChild = rightChildIndex;
        internalNode.entryCount = 0U;
        for (std::size_t i = 0U; i < kLeafCapacity; ++i) {
            internalNode.entries[i] = EntityEntry{};
        }
        internalNode.bounds = m_nodes[static_cast<std::size_t>(leftChildIndex)].bounds.UnionUVE(
            m_nodes[static_cast<std::size_t>(rightChildIndex)].bounds);
    }

    ++m_entityCount;
    RefitFromLeafUVE(static_cast<std::size_t>(leafIndex));
    RebalanceTowardRootUVE(leafIndex);
}

void DynamicMeshBvhUVE::RebalanceTowardRootUVE(const std::int32_t nodeIndex) noexcept {
    std::int32_t current = nodeIndex;
    while (current != kInvalidNodeIndex && static_cast<std::size_t>(current) < m_nodes.size()) {
        const std::int32_t parentIndex = m_nodes[static_cast<std::size_t>(current)].parentIndex;
        if (parentIndex == kInvalidNodeIndex || static_cast<std::size_t>(parentIndex) >= m_nodes.size()) {
            break;
        }
        const Node& parent = m_nodes[static_cast<std::size_t>(parentIndex)];
        const std::int32_t leftHeight = parent.leftChild != kInvalidNodeIndex
                                            ? m_nodes[static_cast<std::size_t>(parent.leftChild)].height
                                            : -1;
        const std::int32_t rightHeight = parent.rightChild != kInvalidNodeIndex
                                             ? m_nodes[static_cast<std::size_t>(parent.rightChild)].height
                                             : -1;
        if (leftHeight >= rightHeight + 2) {
            RotateHeavyChildUVE(parentIndex, true);
        } else if (rightHeight >= leftHeight + 2) {
            RotateHeavyChildUVE(parentIndex, false);
        }
        current = parentIndex;
    }
}

void DynamicMeshBvhUVE::RotateHeavyChildUVE(const std::int32_t nodeIndex, const bool heavyIsLeft) noexcept {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= m_nodes.size()) {
        return;
    }
    const std::int32_t heavyIndex =
        heavyIsLeft ? m_nodes[static_cast<std::size_t>(nodeIndex)].leftChild
                    : m_nodes[static_cast<std::size_t>(nodeIndex)].rightChild;
    if (heavyIndex == kInvalidNodeIndex || static_cast<std::size_t>(heavyIndex) >= m_nodes.size()) {
        return;
    }
    Node& heavy = m_nodes[static_cast<std::size_t>(heavyIndex)];
    if (heavy.IsLeaf()) {
        return; // Nothing to promote; a leaf sibling pair cannot differ in height by two.
    }

    // Promote whichever grandchild stands taller (the left one wins ties).
    const std::int32_t leftGrandChild = heavy.leftChild;
    const std::int32_t rightGrandChild = heavy.rightChild;
    const std::int32_t leftHeight = leftGrandChild != kInvalidNodeIndex
                                        ? m_nodes[static_cast<std::size_t>(leftGrandChild)].height
                                        : -1;
    const std::int32_t rightHeight = rightGrandChild != kInvalidNodeIndex
                                         ? m_nodes[static_cast<std::size_t>(rightGrandChild)].height
                                         : -1;
    const bool promoteLeft = leftHeight >= rightHeight;
    const std::int32_t demotedIndex = promoteLeft ? rightGrandChild : leftGrandChild;

    const std::int32_t grandParentIndex = m_nodes[static_cast<std::size_t>(nodeIndex)].parentIndex;

    // The demoted grandchild takes the heavy child's old slot under nodeIndex.
    if (heavyIsLeft) {
        m_nodes[static_cast<std::size_t>(nodeIndex)].leftChild = demotedIndex;
    } else {
        m_nodes[static_cast<std::size_t>(nodeIndex)].rightChild = demotedIndex;
    }
    if (demotedIndex != kInvalidNodeIndex) {
        m_nodes[static_cast<std::size_t>(demotedIndex)].parentIndex = nodeIndex;
    }

    // nodeIndex hangs under the heavy child, in the slot the demoted grandchild vacated.
    if (promoteLeft) {
        m_nodes[static_cast<std::size_t>(heavyIndex)].rightChild = nodeIndex;
    } else {
        m_nodes[static_cast<std::size_t>(heavyIndex)].leftChild = nodeIndex;
    }
    m_nodes[static_cast<std::size_t>(nodeIndex)].parentIndex = heavyIndex;

    // The heavy child takes nodeIndex's place in the tree.
    m_nodes[static_cast<std::size_t>(heavyIndex)].parentIndex = grandParentIndex;
    if (grandParentIndex != kInvalidNodeIndex && static_cast<std::size_t>(grandParentIndex) < m_nodes.size()) {
        if (m_nodes[static_cast<std::size_t>(grandParentIndex)].leftChild == nodeIndex) {
            m_nodes[static_cast<std::size_t>(grandParentIndex)].leftChild = heavyIndex;
        } else if (m_nodes[static_cast<std::size_t>(grandParentIndex)].rightChild == nodeIndex) {
            m_nodes[static_cast<std::size_t>(grandParentIndex)].rightChild = heavyIndex;
        }
    } else {
        m_root = heavyIndex;
    }

    // nodeIndex sits deeper now; the refit walks from it up to the root, repairing bounds and
    // heights across the rotated subtree and every ancestor above it.
    RefitFromLeafUVE(static_cast<std::size_t>(nodeIndex));
}

void DynamicMeshBvhUVE::InsertOrUpdateUVE(const Scene::EntityUVE entity, const Math::AabbUVE& worldBounds) {
    if (entity == Scene::kInvalidEntityUVE || !IsValidAabbUVE(worldBounds)) {
        return;
    }

    const auto existingIt = m_entityToLeafNode.find(entity);
    if (existingIt != m_entityToLeafNode.end()) {
        const std::int32_t leafIndex = existingIt->second;
        if (leafIndex >= 0 && static_cast<std::size_t>(leafIndex) < m_nodes.size()) {
            Node& leaf = m_nodes[static_cast<std::size_t>(leafIndex)];
            for (std::uint32_t i = 0U; i < leaf.entryCount; ++i) {
                if (leaf.entries[i].entity == entity) {
                    leaf.entries[i].bounds = worldBounds;
                    RefitFromLeafUVE(static_cast<std::size_t>(leafIndex));
                    return;
                }
            }
        }
    }

    const EntityEntry newEntry{entity, worldBounds};

    if (m_root == kInvalidNodeIndex) {
        const std::int32_t rootIndex = AllocateNode();
        Node& rootNode = m_nodes[static_cast<std::size_t>(rootIndex)];
        rootNode.parentIndex = kInvalidNodeIndex;
        rootNode.leftChild = kInvalidNodeIndex;
        rootNode.rightChild = kInvalidNodeIndex;
        rootNode.entryCount = 1U;
        rootNode.entries[0] = newEntry;
        rootNode.bounds = worldBounds;

        m_root = rootIndex;
        m_entityToLeafNode[entity] = rootIndex;
        m_entityCount = 1U;
        return;
    }

    const std::int32_t targetLeafIndex = FindBestLeafForInsertion(worldBounds);
    Node& targetLeaf = m_nodes[static_cast<std::size_t>(targetLeafIndex)];

    if (targetLeaf.entryCount < kLeafCapacity) {
        targetLeaf.entries[targetLeaf.entryCount] = newEntry;
        ++targetLeaf.entryCount;
        targetLeaf.bounds = targetLeaf.bounds.UnionUVE(worldBounds);
        m_entityToLeafNode[entity] = targetLeafIndex;
        ++m_entityCount;
        RefitFromLeafUVE(static_cast<std::size_t>(targetLeafIndex));
        RebalanceTowardRootUVE(targetLeafIndex);
    } else {
        SplitLeaf(targetLeafIndex, newEntry);
    }
}

void DynamicMeshBvhUVE::RemoveUVE(const Scene::EntityUVE entity) {
    static_cast<void>(RemoveEntityUVE(entity));
}

bool DynamicMeshBvhUVE::RemoveEntityUVE(const Scene::EntityUVE entity) {
    if (entity == Scene::kInvalidEntityUVE) {
        return false;
    }

    const auto it = m_entityToLeafNode.find(entity);
    if (it == m_entityToLeafNode.end()) {
        return false;
    }

    const std::int32_t leafIndex = it->second;
    m_entityToLeafNode.erase(it);
    --m_entityCount;

    if (leafIndex < 0 || static_cast<std::size_t>(leafIndex) >= m_nodes.size()) {
        return true;
    }

    Node& leaf = m_nodes[static_cast<std::size_t>(leafIndex)];
    for (std::uint32_t i = 0U; i < leaf.entryCount; ++i) {
        if (leaf.entries[i].entity == entity) {
            leaf.entries[i] = leaf.entries[leaf.entryCount - 1U];
            leaf.entries[leaf.entryCount - 1U] = EntityEntry{};
            --leaf.entryCount;
            break;
        }
    }

    if (leaf.entryCount > 0U) {
        RefitFromLeafUVE(static_cast<std::size_t>(leafIndex));
        return true;
    }

    // Leaf is now empty
    if (leafIndex == m_root) {
        FreeNode(leafIndex);
        m_root = kInvalidNodeIndex;
        return true;
    }

    const std::int32_t parentIndex = m_nodes[static_cast<std::size_t>(leafIndex)].parentIndex;
    if (parentIndex == kInvalidNodeIndex || static_cast<std::size_t>(parentIndex) >= m_nodes.size()) {
        FreeNode(leafIndex);
        return true;
    }

    const Node& parentNode = m_nodes[static_cast<std::size_t>(parentIndex)];
    const std::int32_t grandParentIndex = parentNode.parentIndex;
    const std::int32_t siblingIndex = (parentNode.leftChild == leafIndex)
                                          ? parentNode.rightChild
                                          : parentNode.leftChild;

    if (siblingIndex != kInvalidNodeIndex && static_cast<std::size_t>(siblingIndex) < m_nodes.size()) {
        if (grandParentIndex == kInvalidNodeIndex) {
            m_root = siblingIndex;
            m_nodes[static_cast<std::size_t>(siblingIndex)].parentIndex = kInvalidNodeIndex;
        } else {
            Node& grandParent = m_nodes[static_cast<std::size_t>(grandParentIndex)];
            if (grandParent.leftChild == parentIndex) {
                grandParent.leftChild = siblingIndex;
            } else {
                grandParent.rightChild = siblingIndex;
            }
            m_nodes[static_cast<std::size_t>(siblingIndex)].parentIndex = grandParentIndex;
        }
        RefitFromLeafUVE(static_cast<std::size_t>(siblingIndex));
        RebalanceTowardRootUVE(siblingIndex);
    }

    FreeNode(leafIndex);
    FreeNode(parentIndex);
    return true;
}

void DynamicMeshBvhUVE::QueryFrustumUVE(const Math::FrustumUVE& frustum,
                                        std::vector<Scene::EntityUVE>& outVisibleEntities) const {
    outVisibleEntities.clear();
    if (m_root == kInvalidNodeIndex || m_nodes.empty()) {
        return;
    }

    struct TraversalEntry {
        std::int32_t nodeIndex;
        bool fullyInside;
    };

    constexpr std::size_t kMaxStackCapacity = 256U;
    std::array<TraversalEntry, kMaxStackCapacity> fixedStack{};
    std::vector<TraversalEntry> dynamicStack;
    std::size_t fixedStackSize = 0U;

    const auto PushStack = [&](const TraversalEntry entry) {
        if (dynamicStack.empty()) {
            if (fixedStackSize < kMaxStackCapacity) {
                fixedStack[fixedStackSize++] = entry;
                return;
            }
            dynamicStack.reserve(kMaxStackCapacity * 2U);
            for (std::size_t i = 0U; i < fixedStackSize; ++i) {
                dynamicStack.push_back(fixedStack[i]);
            }
            fixedStackSize = 0U;
        }
        dynamicStack.push_back(entry);
    };

    const auto PopStack = [&]() -> TraversalEntry {
        if (!dynamicStack.empty()) {
            const TraversalEntry top = dynamicStack.back();
            dynamicStack.pop_back();
            return top;
        }
        return fixedStack[--fixedStackSize];
    };

    const auto HasItems = [&]() noexcept -> bool {
        return fixedStackSize > 0U || !dynamicStack.empty();
    };

    PushStack(TraversalEntry{m_root, false});

    while (HasItems()) {
        const TraversalEntry current = PopStack();
        if (current.nodeIndex < 0 || static_cast<std::size_t>(current.nodeIndex) >= m_nodes.size()) {
            continue;
        }

        const Node& node = m_nodes[static_cast<std::size_t>(current.nodeIndex)];

        if (current.fullyInside) {
            // Entire subtree accepted without further plane testing
            if (node.IsLeaf()) {
                for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                    outVisibleEntities.push_back(node.entries[i].entity);
                }
            } else {
                if (node.rightChild != kInvalidNodeIndex) {
                    PushStack(TraversalEntry{node.rightChild, true});
                }
                if (node.leftChild != kInvalidNodeIndex) {
                    PushStack(TraversalEntry{node.leftChild, true});
                }
            }
            continue;
        }

        const Math::Vector3UVE center = node.bounds.GetCenterUVE();
        const Math::Vector3UVE extents = node.bounds.GetExtentsUVE();
        bool nodeFullyInside = true;
        bool isOutside = false;

        for (const Math::PlaneUVE& plane : frustum.planes) {
            const float radius = extents.x * std::abs(plane.normal.x) +
                                 extents.y * std::abs(plane.normal.y) +
                                 extents.z * std::abs(plane.normal.z);
            const float signedDist = plane.GetSignedDistanceUVE(center);
            if (signedDist + radius < 0.0F) {
                isOutside = true;
                break;
            }
            if (signedDist - radius < 0.0F) {
                nodeFullyInside = false;
            }
        }

        if (isOutside) {
            // Prune entire branch immediately
            continue;
        }

        if (nodeFullyInside) {
            // Entire subtree accepted without further plane testing
            if (node.IsLeaf()) {
                for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                    outVisibleEntities.push_back(node.entries[i].entity);
                }
            } else {
                if (node.rightChild != kInvalidNodeIndex) {
                    PushStack(TraversalEntry{node.rightChild, true});
                }
                if (node.leftChild != kInvalidNodeIndex) {
                    PushStack(TraversalEntry{node.leftChild, true});
                }
            }
            continue;
        }

        // Partially inside
        if (node.IsLeaf()) {
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                if (frustum.IntersectsUVE(node.entries[i].bounds)) {
                    outVisibleEntities.push_back(node.entries[i].entity);
                }
            }
        } else {
            if (node.rightChild != kInvalidNodeIndex) {
                PushStack(TraversalEntry{node.rightChild, false});
            }
            if (node.leftChild != kInvalidNodeIndex) {
                PushStack(TraversalEntry{node.leftChild, false});
            }
        }
    }
}

void DynamicMeshBvhUVE::QueryAabbUVE(const Math::AabbUVE& queryBounds,
                                     std::vector<Scene::EntityUVE>& outEntities) const {
    outEntities.clear();
    if (m_root == kInvalidNodeIndex || m_nodes.empty() || !IsValidAabbUVE(queryBounds)) {
        return;
    }

    constexpr std::size_t kMaxStackCapacity = 256U;
    std::array<std::int32_t, kMaxStackCapacity> fixedStack{};
    std::vector<std::int32_t> dynamicStack;
    std::size_t fixedStackSize = 0U;

    const auto PushStack = [&](const std::int32_t idx) {
        if (dynamicStack.empty()) {
            if (fixedStackSize < kMaxStackCapacity) {
                fixedStack[fixedStackSize++] = idx;
                return;
            }
            dynamicStack.reserve(kMaxStackCapacity * 2U);
            for (std::size_t i = 0U; i < fixedStackSize; ++i) {
                dynamicStack.push_back(fixedStack[i]);
            }
            fixedStackSize = 0U;
        }
        dynamicStack.push_back(idx);
    };

    const auto PopStack = [&]() -> std::int32_t {
        if (!dynamicStack.empty()) {
            const std::int32_t top = dynamicStack.back();
            dynamicStack.pop_back();
            return top;
        }
        return fixedStack[--fixedStackSize];
    };

    const auto HasItems = [&]() noexcept -> bool {
        return fixedStackSize > 0U || !dynamicStack.empty();
    };

    PushStack(m_root);

    while (HasItems()) {
        const std::int32_t nodeIdx = PopStack();
        if (nodeIdx < 0 || static_cast<std::size_t>(nodeIdx) >= m_nodes.size()) {
            continue;
        }

        const Node& node = m_nodes[static_cast<std::size_t>(nodeIdx)];
        if (!queryBounds.IntersectsUVE(node.bounds)) {
            continue;
        }

        if (node.IsLeaf()) {
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                if (queryBounds.IntersectsUVE(node.entries[i].bounds)) {
                    outEntities.push_back(node.entries[i].entity);
                }
            }
        } else {
            if (node.rightChild != kInvalidNodeIndex) {
                PushStack(node.rightChild);
            }
            if (node.leftChild != kInvalidNodeIndex) {
                PushStack(node.leftChild);
            }
        }
    }
}

void DynamicMeshBvhUVE::QueryRayUVE(const Math::RayUVE& ray, const float maxDistance,
                                    std::vector<Scene::EntityUVE>& outEntities) const {
    outEntities.clear();
    if (m_root == kInvalidNodeIndex || m_nodes.empty() || maxDistance < 0.0F) {
        return;
    }

    constexpr std::size_t kMaxStackCapacity = 256U;
    std::array<std::int32_t, kMaxStackCapacity> fixedStack{};
    std::vector<std::int32_t> dynamicStack;
    std::size_t fixedStackSize = 0U;

    const auto PushStack = [&](const std::int32_t idx) {
        if (dynamicStack.empty()) {
            if (fixedStackSize < kMaxStackCapacity) {
                fixedStack[fixedStackSize++] = idx;
                return;
            }
            dynamicStack.reserve(kMaxStackCapacity * 2U);
            for (std::size_t i = 0U; i < fixedStackSize; ++i) {
                dynamicStack.push_back(fixedStack[i]);
            }
            fixedStackSize = 0U;
        }
        dynamicStack.push_back(idx);
    };

    const auto PopStack = [&]() -> std::int32_t {
        if (!dynamicStack.empty()) {
            const std::int32_t top = dynamicStack.back();
            dynamicStack.pop_back();
            return top;
        }
        return fixedStack[--fixedStackSize];
    };

    const auto HasItems = [&]() noexcept -> bool {
        return fixedStackSize > 0U || !dynamicStack.empty();
    };

    PushStack(m_root);

    while (HasItems()) {
        const std::int32_t nodeIdx = PopStack();
        if (nodeIdx < 0 || static_cast<std::size_t>(nodeIdx) >= m_nodes.size()) {
            continue;
        }

        const Node& node = m_nodes[static_cast<std::size_t>(nodeIdx)];
        if (!Math::IntersectRayUVE(ray, node.bounds, maxDistance).has_value()) {
            continue;
        }

        if (node.IsLeaf()) {
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                if (Math::IntersectRayUVE(ray, node.entries[i].bounds, maxDistance).has_value()) {
                    outEntities.push_back(node.entries[i].entity);
                }
            }
        } else {
            if (node.rightChild != kInvalidNodeIndex) {
                PushStack(node.rightChild);
            }
            if (node.leftChild != kInvalidNodeIndex) {
                PushStack(node.leftChild);
            }
        }
    }
}

std::size_t DynamicMeshBvhUVE::GetEntityCountUVE() const noexcept {
    return m_entityCount;
}

std::size_t DynamicMeshBvhUVE::GetNodeCountUVE() const noexcept {
    return m_nodes.size() - m_freeNodeIndices.size();
}

std::int32_t DynamicMeshBvhUVE::GetRootIndexUVE() const noexcept {
    return m_root;
}

bool DynamicMeshBvhUVE::IsEmptyUVE() const noexcept {
    return m_entityCount == 0U;
}

bool DynamicMeshBvhUVE::ContainsEntityUVE(const Scene::EntityUVE entity) const noexcept {
    return m_entityToLeafNode.contains(entity);
}

std::int32_t DynamicMeshBvhUVE::GetEntityLeafIndexUVE(const Scene::EntityUVE entity) const noexcept {
    const auto it = m_entityToLeafNode.find(entity);
    return it != m_entityToLeafNode.end() ? it->second : kInvalidNodeIndex;
}

std::optional<Math::AabbUVE> DynamicMeshBvhUVE::GetEntityBoundsUVE(const Scene::EntityUVE entity) const noexcept {
    const auto it = m_entityToLeafNode.find(entity);
    if (it != m_entityToLeafNode.end()) {
        const std::int32_t leafIdx = it->second;
        if (leafIdx >= 0 && static_cast<std::size_t>(leafIdx) < m_nodes.size()) {
            const Node& leaf = m_nodes[static_cast<std::size_t>(leafIdx)];
            for (std::uint32_t i = 0U; i < leaf.entryCount; ++i) {
                if (leaf.entries[i].entity == entity) {
                    return leaf.entries[i].bounds;
                }
            }
        }
    }
    return std::nullopt;
}

const DynamicMeshBvhUVE::Node& DynamicMeshBvhUVE::GetNodeUVE(const std::size_t index) const {
    if (index >= m_nodes.size()) {
        throw std::out_of_range("DynamicMeshBvhUVE::GetNodeUVE: index out of range");
    }
    return m_nodes[index];
}

const std::vector<DynamicMeshBvhUVE::Node>& DynamicMeshBvhUVE::GetNodesUVE() const noexcept {
    return m_nodes;
}

DynamicMeshBvhUVE::TreeStatsUVE DynamicMeshBvhUVE::ComputeStatsUVE() const noexcept {
    TreeStatsUVE stats{};
    stats.entityCount = m_entityCount;
    stats.nodeCount = GetNodeCountUVE();

    if (m_root == kInvalidNodeIndex || m_nodes.empty()) {
        return stats;
    }

    struct StackNode {
        std::int32_t index;
        std::size_t depth;
    };

    std::vector<StackNode> stack;
    stack.push_back(StackNode{m_root, 1U});

    while (!stack.empty()) {
        const StackNode current = stack.back();
        stack.pop_back();

        if (current.index < 0 || static_cast<std::size_t>(current.index) >= m_nodes.size()) {
            continue;
        }

        stats.maxDepth = std::max(stats.maxDepth, current.depth);
        const Node& node = m_nodes[static_cast<std::size_t>(current.index)];

        if (node.IsLeaf()) {
            ++stats.leafCount;
        } else {
            ++stats.internalCount;
            if (node.rightChild != kInvalidNodeIndex) {
                stack.push_back(StackNode{node.rightChild, current.depth + 1U});
            }
            if (node.leftChild != kInvalidNodeIndex) {
                stack.push_back(StackNode{node.leftChild, current.depth + 1U});
            }
        }
    }

    if (stats.leafCount > 0U) {
        stats.averageLeafFill = static_cast<float>(stats.entityCount) / static_cast<float>(stats.leafCount);
    }

    return stats;
}

} // namespace UVE::Render
