// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "entity.h"
#include "aabb.h"
#include "frustum.h"
#include "ray.h"

namespace UVE::Render {

/// DynamicMeshBvh is a binary Axis-Aligned Bounding Box (AABB) Bounding Volume Hierarchy
/// designed for high-performance spatial indexing and frustum culling of mesh entities.
///
/// Key Architectural Properties:
/// - Binary tree representation with contiguous node storage in std::vector<Node>.
/// - Leaf nodes store up to `kLeafCapacity = 4` entity entries (`Scene::Entity` + `Math::Aabb`).
/// - Top-down median-split construction along the axis of longest extent.
/// - Incremental insertion using Surface Area Heuristic (SAH) enlargement minimization and dynamic leaf splitting.
/// - Incremental entity removal with sibling contraction to keep tree balanced and eliminate empty nodes.
/// - Fast bottom-up refitting (`RefitFromLeaf`) updating parent bounds up to the root in O(log N)
///   time when an entity transforms without reallocating tree nodes or changing topology.
/// - Fast iterative stack-based frustum culling query (`QueryFrustum`) evaluating plane equations;
///   pruning subtrees entirely when outside, and accepting entire subtrees when completely inside
///   without further plane testing.
class DynamicMeshBvh {
public:
    static constexpr std::size_t kLeafCapacity = 4U;
    static constexpr std::int32_t kInvalidNodeIndex = -1;

    /// An entity entry stored within a leaf node.
    struct EntityEntry {
        Scene::Entity entity = Scene::kInvalidEntity;
        Math::Aabb bounds{};

        [[nodiscard]] constexpr bool operator==(const EntityEntry& other) const noexcept {
            return entity == other.entity && bounds == other.bounds;
        }

        [[nodiscard]] constexpr bool operator!=(const EntityEntry& other) const noexcept {
            return !(*this == other);
        }
    };

    /// Node representation in the binary BVH.
    struct Node {
        Math::Aabb bounds{};
        std::int32_t leftChild = kInvalidNodeIndex;
        std::int32_t rightChild = kInvalidNodeIndex;
        std::int32_t parentIndex = kInvalidNodeIndex;
        /// Subtree height, leaves at 0. Maintained bottom-up by every refit so insertion-time
        /// rebalancing can compare sibling heights in O(1) instead of walking subtrees.
        std::int32_t height = 0;
        std::array<EntityEntry, kLeafCapacity> entries{};
        std::uint32_t entryCount = 0U;

        [[nodiscard]] constexpr bool IsLeaf() const noexcept {
            return leftChild == kInvalidNodeIndex && rightChild == kInvalidNodeIndex;
        }
    };

    /// Diagnostic statistics describing the BVH topology.
    struct TreeStats {
        std::size_t nodeCount = 0U;
        std::size_t leafCount = 0U;
        std::size_t internalCount = 0U;
        std::size_t entityCount = 0U;
        std::size_t maxDepth = 0U;
        float averageLeafFill = 0.0F;
    };

    DynamicMeshBvh() = default;
    explicit DynamicMeshBvh(std::vector<EntityEntry> entries);

    ~DynamicMeshBvh() = default;
    DynamicMeshBvh(const DynamicMeshBvh&) = default;
    DynamicMeshBvh& operator=(const DynamicMeshBvh&) = default;
    DynamicMeshBvh(DynamicMeshBvh&&) noexcept = default;
    DynamicMeshBvh& operator=(DynamicMeshBvh&&) noexcept = default;

    /// Builds the BVH top-down from scratch using median split partitioning along the longest extent axis.
    void Build(std::vector<EntityEntry> entries);

    /// Rebuilds the current tree topology using top-down median split from existing entities.
    void Rebuild();

    /// Inserts a new entity or updates an existing entity's world bounding box.
    /// If the entity already exists in the BVH, its bounds are updated and RefitFromLeaf
    /// is called to update ancestors in O(log N) time without node reallocations.
    /// If the entity does not exist, it is incrementally inserted using SAH surface area enlargement.
    void InsertOrUpdate(Scene::Entity entity, const Math::Aabb& worldBounds);

    /// Removes an entity from the BVH.
    /// If the entity was the last element in its leaf node, the node is contracted and
    /// its sibling is promoted, maintaining a clean binary tree structure with zero empty leaves.
    void Remove(Scene::Entity entity);

    /// Removes an entity from the BVH, returning true if the entity was found and removed.
    bool RemoveEntity(Scene::Entity entity);

    /// Bottom-up refitting starting from a given node index (leaf or internal) up to the root.
    /// Recomputes bounds in O(log N) time without reallocating nodes.
    void RefitFromLeaf(std::size_t leafIndex) noexcept;

    /// Traverses the BVH against the view frustum and populates outVisibleEntities.
    /// Evaluates frustum planes hierarchically:
    /// - If a node is completely outside any frustum plane, its entire subtree is pruned immediately.
    /// - If a node is completely inside all frustum planes, its entire subtree is accepted without
    ///   any further plane testing.
    /// - If a node intersects the frustum boundary, leaf entities are tested individually and internal
    ///   children are pushed to the traversal stack.
    void QueryFrustum(const Math::Frustum& frustum,
                         std::vector<Scene::Entity>& outVisibleEntities) const;

    /// Queries entities whose bounds intersect the given query AABB.
    void QueryAabb(const Math::Aabb& queryBounds,
                      std::vector<Scene::Entity>& outEntities) const;

    /// Queries entities whose bounds intersect the given query ray within [0, maxDistance].
    void QueryRay(const Math::Ray& ray, float maxDistance,
                     std::vector<Scene::Entity>& outEntities) const;

    /// Clears the hierarchy, releasing all nodes and entity mappings.
    void Clear() noexcept;

    /// Returns the total number of indexed entities.
    [[nodiscard]] std::size_t GetEntityCount() const noexcept;

    /// Returns the total number of active allocated nodes in the tree.
    [[nodiscard]] std::size_t GetNodeCount() const noexcept;

    /// Returns the index of the root node, or kInvalidNodeIndex if empty.
    [[nodiscard]] std::int32_t GetRootIndex() const noexcept;

    /// Returns true if the hierarchy contains zero entities.
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// Returns true if the entity is currently present in the BVH.
    [[nodiscard]] bool ContainsEntity(Scene::Entity entity) const noexcept;

    /// Returns the leaf node index for the given entity, or kInvalidNodeIndex if not present.
    [[nodiscard]] std::int32_t GetEntityLeafIndex(Scene::Entity entity) const noexcept;

    /// Returns the current world bounds for the given entity, if present.
    [[nodiscard]] std::optional<Math::Aabb> GetEntityBounds(Scene::Entity entity) const noexcept;

    /// Returns a const reference to a node by index.
    [[nodiscard]] const Node& GetNode(std::size_t index) const;

    /// Returns const access to the internal node array.
    [[nodiscard]] const std::vector<Node>& GetNodes() const noexcept;

    /// Computes and returns topology diagnostics and statistics.
    [[nodiscard]] TreeStats ComputeStats() const noexcept;

private:
    [[nodiscard]] std::int32_t AllocateNode();
    void FreeNode(std::int32_t index) noexcept;

    std::int32_t BuildRecursive(std::vector<EntityEntry>& entries, std::size_t begin, std::size_t end,
                                std::int32_t parent);

    [[nodiscard]] std::int32_t FindBestLeafForInsertion(const Math::Aabb& bounds) const noexcept;
    void SplitLeaf(std::int32_t leafIndex, const EntityEntry& newEntry);

    /// Walks from a node to the root, rotating wherever sibling subtree heights differ by two
    /// or more (AVL-style). Bounds and heights are refit along the way, so the tree stays
    /// O(log N) deep under ordered incremental insertion instead of degenerating into a spine.
    /// Leaf content never moves during a rotation, so the entity-to-leaf map stays valid.
    void RebalanceTowardRoot(std::int32_t nodeIndex) noexcept;

    /// Promotes the taller grandchild of `nodeIndex`'s heavier child, taking that child's place
    /// in the tree. `heavyIsLeft` selects which child of `nodeIndex` is the tall one.
    void RotateHeavyChild(std::int32_t nodeIndex, bool heavyIsLeft) noexcept;

    std::vector<Node> m_nodes;
    std::unordered_map<Scene::Entity, std::int32_t> m_entityToLeafNode;
    std::vector<std::int32_t> m_freeNodeIndices;
    std::int32_t m_root = kInvalidNodeIndex;
    std::size_t m_entityCount = 0U;
};

} // namespace UVE::Render
